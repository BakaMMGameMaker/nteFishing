// 自适应指针跟随器
//
// 首次按键使用固定时长校准速度；
// 后续根据 距离/速度 动态计算按键时长，
// 并通过多样本运行平均持续优化速度估计

#pragma once

#include <cmath>

/// 一次跟随动作：方向 + 按键时长
struct FollowAction {
    char   Direction = '\0';  // 'A'=左移, 'D'=右移, '\0'=死区内无需操作
    double Duration  = 0.0;   // 按键时长（秒）
};

/// 自适应指针跟随器
/// 封装速度校准、按键时长计算、运行平均等状态与逻辑，
/// 只负责计算，不执行输入（PressFor 由调用方调用）
class Follower {
public:
    /// 计算跟随动作：根据目标位置与当前指针位置的差距，返回方向与按键时长
    /// @param TargetX   目标 X 坐标（相对截图区域）
    /// @param CurrentX  当前 X 坐标（金色指针中心，相对截图区域）
    /// @return 跟随动作；死区内返回 Direction='\0'
    FollowAction Follow(int TargetX, int CurrentX) {
        // 差距小于死区，不操作
        if (std::abs(TargetX - CurrentX) < kDeadZone) return {};

        FollowAction Action;
        Action.Direction = (TargetX < CurrentX) ? 'A' : 'D';

        if (!m_IsCalibrated) {
            // 首次移动：未校准，使用固定时长作为基准
            Action.Duration = kCalibDuration;
            m_IsCalibrated = true;
        } else {
            // 用上次按键的实际像素位移更新速度估计（像素/秒）
            const double NewVelocity =
                std::abs(CurrentX - m_LastCursorX) / m_LastDuration;
            // 运行平均值：多样本取平均，逐步逼近真实速度
            m_Velocity = (m_Velocity * m_SampleCount + NewVelocity) / (m_SampleCount + 1);
            ++m_SampleCount;

            // 根据当前距离与速度计算最优按键时长
            const double Dist = std::abs(TargetX - CurrentX);
            Action.Duration = Dist / m_Velocity;

            // 钳制时长，避免极端情况（过短按键可能不触发，过长按键超调严重）
            if (Action.Duration < kMinDuration) Action.Duration = kMinDuration;
            if (Action.Duration > kMaxDuration) Action.Duration = kMaxDuration;
        }

        // 记录本次按键前的状态，供下次速度校准使用
        m_LastCursorX  = CurrentX;
        m_LastDuration = Action.Duration;

        return Action;
    }

private:
    // 自适应状态
    bool   m_IsCalibrated = false;  // 是否已完成首次速度校准
    double m_Velocity     = 0.0;    // 指针移动速度估计（像素/秒）
    int    m_LastCursorX  = 0;      // 上次按键前的指针 X 坐标
    double m_LastDuration = 0.1;    // 上次按键时长（秒）
    int    m_SampleCount  = 0;      // 已采集的速度样本数

    // 常量
    static constexpr int    kDeadZone      = 50;    // 指针跟随死区（像素）
    static constexpr double kCalibDuration = 0.1;   // 校准按键时长（秒）
    static constexpr double kMinDuration   = 0.05;   // 最小时长（秒）
    static constexpr double kMaxDuration   = 0.4;   // 最大时长（秒）
};
