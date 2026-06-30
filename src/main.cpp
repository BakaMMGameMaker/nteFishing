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
#include "image_matcher.h"

#include <chrono>
#include <optional>
#include <string>

// 全局 Interception 实例
InterceptionDriver g_Interception;

int main() {
    // 声明 DPI 感知：禁用 Windows 的坐标虚拟化，
    // 使 GetSystemMetrics / GDI 截图返回物理像素而非逻辑坐标
    SetProcessDPIAware();

    // 设置控制台编码为 UTF-8，确保中文正常显示
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 检查 Interception 驱动是否可用
    if (!g_Interception.Ok()) {
        NTEAutoFishing::Log("Interception 驱动未就绪！按 Enter 退出...");
        std::cin.get();
        return 1;
    }
    NTEAutoFishing::Log("Interception 驱动已就绪");

    // 构造图像匹配器（捕获屏幕尺寸 + 扫描 img 目录预加载所有模板图像）
    ImageMatcher matcher(
        NTEAutoFishing::GetScreenWidth(), NTEAutoFishing::GetScreenHeight(),
        NTEAutoFishing::GetImageDir()
    );

    NTEAutoFishing::Log("屏幕尺寸: " + std::to_string(matcher.ScreenWidth()) + "x"
        + std::to_string(matcher.ScreenHeight()));
    NTEAutoFishing::Log("异环钓鱼自动化工具已启动");
    NTEAutoFishing::Log("五秒后开始尝试钓鱼，请聚焦游戏窗口");
    NTEAutoFishing::WaitFor(5.0);

    Follower follower;  // 自适应指针跟随器

    while (true) {
        // ----- 阶段 1：检测右下角钓鱼按钮 -----
        while (!matcher.FindTemplatesInScreenRatio(
                   {"ready_to_fish.png"}, 0.75, 0.75, 1.0, 1.0
               )["ready_to_fish.png"]) {
            NTEAutoFishing::WaitFor(1.0);
            NTEAutoFishing::Log("未检测到开始钓鱼按钮，等待中...");
        }

        // ----- 阶段 2：抛竿 + 等待鱼上钩 -----
        NTEAutoFishing::PressFor('F', 0.05);       // 开始钓鱼（抛竿）

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

                if (matcher.FindTemplatesInScreenRatio(
                        {"fish_caught.png"}, 0.375, 0.175, 0.625, 0.25
                    )["fish_caught.png"]) {
                    bFishHooked = true;
                    NTEAutoFishing::Log("鱼已上钩！");
                    break;
                }
                NTEAutoFishing::WaitFor(kCheckInterval);
            }

            if (!bFishHooked) {
                NTEAutoFishing::Log("等待鱼上钩超时，强制提竿");
            }
        }

        NTEAutoFishing::PressFor('F', 0.05);       // 提竿（开始上鱼）
        NTEAutoFishing::WaitFor(0.5);   // 等待上鱼 UI 出现

        // ----- 阶段 3：上鱼过程 —— 指针跟随绿色矩形 -----
        // 上鱼过程中屏幕顶部有一条左右移动的绿色圆角矩形（进度条），
        // 以及一个金色窄矩形指针。
        // green_rect_left.png 是绿色矩形左边缘，
        // green_rect_right.png 是绿色矩形右边缘。
        // 两侧都检测到时跟随完整矩形中心；仅一侧时降级跟随该侧边缘。

        constexpr int kMaxAttempts = 5;  // 重试次数
        int Retry = 0;

        while (true) {
            // 同一帧截图并批量匹配三个模板：屏幕顶部 30%-70% 宽、4%-10% 高
            auto Results = matcher.FindTemplatesInScreenRatio(
                {"green_rect_left.png", "green_rect_right.png", "gold_cursor.png"},
                0.30, 0.04, 0.70, 0.10
            );

            const auto& GreenRectLeft  = Results["green_rect_left.png"];
            const auto& GreenRectRight = Results["green_rect_right.png"];
            const auto& Cursor         = Results["gold_cursor.png"];

            // 提前判断两侧绿色矩形是否找到
            const bool bLeft  = GreenRectLeft.has_value();
            const bool bRight = GreenRectRight.has_value();

            // 金色指针缺失但绿色矩形存在 → 瞬态识别失败，重试
            // 如果绿色矩形也全部缺失，则落到底部 else 分支检查鱼是否已上钩
            if (!Cursor.has_value()) {
                if (bLeft || bRight) {
                    NTEAutoFishing::Log("未检测到金色指针，重试...");
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
                    if (Act.Direction != '\0') NTEAutoFishing::PressFor(Act.Direction, Act.Duration);
                    continue;

                } else if (bLeft) {
                    // 仅找到左侧：跟随左侧的右边缘
                    NTEAutoFishing::Log("未检测到 green_rect_right，仅跟随左侧右边缘");
                    const int LeftRightEdgeX =
                        GreenRectLeft->FoundAtX + GreenRectLeft->TemplateWidth;
                    FollowAction Act = follower.Follow(LeftRightEdgeX, CursorCenterX);
                    if (Act.Direction != '\0') NTEAutoFishing::PressFor(Act.Direction, Act.Duration);
                    continue;

                } else if (bRight) {
                    // 仅找到右侧：跟随右侧的左边缘
                    NTEAutoFishing::Log("未检测到 green_rect_left，仅跟随右侧左边缘");
                    FollowAction Act = follower.Follow(GreenRectRight->FoundAtX, CursorCenterX);
                    if (Act.Direction != '\0') NTEAutoFishing::PressFor(Act.Direction, Act.Duration);
                    continue;
                }
            }

            // 到这里的情况：
            // 1. 金色指针 + 两侧绿色矩形全部缺失（鱼可能已上钩）
            // 2. 金色指针存在但两侧绿色矩形都缺失
            {
                // 可能鱼已上钩，或脱钩/识别失败
                bool bFishCaught = false;
                NTEAutoFishing::WaitFor(2.5);

                // 检测"点击关闭"按钮（鱼已成功捕获）
                while (matcher.FindTemplatesInScreenRatio(
                           {"click_to_close.png"}, 0.4, 0.875, 0.6, 0.95
                       )["click_to_close.png"]) {
                    bFishCaught = true;
                    NTEAutoFishing::WaitFor(1.0);
                    NTEAutoFishing::Click();  // 点击关闭结果展示界面
                }

                if (bFishCaught) {
                    NTEAutoFishing::Log("成功捕获一条鱼！");
                    break;  // 回到外层循环，等待下一次钓鱼
                }

                // 鱼脱钩或识别失败
                if (Retry >= kMaxAttempts) {
                    NTEAutoFishing::Log("重试次数已耗尽，放弃本次钓鱼");
                    break;  // 回到外层循环，等待下一次钓鱼
                }

                ++Retry;
                NTEAutoFishing::Log("未检测到金色指针或绿色矩形左右边缘，重试中"
                    + std::to_string(Retry) + " / "
                    + std::to_string(kMaxAttempts) + "...");
                NTEAutoFishing::WaitFor(1.0);
                continue;
            }
        }
    }
}
