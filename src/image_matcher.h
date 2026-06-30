// 图像匹配器：封装屏幕截图、OpenCV 模板匹配与相关工具函数
//
// 构造时捕获屏幕尺寸，之后 FindTemplateInScreenRatio 无需再传递屏幕宽高。
// 静态工具方法（Log / GetImg / FindTemplate）可直接通过类名调用。

#pragma once

// 在任何 <windows.h> 之前定义，防止与 OpenCV 的 min/max 冲突，减少不必要头文件的引入
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <iostream>
#include <optional>
#include <string>

#include <opencv2/opencv.hpp>

/// 模板匹配结果
struct FoundImg {
    int    FoundAtX       = 0;    // 匹配位置 X 坐标（左上角，相对于截图区域）
    int    FoundAtY       = 0;    // 匹配位置 Y 坐标
    int    TemplateWidth  = 0;    // 模板图像宽度
    int    TemplateHeight = 0;    // 模板图像高度
    double Confidence     = 0.0;  // 匹配置信度（TM_CCOEFF_NORMED 相关系数）
};

/// 带标签的图像，绑定图像数据与日志标签
struct LabeledImage {
    cv::Mat     Mat;    // 图像数据
    std::string Label;  // 模板名称标签（用于日志）
};

/// 图像匹配器：封装屏幕截图、OpenCV 模板匹配与相关工具函数
class ImageMatcher {
public:
    /// 构造时捕获屏幕尺寸
    /// @param ScreenWidth   屏幕宽度（像素）
    /// @param ScreenHeight  屏幕高度（像素）
    ImageMatcher(int ScreenWidth, int ScreenHeight)
        : m_ScreenW(ScreenWidth), m_ScreenH(ScreenHeight) {}

    int ScreenWidth()  const { return m_ScreenW; }
    int ScreenHeight() const { return m_ScreenH; }

    // ============================================================
    // 静态工具函数
    // ============================================================

    /// 输出日志信息
    static void Log(const std::string& Message) {
        std::cout << Message << '\n';
    }

    /// 从文件加载图像（BGR 彩色）
    /// @param ImgPath  图像文件路径（同时用作日志标签）
    /// @return LabeledImage；加载失败时 Mat 为空
    static LabeledImage GetImg(const std::string& ImgPath) {
        cv::Mat mat = cv::imread(ImgPath, cv::IMREAD_COLOR);
        if (mat.empty()) {
            Log("加载图像失败: " + ImgPath);
        }
        return {mat, ImgPath};
    }

    /// 在截图中查找模板图像，输出匹配日志
    /// @param Img       带标签的模板图像（Label 用于日志）
    /// @param Haystack  被搜索的截图
    /// @return 匹配结果；未找到返回 nullopt
    static std::optional<FoundImg> FindTemplate(
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

    // ============================================================
    // 实例方法（依赖屏幕尺寸）
    // ============================================================

    /// 截取屏幕指定比例区域，返回 BGR 格式图像
    /// @param X1, Y1  左上角比例坐标（0.0~1.0，含）
    /// @param X2, Y2  右下角比例坐标（0.0~1.0，不含）
    /// @return BGR 3 通道 cv::Mat；失败或无效区域时返回空 Mat
    cv::Mat GetScreenArea(
        double X1, double Y1,
        double X2, double Y2
    ) const {
        // 比例坐标 → 像素坐标
        int x1 = static_cast<int>(X1 * m_ScreenW);
        int y1 = static_cast<int>(Y1 * m_ScreenH);
        int x2 = static_cast<int>(X2 * m_ScreenW);
        int y2 = static_cast<int>(Y2 * m_ScreenH);

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

    /// 在屏幕指定比例区域内截图并查找模板
    /// @param Img       带标签的模板图像
    /// @param X1..Y2    搜索区域（屏幕比例，0.0~1.0）
    /// @return 匹配结果；未找到返回 nullopt
    std::optional<FoundImg> FindTemplateInScreenRatio(
        const LabeledImage& Img,
        double X1, double Y1,
        double X2, double Y2
    ) const {
        const cv::Mat Haystack = GetScreenArea(X1, Y1, X2, Y2);
        return FindTemplate(Img, Haystack);
    }

private:
    /// 模板匹配核心算法：归一化相关系数法
    /// @param Needle    模板图像 (BGR)
    /// @param Haystack  被搜索的截图 (BGR)
    /// @return 匹配结果；未找到或入参无效时返回 nullopt
    static std::optional<FoundImg> ImgPosition(
        const cv::Mat& Needle,
        const cv::Mat& Haystack
    ) {
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

    int m_ScreenW;  // 屏幕宽度（像素）
    int m_ScreenH;  // 屏幕高度（像素）

    /// 模板匹配置信度阈值（TM_CCOEFF_NORMED 相关系数）
    static constexpr double kMatchThreshold = 0.80;
};
