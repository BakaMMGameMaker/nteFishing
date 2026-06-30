// Interception 驱动封装（内核 HID 层注入，绕过 UE5 RawInput）
//
// 包含：
//   - InterceptionDriver 类（动态加载 interception.dll）
//   - 全局驱动实例声明
//
// Interception 原始类型定义（enum/struct/函数指针）封装在匿名 namespace 中，
// 仅服务于 InterceptionDriver 内部实现，外界无需知晓。

#pragma once

#include <windows.h>

// ============================================================
// 匿名 namespace：Interception 驱动内部类型（匹配 interception.h）
// ============================================================

namespace {

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

/// Interception DLL 函数指针类型
typedef void* (*PfnInterceptionCreateContext)();
typedef void  (*PfnInterceptionDestroyContext)(void*);
typedef int   (*PfnInterceptionSend)(void*, int, const void*, unsigned int);

} // anonymous namespace

// ============================================================
// InterceptionDriver 类
// ============================================================

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
