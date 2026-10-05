#pragma once
#include <cstdint>

namespace app_config {
// Services compile independently; enable them with Board CMake options.
constexpr uint32_t imu_period_ms = 5;
constexpr uint32_t control_period_ms = 2;
constexpr uint32_t monitor_period_ms = 250;

// M2006 + C610 is an EXAMPLE, not a declaration of the installed hardware.
constexpr bool motor_hardware_confirmed = false; // 改成实际电机：核对型号、电调、接线及协议后才能改 true
constexpr unsigned motor_bus = 0;                // 改成实际电机：0=CAN1，1=CAN2
constexpr uint16_t motor_feedback_id = 0x201;     // 改成实际电机：C610 的 ID 1 对应 0x201
constexpr float motor_reduction = 36.0f;         // 改成实际电机：减速比，转子/输出轴
constexpr uint16_t motor_current_limit = 0;      // 改成实际电机：先用保守限幅；C610 协议计数最大 10000，不是 A
constexpr uint32_t motor_timeout_ms = 100;       // 改成实际电机：结合反馈频率、任务周期和安全要求
constexpr float speed_kp = 0.0f;                 // 改成实际电机：先调速度环，输入 rad/s、输出电流指令计数
constexpr float speed_ki = 0.0f;                 // 改成实际电机：连续时间积分系数
constexpr float speed_kd = 0.0f;                 // 改成实际电机：连续时间微分系数
constexpr float speed_integral_limit = 0.0f;     // 改成实际电机：积分输出计数限幅
constexpr float position_kp = 0.0f;              // 改成实际电机：外环 rad -> rad/s
constexpr float position_ki = 0.0f;              // 改成实际电机：完成速度环后再调外环
constexpr float position_kd = 0.0f;              // 改成实际电机：结合噪声与周期决定是否启用
constexpr float position_integral_limit = 0.0f;  // 改成实际电机：外环积分输出 rad/s 限幅
constexpr float output_speed_limit = 0.0f;       // 改成实际电机：输出轴 rad/s 限速
constexpr uint32_t command_timeout_ms = 100;     // 改成实际电机：上层命令租期，需持续刷新，禁止一次命令永久运行
}
