// 通用工具：日志输出、图像加载、屏幕尺寸获取、高层输入模拟
//
// 包含 Image 结构体与命名空间 NTEAutoFishing 下的工具函数。

#pragma once

// 在任何 <windows.h> 之前定义，防止与 OpenCV 的 min/max 冲突，减少不必要头文件的引入
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>

#include <opencv2/opencv.hpp>

#include "interception_driver.h"

namespace NTEAutoFishing {

/// 带路径的图像，绑定图像数据与文件路径
struct Image {
    cv::Mat     Mat;   // 图像数据
    std::string Path;  // 图像文件路径（用于日志）
};

/// 输出日志信息
inline void Log(const std::string& Message) {
    std::cout << Message << '\n';
}

/// 从文件加载图像（BGR 彩色）
/// @param ImgPath  图像文件路径（同时用作日志标签）
/// @return Image；加载失败时 Mat 为空
inline Image GetImg(const std::string& ImgPath) {
    cv::Mat mat = cv::imread(ImgPath, cv::IMREAD_COLOR);
    if (mat.empty()) {
        Log("加载图像失败: " + ImgPath);
    }
    return {mat, ImgPath};
}

/// 获取屏幕宽度（物理像素），考虑 DPI 感知设置
inline int GetScreenWidth() {
    return GetSystemMetrics(SM_CXSCREEN);
}

/// 获取屏幕高度（物理像素），考虑 DPI 感知设置
inline int GetScreenHeight() {
    return GetSystemMetrics(SM_CYSCREEN);
}

/// 模拟鼠标左键点击（当前光标位置，按下→50ms→释放）
inline void Click() {
    g_Interception.SendLeftClick(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    g_Interception.SendLeftClick(false);
}

/// 等待指定时长（秒）
inline void WaitFor(const double Time) {
    const int ms = static_cast<int>(Time * 1000.0);
    if (ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
}

/// 模拟按键：按下 Key 键，保持 Time 秒后释放
/// @param Key   按键字符（映射表: 'F'→0x21, 'A'→0x1E, 'D'→0x20）
/// @param Time  按键保持时长（秒）
inline void PressFor(const char Key, const double Time) {
    static const std::unordered_map<char, unsigned short> kScanCodeMap = {
        {'F', 0x21},  // F 键
        {'A', 0x1E},  // A 键
        {'D', 0x20},  // D 键
    };

    auto it = kScanCodeMap.find(Key);
    if (it == kScanCodeMap.end()) return;  // 未知按键，静默忽略

    g_Interception.SendKey(it->second, true);
    WaitFor(Time);
    g_Interception.SendKey(it->second, false);
}

} // namespace NTEAutoFishing
