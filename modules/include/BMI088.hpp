#pragma once
#include "IMU.hpp"
#include "bsp_ports.hpp"

namespace BMI088 {

// Polling driver: accel +/-6g, normal bandwidth, 400Hz; gyro +/-2000deg/s,
// 400Hz / 47Hz bandwidth. No heater, axis remapping or implicit calibration.
// All ports are synchronous, and callbacks/contexts must outlive this object.
// The owner must serialize the entire operation on the shared SPI bus.
class cBMI088 final : public cIMU {
public:
    // Construction does not touch the board. Call Config after board/BSP init.
    cBMI088(bsp::SpiDevice accel, bsp::SpiDevice gyro, bsp::Clock clock);
    cBMI088(const cBMI088 &) = delete;
    cBMI088 &operator=(const cBMI088 &) = delete;

    // Bounded initialization: at most 1000ms with a conforming injected clock
    // and transport; each exchange has a timeout of at most 10ms.
    imu_status_t Config() override;
    // All three reads must succeed within 30ms. A failure latches the driver
    // uninitialized until explicit Config(). No stale/partial sample is returned.
    // Samples are polled, not hardware-synchronized or guaranteed newly ready.
    imu_status_t Update(imu_snapshot_t &out) override;
    bool initialized() const override { return initialized_; }
    imu_status_t status() const override { return status_; }

private:
    bsp::SpiDevice accel_;
    bsp::SpiDevice gyro_;
    bsp::Clock clock_;
    bool initialized_{};
    imu_status_t status_{imu_status_t::not_initialized};
    uint32_t sequence_{}; // Preserved across explicit reconfiguration.
    uint32_t operation_start_ms_{};
    uint32_t operation_budget_ms_{};

    bool PortsValid() const;
    void Deselect();
    imu_status_t Fail(imu_status_t error);
    uint32_t RemainingMs() const;
    imu_status_t Delay(uint32_t ms);
    imu_status_t Exchange(const bsp::SpiDevice &device, const uint8_t *tx,
                          uint8_t *rx, size_t size);
    imu_status_t Read(const bsp::SpiDevice &device, bool accel, uint8_t address,
                      uint8_t *data, size_t size);
    imu_status_t Write(const bsp::SpiDevice &device, uint8_t address, uint8_t value);
    imu_status_t CheckId(const bsp::SpiDevice &device, bool accel);
    imu_status_t ConfigureRegister(const bsp::SpiDevice &device, bool accel,
                                   uint8_t address, uint8_t value,
                                   uint8_t mask, uint32_t settle_ms);
};

} // namespace BMI088
