#pragma once
#include <cstdint>

// HOST TEST CONFIGURATION ONLY. Include ahead of threads/include for this target.
// Nonzero limits/gains make unintended drive observable; production defaults stay safe.
namespace app_config {
constexpr std::uint32_t imu_period_ms = 5;
constexpr std::uint32_t control_period_ms = 2;
constexpr std::uint32_t monitor_period_ms = 250;
constexpr bool motor_hardware_confirmed = true;
constexpr unsigned motor_bus = 0;
constexpr std::uint16_t motor_feedback_id = 0x201;
constexpr float motor_reduction = 36.0f;
constexpr std::uint16_t motor_current_limit = 100;
constexpr std::uint32_t motor_timeout_ms = 100;
constexpr float speed_kp = 10.0f;
constexpr float speed_ki = 0.0f;
constexpr float speed_kd = 0.0f;
constexpr float speed_integral_limit = 0.0f;
constexpr float position_kp = 1.0f;
constexpr float position_ki = 0.0f;
constexpr float position_kd = 0.0f;
constexpr float position_integral_limit = 0.0f;
constexpr float output_speed_limit = 20.0f;
constexpr std::uint32_t command_timeout_ms = 10;
}
