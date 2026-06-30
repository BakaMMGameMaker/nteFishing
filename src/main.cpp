// 异环钓鱼自动化工具
//
// 通过屏幕截图 + OpenCV 模板匹配识别游戏 UI 元素，
// 通过 Interception 内核驱动在 HID 层注入键盘/鼠标事件，
// 实现钓鱼流程全自动化。

// 在任何 <windows.h> 之前定义，防止与 OpenCV 的 min/max 冲突，减少不必要头文件的引入
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "interception_driver.h"
#include "follower.h"

#include <iostream>
#include <optional>
#include <string>

#include <opencv2/opencv.hpp>

/// 模板匹配置信度阈值
constexpr double kMatchThreshold = 0.80;

// 全局 Interception 实例
InterceptionDriver g_Interception;

/// 模板匹配结果
struct FoundImg {
    int    FoundAtX       = 0;    // 匹配位置 X 坐标（左上角，相对于截图区域）
    int    FoundAtY       = 0;    // 匹配位置 Y 坐标
    int    TemplateWidth  = 0;    // 模板图像宽度
    int    TemplateHeight = 0;    // 模板图像高度
    double Confidence     = 0.0;  // 匹配置信度（TM_CCOEFF_NORMED 相关系数）
};

/// 带标签的图像
struct LabeledImage {
    cv::Mat     Mat;    // 图像数据
    std::string Label;  // 模板名称标签（用于日志）
};

/// 输出日志信息
void Log(const std::string& Message) {
    std::cout << Message << '\n';
}

/// 获取主显示器屏幕宽度（像素）
int GetScreenWidth() {
    return GetSystemMetrics(SM_CXSCREEN);
}

/// 获取主显示器屏幕高度（像素）
int GetScreenHeight() {
    return GetSystemMetrics(SM_CYSCREEN);
}

/// 截取屏幕指定比例区域，返回 BGR 格式图像
/// @param ScreenW, ScreenH  屏幕宽高（像素）
/// @param X1, Y1  左上角比例坐标（0.0~1.0，含）
/// @param X2, Y2  右下角比例坐标（0.0~1.0，不含）
/// @return BGR 3 通道 cv::Mat；失败或无效区域时返回空 Mat
cv::Mat GetScreenArea(
    const int ScreenW, const int ScreenH,
    const double X1, const double Y1,
    const double X2, const double Y2
) {
    // 比例坐标 → 像素坐标
    int x1 = static_cast<int>(X1 * ScreenW);
    int y1 = static_cast<int>(Y1 * ScreenH);
    int x2 = static_cast<int>(X2 * ScreenW);
    int y2 = static_cast<int>(Y2 * ScreenH);

    // 边界检查与裁切
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    const int width  = x2 - x1;
    const int height = y2 - y1;
    if (width <= 0 || height <= 0) return cv::Mat();

    // 获取桌面设备上下文
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) return cv::Mat();

    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    if (!hMemDC) {
        ReleaseDC(NULL, hScreenDC);
        return cv::Mat();
    }

    // 创建 32 位 DIBSection，直接获取像素缓冲区指针
    // biHeight 为负值表示 top-down（与 OpenCV 坐标一致，无需翻转）
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height;  // 负值 = top-down DIB
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    BYTE* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hMemDC, &bmi, DIB_RGB_COLORS,
                                    reinterpret_cast<void**>(&pBits), NULL, 0);
    if (!hBmp || !pBits) {
        if (hBmp) DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        return cv::Mat();
    }

    // BitBlt 拷贝屏幕像素到 DIBSection
    HBITMAP hOldBmp = static_cast<HBITMAP>(SelectObject(hMemDC, hBmp));
    BitBlt(hMemDC, 0, 0, width, height, hScreenDC, x1, y1, SRCCOPY);

    // 将 BGRA 像素包装为 cv::Mat 视图，转换到 BGR 并深拷贝
    cv::Mat bgra(height, width, CV_8UC4, pBits);
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    cv::Mat result = bgr.clone();  // clone() 必须：pBits 即将随 GDI 释放

    // 清理 GDI 资源
    SelectObject(hMemDC, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);

    return result;
}

/// 从文件加载图像
/// @param ImgPath  图像文件路径（同时用作日志标签）
/// @return LabeledImage；加载失败时 Mat 为空
LabeledImage GetImg(const std::string& ImgPath) {
    cv::Mat mat = cv::imread(ImgPath, cv::IMREAD_COLOR);
    if (mat.empty()) {
        Log("加载图像失败: " + ImgPath);
    }
    return {mat, ImgPath};
}

