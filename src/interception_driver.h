// Interception 驱动封装（内核 HID 层注入，绕过 UE5 RawInput）
//
// 包含：
//   - Interception 输入事件类型定义
//   - InterceptionDriver 类（动态加载 interception.dll）
//   - 全局驱动实例声明
//   - 高层输入模拟函数（Click / PressFor / WaitFor）

#pragma once

#include <windows.h>

#include <chrono>
#include <thread>
#include <unordered_map>

// ============================================================
// Interception 驱动类型定义（匹配 interception.h）
// ============================================================

/// 按键状态
enum InterceptionKeyState {
    INTERCEPTION_KEY_DOWN = 0x00,  // 按下
    INTERCEPTION_KEY_UP   = 0x01,  // 释放
};

/// 鼠标状态
enum InterceptionMouseState {
    INTERCEPTION_MOUSE_LEFT_BUTTON_DOWN = 0x001,  // 左键按下
    INTERCEPTION_MOUSE_LEFT_BUTTON_UP   = 0x002,  // 左键释放
};

/// 按键 Stroke 结构（对应 interception.h 的 InterceptionKeyStroke）
struct InterceptionKeyStroke {
    unsigned short code;        // 硬件扫描码（PS/2 Set 1）
    unsigned short state;       // InterceptionKeyState
    unsigned int   information; // 保留字段
};

/// 鼠标 Stroke 结构（对应 interception.h 的 InterceptionMouseStroke）
struct InterceptionMouseStroke {
    unsigned short state;       // InterceptionMouseState
    unsigned short flags;       // 移动标志（当前未使用）
    short          rolling;     // 滚轮增量（当前未使用）
    int            x;           // X 坐标（当前未使用）
    int            y;           // Y 坐标（当前未使用）
    unsigned int   information; // 保留字段
};

/// Interception 设备 ID（匹配 interception.h）
constexpr int INTERCEPTION_KEYBOARD_ID = 1;   // INTERCEPTION_KEYBOARD(0)
constexpr int INTERCEPTION_MOUSE_ID    = 11;  // INTERCEPTION_MOUSE(0)

// ============================================================
// InterceptionDriver 类
// ============================================================

// Interception DLL 函数指针类型
typedef void* (*PfnInterceptionCreateContext)();
typedef void  (*PfnInterceptionDestroyContext)(void*);
typedef int   (*PfnInterceptionSend)(void*, int, const void*, unsigned int);

/// 动态加载 interception.dll，封装内核级 HID 输入注入
class InterceptionDriver {
public:
    InterceptionDriver() {
        m_hDll = LoadLibraryA("interception.dll");
        if (!m_hDll) return;

        auto createFn = (PfnInterceptionCreateContext)GetProcAddress(
            m_hDll, "interception_create_context");
        m_sendFn = (PfnInterceptionSend)GetProcAddress(
            m_hDll, "interception_send");
        m_destroyFn = (PfnInterceptionDestroyContext)GetProcAddress(
            m_hDll, "interception_destroy_context");

        if (!createFn || !m_sendFn) {
            FreeLibrary(m_hDll);
            m_hDll = NULL;
            return;
        }

        m_ctx = createFn();
        if (!m_ctx) {
            FreeLibrary(m_hDll);
            m_hDll = NULL;
            return;
        }

        m_available = true;
    }

    ~InterceptionDriver() {
        if (m_ctx && m_destroyFn) m_destroyFn(m_ctx);
        if (m_hDll) FreeLibrary(m_hDll);
    }

    // 禁止拷贝（管理 DLL 句柄和驱动上下文）
    InterceptionDriver(const InterceptionDriver&) = delete;
    InterceptionDriver& operator=(const InterceptionDriver&) = delete;

    /// 驱动是否成功初始化
    bool Ok() const { return m_available; }

    /// 发送键盘事件（内核 HID 层）
    /// @param scanCode  PS/2 硬件扫描码
    /// @param down      true=按下, false=释放
    void SendKey(unsigned short scanCode, bool down) const {
        InterceptionKeyStroke s = {
            scanCode,
            static_cast<unsigned short>(down ? INTERCEPTION_KEY_DOWN
                                             : INTERCEPTION_KEY_UP),
            0
        };
        m_sendFn(m_ctx, INTERCEPTION_KEYBOARD_ID, &s, 1);
    }

    /// 发送鼠标左键事件（内核 HID 层）
    /// @param down  true=按下, false=释放
    void SendLeftClick(bool down) const {
        InterceptionMouseStroke s = {
            static_cast<unsigned short>(
                down ? INTERCEPTION_MOUSE_LEFT_BUTTON_DOWN
                     : INTERCEPTION_MOUSE_LEFT_BUTTON_UP),
            0, 0, 0, 0, 0
        };
        m_sendFn(m_ctx, INTERCEPTION_MOUSE_ID, &s, 1);
    }

private:
    HMODULE m_hDll               = NULL;
    void*   m_ctx                = nullptr;
    bool    m_available          = false;
    PfnInterceptionSend          m_sendFn    = nullptr;
    PfnInterceptionDestroyContext m_destroyFn = nullptr;
};

// ============================================================
// 全局驱动实例（声明，定义在 main.cpp）
// ============================================================

extern InterceptionDriver g_Interception;

// ============================================================
// 高层输入模拟函数（内联，依赖全局 Interception 实例）
// ============================================================

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
