// 通用工具：日志输出、图像加载
//
// 包含 LabeledImage 结构体与命名空间 NTEAutoFishing 下的工具函数。

#pragma once

#include <iostream>
#include <string>

#include <opencv2/opencv.hpp>

/// 带标签的图像，绑定图像数据与日志标签
struct LabeledImage {
    cv::Mat     Mat;    // 图像数据
    std::string Label;  // 模板名称标签（用于日志）
};

namespace NTEAutoFishing {

/// 输出日志信息
inline void Log(const std::string& Message) {
    std::cout << Message << '\n';
}

/// 从文件加载图像（BGR 彩色）
/// @param ImgPath  图像文件路径（同时用作日志标签）
/// @return LabeledImage；加载失败时 Mat 为空
inline LabeledImage GetImg(const std::string& ImgPath) {
    cv::Mat mat = cv::imread(ImgPath, cv::IMREAD_COLOR);
    if (mat.empty()) {
        Log("加载图像失败: " + ImgPath);
    }
    return {mat, ImgPath};
}

} // namespace NTEAutoFishing