/// 在截图中查找模板图像位置
/// @param Needle    模板图像 (BGR)
/// @param Haystack  被搜索的截图 (BGR)
/// @return 匹配结果；未找到或入参无效时返回 nullopt
std::optional<FoundImg> ImgPosition(const cv::Mat& Needle, const cv::Mat& Haystack) {
    // 入参有效性检查
    if (Needle.empty() || Haystack.empty()) return std::nullopt;

    // 模板不能大于搜索区域
    if (Needle.cols > Haystack.cols || Needle.rows > Haystack.rows)
        return std::nullopt;

    // 模板匹配：归一化相关系数法，对亮度变化不敏感
    cv::Mat result;
    cv::matchTemplate(Haystack, Needle, result, cv::TM_CCOEFF_NORMED);

    // 查找最佳匹配位置
    double minVal = 0.0, maxVal = 0.0;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

    // 置信度达标则返回匹配结果
    if (maxVal >= kMatchThreshold) {
        return FoundImg{maxLoc.x, maxLoc.y, Needle.cols, Needle.rows, maxVal};
    }
    return std::nullopt;
}

/// 在截图中查找模板图像，输出匹配日志
/// @param Img       带标签的模板图像（Label 用于日志）
/// @param Haystack  被搜索的截图
/// @return 匹配结果；未找到返回 nullopt
std::optional<FoundImg> FindTemplate(
    const LabeledImage& Img,
    const cv::Mat& Haystack
) {
    auto Result = ImgPosition(Img.Mat, Haystack);
    if (Result.has_value()) {
        Log("✓ " + Img.Label + " 置信度=" + std::to_string(Result->Confidence)
            + " (" + std::to_string(Result->FoundAtX) + ","
            + std::to_string(Result->FoundAtY) + ")");
    }
    return Result;
}

/// 在屏幕指定比例区域内截图并查找模板
/// @param Img       带标签的模板图像
/// @param ScreenW/H 屏幕宽高
/// @param X1..Y2    搜索区域（屏幕比例，0.0~1.0）
/// @return 匹配结果；未找到返回 nullopt
std::optional<FoundImg> FindTemplateInScreenRatio(
    const LabeledImage& Img,
    const int ScreenW,
    const int ScreenH,
    const double X1, const double Y1,
    const double X2, const double Y2
) {
    const cv::Mat Haystack = GetScreenArea(ScreenW, ScreenH, X1, Y1, X2, Y2);
    return FindTemplate(Img, Haystack);
}

