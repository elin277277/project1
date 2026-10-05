#pragma once
#include <cstdint>

// Platform-independent status and complete-sample contract.
enum class imu_status_t : uint8_t {
    ok,
    not_initialized,
    invalid_port,
    transport_error,
    timeout,
    busy,
    accel_id_error,
    gyro_id_error,
    config_error
};

struct imu_vector3_t {
    float x{};
    float y{};
    float z{};
};

struct imu_snapshot_t {
    imu_vector3_t accel{}; // Sensor axes, m/s^2; no installation rotation applied.
    imu_vector3_t gyro{};  // Sensor axes, rad/s; no assumed calibration offsets.
    float temperature_c{};
    bool valid{};
    uint32_t timestamp_ms{}; // Injected clock at completion; wraps modulo 2^32.
    uint32_t sequence{};     // Successful complete reads only; wraps modulo 2^32.
    imu_status_t error{imu_status_t::not_initialized};
};

// Single-owner driver. A service must synchronize publication to other threads.
class cIMU {
public:
    virtual ~cIMU() = default;
    virtual imu_status_t Config() = 0;
    // Every failure replaces out with an invalid, zero-data snapshot, not old data.
    virtual imu_status_t Update(imu_snapshot_t &out) = 0;
    virtual bool initialized() const = 0;
    virtual imu_status_t status() const = 0;
};
