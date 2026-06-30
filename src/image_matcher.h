// 图像匹配器：封装屏幕截图与 OpenCV 模板匹配
//
// 构造时捕获屏幕尺寸并预加载模板图像到缓存，
// FindTemplatesInScreenRatio 一次截图批量匹配多张模板。

#pragma once

// 在任何 <windows.h> 之前定义，防止与 OpenCV 的 min/max 冲突，减少不必要头文件的引入
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <algorithm>
#include <filesystem>

#include <opencv2/opencv.hpp>

#include "utils.h"

namespace {

/// 递归扫描图像目录，收集指定扩展名的文件
/// @param ImgDir            图像目录路径
/// @param IncludeExtension  要收集的文件扩展名（如 ".png"）
/// @return 相对于 ImgDir 的文件路径列表（正斜杠）
std::vector<std::string> ScanImageDir(
    const std::string& ImgDir,
    const std::string& IncludeExtension
) {
    namespace fs = std::filesystem;
    std::vector<std::string> Paths;

    // 规范化扩展名：确保以 . 开头并转为小写
    std::string Ext = IncludeExtension;
    if (!Ext.empty() && Ext[0] != '.') Ext = '.' + Ext;
    std::transform(Ext.begin(), Ext.end(), Ext.begin(),
        [](unsigned char c) { return std::tolower(c); });

    fs::path DirPath(ImgDir);
    std::error_code ec;
    if (!fs::exists(DirPath, ec) || !fs::is_directory(DirPath, ec)) {
        NTEAutoFishing::Log("⚠ 图像目录不存在或不是目录: " + ImgDir);
        return Paths;
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(DirPath)) {
            if (!entry.is_regular_file()) continue;

            std::string FileExt = entry.path().extension().string();
            std::transform(FileExt.begin(), FileExt.end(), FileExt.begin(),
                [](unsigned char c) { return std::tolower(c); });

            if (FileExt == Ext) {
                Paths.push_back(
                    fs::relative(entry.path(), DirPath).generic_string());
            }
        }
    } catch (const std::exception& e) {
        NTEAutoFishing::Log(std::string("⚠ 扫描图像目录异常: ") + e.what());
    }

    return Paths;
}

} // namespace

/// 模板匹配结果
struct FoundImg {
    int    FoundAtX       = 0;    // 匹配位置 X 坐标（左上角，相对于截图区域）
    int    FoundAtY       = 0;    // 匹配位置 Y 坐标
    int    TemplateWidth  = 0;    // 模板图像宽度
    int    TemplateHeight = 0;    // 模板图像高度
    double Confidence     = 0.0;  // 匹配置信度（TM_CCOEFF_NORMED 相关系数）
};

/// 图像匹配器：封装屏幕截图与 OpenCV 模板匹配
class ImageMatcher {
public:
    /// 构造时捕获屏幕尺寸并预加载模板图像
    /// @param ScreenWidth   屏幕宽度（像素）
    /// @param ScreenHeight  屏幕高度（像素）
    /// @param ImgDir        图像资源目录路径，将递归扫描该目录下所有指定扩展名的图像
    ImageMatcher(int ScreenWidth, int ScreenHeight,
                 const std::string& ImgDir = NTEAutoFishing::GetImageDir())
        : m_ScreenW(ScreenWidth), m_ScreenH(ScreenHeight) {
        namespace fs = std::filesystem;
        for (const auto& RelPath : ScanImageDir(ImgDir, ".png")) {
            std::string FullPath = (fs::path(ImgDir) / RelPath).string();
            m_ImageCache.emplace(RelPath, NTEAutoFishing::GetImg(FullPath));
        }
    }

    int ScreenWidth()  const { return m_ScreenW; }
    int ScreenHeight() const { return m_ScreenH; }

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

    /// 在屏幕指定比例区域内截图，批量匹配多张模板
    /// @param TemplatePaths  模板图像路径列表（须已在构造时预加载）
    /// @param X1..Y2         搜索区域（屏幕比例，0.0~1.0）
    /// @return 路径 → 匹配结果映射；未预加载的路径对应 nullopt
    std::unordered_map<std::string, std::optional<FoundImg>> FindTemplatesInScreenRatio(
        const std::vector<std::string>& TemplatePaths,
        double X1, double Y1,
        double X2, double Y2
    ) const {
        // 一次截图，供所有模板共用
        const cv::Mat Haystack = GetScreenArea(X1, Y1, X2, Y2);

        std::unordered_map<std::string, std::optional<FoundImg>> Results;
        for (const auto& Path : TemplatePaths) {
            auto It = m_ImageCache.find(Path);
            if (It == m_ImageCache.end()) {
                NTEAutoFishing::Log("⚠ 模板未预加载: " + Path);
                Results[Path] = std::nullopt;
                continue;
            }

            auto Result = ImgPosition(It->second.Mat, Haystack);
            if (Result.has_value()) {
                NTEAutoFishing::Log("✓ " + It->second.Path
                    + " 置信度=" + std::to_string(Result->Confidence)
                    + " (" + std::to_string(Result->FoundAtX) + ","
                    + std::to_string(Result->FoundAtY) + ")");
            }
            Results[Path] = Result;
        }
        return Results;
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

    /// 模板图像缓存：文件路径 → 预加载的带标签图像
    std::unordered_map<std::string, NTEAutoFishing::Image> m_ImageCache;

    /// 模板匹配置信度阈值（TM_CCOEFF_NORMED 相关系数）
    static constexpr double kMatchThreshold = 0.80;
};