int main() {
    // 声明 DPI 感知：禁用 Windows 的坐标虚拟化，
    // 使 GetSystemMetrics / GDI 截图返回物理像素而非逻辑坐标
    SetProcessDPIAware();

    // 设置控制台编码为 UTF-8，确保中文正常显示
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 检查 Interception 驱动是否可用
    if (!g_Interception.Ok()) {
        Log("Interception 驱动未就绪！请执行以下步骤：");
        Log("  1. 管理员身份运行 interception\\install-interception.exe /install");
        Log("  2. 重启电脑");
        Log("  3. 重新运行本程序（可能需要管理员权限）");
        Log("按 Enter 退出...");
        std::cin.get();
        return 1;
    }
    Log("Interception 驱动已就绪");

    // 预加载所有模板图像
    const LabeledImage ReadyToFishImg  = GetImg("img/ready_to_fish.png");
    const LabeledImage FishCaughtImg   = GetImg("img/fish_caught.png");
    const LabeledImage ClickToCloseImg = GetImg("img/click_to_close.png");
    const LabeledImage GreenRectLeftImg  = GetImg("img/green_rect_left.png");
    const LabeledImage GreenRectRightImg = GetImg("img/green_rect_right.png");
    const LabeledImage GoldCursorImg     = GetImg("img/gold_cursor.png");

    // 获取屏幕信息
    const int InScreenWidth  = GetScreenWidth();
    const int InScreenHeight = GetScreenHeight();

    Log("屏幕尺寸: " + std::to_string(InScreenWidth) + "x"
        + std::to_string(InScreenHeight));
    Log("异环钓鱼自动化工具已启动");
    Log("五秒后开始尝试钓鱼，请聚焦游戏窗口");
    WaitFor(5.0);

    Follower follower;  // 自适应指针跟随器

    while (true) {
        // ----- 阶段 1：检测右下角钓鱼按钮 -----
        while (!FindTemplateInScreenRatio(ReadyToFishImg,
                                           InScreenWidth, InScreenHeight,
                                           0.75, 0.75, 1.0, 1.0)) {
            WaitFor(1.0);
            Log("未检测到开始钓鱼按钮，等待中...");
        }

        // ----- 阶段 2：抛竿 + 等待鱼上钩 -----
        PressFor('F', 0.05);       // 开始钓鱼（抛竿）

        // 持续检测 fish_caught.png 判断鱼是否上钩
        // 搜索区域: 屏幕 (37.5%-62.5% 宽, 17.5%-25% 高)
        // 最长等待 7.5 秒，超时后强制提竿
        {
            constexpr double kFishBiteTimeout = 7.5;   // 最长等待时间（秒）
            constexpr double kCheckInterval   = 0.3;   // 检测间隔（秒）
            const auto StartTime = std::chrono::steady_clock::now();
            bool bFishHooked = false;

            while (true) {
                const double Elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - StartTime).count();
                if (Elapsed >= kFishBiteTimeout) break;

                if (FindTemplateInScreenRatio(FishCaughtImg,
                                               InScreenWidth, InScreenHeight,
                                               0.375, 0.175, 0.625, 0.25)) {
                    bFishHooked = true;
                    Log("鱼已上钩！（耗时 " + std::to_string(Elapsed).substr(0, 4) + " 秒）");
                    break;
                }
                WaitFor(kCheckInterval);
            }

            if (!bFishHooked) {
                Log("等待鱼上钩超时（7.5 秒），强制提竿");
            }
        }

        PressFor('F', 0.05);       // 提竿（开始上鱼）
        WaitFor(0.5);   // 等待上鱼 UI 出现

        // ----- 阶段 3：上鱼过程 —— 指针跟随绿色矩形 -----
        // 上鱼过程中屏幕顶部有一条左右移动的绿色圆角矩形（进度条），
        // 以及一个金色窄矩形指针。
        // green_rect_left.png 是绿色矩形左边缘，
        // green_rect_right.png 是绿色矩形右边缘。
        // 两侧都检测到时跟随完整矩形中心；仅一侧时降级跟随该侧边缘。

        constexpr int kMaxAttempts = 5;  // 重试次数
        int Retry = 0;

        while (true) {
            // 同一帧截图：屏幕顶部 30%-70% 宽、4%-10% 高
            const cv::Mat Haystack = GetScreenArea(InScreenWidth, InScreenHeight,
                                                    0.30, 0.04, 0.70, 0.10);

            // 同一帧截图中匹配三个模板
            const std::optional<FoundImg> GreenRectLeft = FindTemplate(GreenRectLeftImg, Haystack);
            const std::optional<FoundImg> GreenRectRight = FindTemplate(GreenRectRightImg, Haystack);
            const std::optional<FoundImg> Cursor = FindTemplate(GoldCursorImg, Haystack);

            // 提前判断两侧绿色矩形是否找到
            const bool bLeft  = GreenRectLeft.has_value();
            const bool bRight = GreenRectRight.has_value();

            // 金色指针缺失但绿色矩形存在 → 瞬态识别失败，重试
            // 如果绿色矩形也全部缺失，则落到底部 else 分支检查鱼是否已上钩
            if (!Cursor.has_value()) {
                if (bLeft || bRight) {
                    Log("未检测到金色指针，重试...");
                    continue;
                }
            }

            // 金色指针存在时执行跟随
            if (Cursor.has_value()) {
                // 金色指针中心 X（相对于截图区域）
                const int CursorCenterX =
                    Cursor->FoundAtX + Cursor->TemplateWidth / 2;

                if (bLeft && bRight) {
                    // 两侧都找到：跟随完整绿色矩形中心
                    const int CenterX =
                        (GreenRectLeft->FoundAtX
                         + GreenRectRight->FoundAtX
                         + GreenRectRight->TemplateWidth) / 2;
                    FollowAction Act = follower.Follow(CenterX, CursorCenterX);
                    if (Act.Direction != '\0') PressFor(Act.Direction, Act.Duration);
                    continue;

                } else if (bLeft) {
                    // 仅找到左侧：跟随左侧的右边缘
                    Log("未检测到 green_rect_right，仅跟随左侧右边缘");
                    const int LeftRightEdgeX =
                        GreenRectLeft->FoundAtX + GreenRectLeft->TemplateWidth;
                    FollowAction Act = follower.Follow(LeftRightEdgeX, CursorCenterX);
                    if (Act.Direction != '\0') PressFor(Act.Direction, Act.Duration);
                    continue;

                } else if (bRight) {
                    // 仅找到右侧：跟随右侧的左边缘
                    Log("未检测到 green_rect_left，仅跟随右侧左边缘");
                    FollowAction Act = follower.Follow(GreenRectRight->FoundAtX, CursorCenterX);
                    if (Act.Direction != '\0') PressFor(Act.Direction, Act.Duration);
                    continue;
                }
            }

            // 到这里的情况：
            // 1. 金色指针 + 两侧绿色矩形全部缺失（鱼可能已上钩）
            // 2. 金色指针存在但两侧绿色矩形都缺失
            {
                // 可能鱼已上钩，或脱钩/识别失败
                bool bFishCaught = false;
                WaitFor(2.5);

                // 检测"点击关闭"按钮（鱼已成功捕获）
                while (FindTemplateInScreenRatio(ClickToCloseImg,
                                                  InScreenWidth, InScreenHeight,
                                                  0.4, 0.875, 0.6, 0.95)) {
                    bFishCaught = true;
                    WaitFor(1.0);
                    Click();  // 点击关闭结果展示界面
                }

                if (bFishCaught) {
                    Log("成功捕获一条鱼！");
                    break;  // 回到外层循环，等待下一次钓鱼
                }

                // 鱼脱钩或识别失败
                if (Retry >= kMaxAttempts) {
                    Log("重试次数已耗尽，放弃本次钓鱼");
                    break;  // 回到外层循环，等待下一次钓鱼
                }

                ++Retry;
                Log("未检测到金色指针或绿色矩形左右边缘，重试中"
                    + std::to_string(Retry) + " / "
                    + std::to_string(kMaxAttempts) + "...");
                WaitFor(1.0);
                continue;
            }
        }
    }
}
