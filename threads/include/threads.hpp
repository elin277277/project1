#pragma once
#include "IMU.hpp"
#include <cstdint>

namespace services {
enum class ControlMode : uint8_t { relax, speed, position };
// Thread context only. Nonblocking mailbox. Caller must refresh within lease.
// After a fault: request relax before explicitly requesting another drive mode.
bool RequestMotor(ControlMode mode,float target);
bool ImuSnapshot(imu_snapshot_t &out); // returns false if no valid recent sample
struct Health {
    bool initialized{}, imu_alive{}, control_alive{}, imu_healthy{}, control_healthy{};
};
bool HealthSnapshot(Health &out);
}
extern "C" void app_start(void);
