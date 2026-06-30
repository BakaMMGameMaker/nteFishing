// 通用工具：日志输出、图像加载
//
// 包含 Image 结构体与命名空间 NTEAutoFishing 下的工具函数。

#pragma once

#include <iostream>
#include <string>

#include <opencv2/opencv.hpp>

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

} // namespace NTEAutoFishing
