#include "BMI088.hpp"

namespace {
constexpr uint32_t kTransferTimeoutMs = 10;
constexpr uint32_t kConfigBudgetMs = 1000;
constexpr uint32_t kUpdateBudgetMs = 30;
constexpr uint8_t kChipId = 0x00;
constexpr uint8_t kAccelChipId = 0x1E;
constexpr uint8_t kGyroChipId = 0x0F;
constexpr uint8_t kAccelData = 0x12;
constexpr uint8_t kGyroData = 0x02;
constexpr uint8_t kTemperature = 0x22;
constexpr uint8_t kAccelConf = 0x40;
constexpr uint8_t kAccelRange = 0x41;
constexpr uint8_t kAccelPowerConf = 0x7C;
constexpr uint8_t kAccelPowerCtrl = 0x7D;
constexpr uint8_t kAccelReset = 0x7E;
constexpr uint8_t kGyroRange = 0x0F;
constexpr uint8_t kGyroBandwidth = 0x10;
constexpr uint8_t kGyroPower = 0x11;
constexpr uint8_t kGyroReset = 0x14;
constexpr uint8_t kResetCommand = 0xB6;
// ACC_CONF: reserved bit7=1, normal BWP=2 in bits6:4, ODR400=0x0A.
// Bosch BMI088 datasheet / official BMI08x definitions: normal BW nibble=0xA.
// https://raw.githubusercontent.com/boschsensortec/BMI08x_SensorAPI/master/bmi08_defs.h
constexpr uint8_t kAccel400HzNormal = 0x80 | (0x02 << 4) | 0x0A; // 0xAA, NOT 0xAB (800Hz).
constexpr uint8_t kGyro400Hz47Hz = 0x03;
constexpr float kAccelScale = (6.0f * 9.80665f) / 32768.0f;
constexpr float kGyroScale = (2000.0f * 3.14159265358979323846f / 180.0f) / 32768.0f;

imu_status_t PortStatus(bsp::Status status) {
    switch (status) {
    case bsp::Status::ok: return imu_status_t::ok;
    case bsp::Status::timeout: return imu_status_t::timeout;
    case bsp::Status::busy: return imu_status_t::busy;
    case bsp::Status::invalid: return imu_status_t::invalid_port;
    case bsp::Status::not_ready:
    case bsp::Status::io_error: return imu_status_t::transport_error;
    }
    return imu_status_t::transport_error;
}

// Explicit sign extension avoids implementation-defined unsigned->int16 casts.
int32_t Signed16(const uint8_t *bytes) {
    const int32_t value = static_cast<int32_t>(bytes[0]) |
                          (static_cast<int32_t>(bytes[1]) << 8);
    return (value & 0x8000) ? value - 65536 : value;
}

imu_vector3_t Vector(const uint8_t *bytes, float scale) {
    return {Signed16(bytes) * scale, Signed16(bytes + 2) * scale,
            Signed16(bytes + 4) * scale};
}
} // namespace

namespace BMI088 {
cBMI088::cBMI088(bsp::SpiDevice accel, bsp::SpiDevice gyro, bsp::Clock clock)
    : accel_(accel), gyro_(gyro), clock_(clock) {}

bool cBMI088::PortsValid() const {
    // A null context is legal for a stateless callback; callbacks are mandatory.
    return accel_.exchange && accel_.cs.write && gyro_.exchange && gyro_.cs.write &&
           clock_.now_ms && clock_.sleep_ms;
}

void cBMI088::Deselect() {
    if (accel_.cs.write) accel_.cs.write(accel_.cs.context, true);
    if (gyro_.cs.write) gyro_.cs.write(gyro_.cs.context, true);
}

imu_status_t cBMI088::Fail(imu_status_t error) {
    Deselect();
    initialized_ = false;
    status_ = error;
    return error;
}

uint32_t cBMI088::RemainingMs() const {
    const uint32_t elapsed = clock_.now_ms(clock_.context) - operation_start_ms_;
    return elapsed < operation_budget_ms_ ? operation_budget_ms_ - elapsed : 0;
}

imu_status_t cBMI088::Delay(uint32_t ms) {
    if (RemainingMs() <= ms) return imu_status_t::timeout;
    clock_.sleep_ms(clock_.context, ms);
    return RemainingMs() ? imu_status_t::ok : imu_status_t::timeout;
}

imu_status_t cBMI088::Exchange(const bsp::SpiDevice &device, const uint8_t *tx,
                               uint8_t *rx, size_t size) {
    const uint32_t remaining = RemainingMs();
    if (!remaining) return imu_status_t::timeout;
    const uint32_t timeout = remaining < kTransferTimeoutMs ? remaining : kTransferTimeoutMs;
    device.cs.write(device.cs.context, false);
    const bsp::Status result = device.exchange(device.context, tx, rx, size, timeout);
    // Always release CS, including timeout, busy and partially completed transfers.
    device.cs.write(device.cs.context, true);
    if (result != bsp::Status::ok) return PortStatus(result);
    return RemainingMs() ? imu_status_t::ok : imu_status_t::timeout;
}

imu_status_t cBMI088::Read(const bsp::SpiDevice &device, bool accel, uint8_t address,
                           uint8_t *data, size_t size) {
    // Private reads are at most XYZ (6 bytes). One exchange holds CS for address,
    // accel-only dummy, and every data byte. MOSI clocks out zero after address.
    if (!data || !size || size > 6) return imu_status_t::invalid_port;
    uint8_t tx[8]{};
    uint8_t rx[8]{};
    tx[0] = address | 0x80;
    const size_t prefix = accel ? 2 : 1;
    const imu_status_t result = Exchange(device, tx, rx, size + prefix);
    if (result == imu_status_t::ok)
        for (size_t i = 0; i < size; ++i) data[i] = rx[i + prefix];
    return result;
}

imu_status_t cBMI088::Write(const bsp::SpiDevice &device, uint8_t address, uint8_t value) {
    const uint8_t tx[2]{static_cast<uint8_t>(address & 0x7F), value};
    uint8_t rx[2]{};
    return Exchange(device, tx, rx, sizeof(tx));
}

imu_status_t cBMI088::CheckId(const bsp::SpiDevice &device, bool accel) {
    uint8_t id{};
    const imu_status_t result = Read(device, accel, kChipId, &id, 1);
    if (result != imu_status_t::ok) return result;
    if (id == (accel ? kAccelChipId : kGyroChipId)) return imu_status_t::ok;
    return accel ? imu_status_t::accel_id_error : imu_status_t::gyro_id_error;
}

imu_status_t cBMI088::ConfigureRegister(const bsp::SpiDevice &device, bool accel,
                                        uint8_t address, uint8_t value,
                                        uint8_t mask, uint32_t settle_ms) {
    imu_status_t result = Write(device, address, value);
    if (result != imu_status_t::ok) return result;
    result = Delay(settle_ms);
    if (result != imu_status_t::ok) return result;
    uint8_t actual{};
    result = Read(device, accel, address, &actual, 1);
    if (result != imu_status_t::ok) return result;
    return (actual & mask) == (value & mask) ? imu_status_t::ok : imu_status_t::config_error;
}

imu_status_t cBMI088::Config() {
    initialized_ = false;
    status_ = imu_status_t::not_initialized;
    if (!PortsValid()) return Fail(imu_status_t::invalid_port);
    Deselect();
    operation_start_ms_ = clock_.now_ms(clock_.context);
    operation_budget_ms_ = kConfigBudgetMs;
    // Also permit Config immediately after board power-up, before the first ID.
    imu_status_t result = Delay(80);
    if (result != imu_status_t::ok) return Fail(result);

    // First accel SPI read switches interface mode: discard its value, NOT errors.
    uint8_t ignored{};
    result = Read(accel_, true, kChipId, &ignored, 1);
    if (result != imu_status_t::ok) return Fail(result);
    result = Delay(1);
    if (result != imu_status_t::ok) return Fail(result);
    result = CheckId(accel_, true);
    if (result != imu_status_t::ok) return Fail(result);
    result = CheckId(gyro_, false);
    if (result != imu_status_t::ok) return Fail(result);

    result = Write(accel_, kAccelReset, kResetCommand);
    if (result != imu_status_t::ok) return Fail(result);
    result = Delay(50);
    if (result != imu_status_t::ok) return Fail(result);
    // Soft reset also resets interface selection; reselect SPI before ID/config.
    result = Read(accel_, true, kChipId, &ignored, 1);
    if (result != imu_status_t::ok) return Fail(result);
    result = Delay(1);
    if (result != imu_status_t::ok) return Fail(result);
    result = CheckId(accel_, true);
    if (result != imu_status_t::ok) return Fail(result);

    result = Write(gyro_, kGyroReset, kResetCommand);
    if (result != imu_status_t::ok) return Fail(result);
    result = Delay(80);
    if (result != imu_status_t::ok) return Fail(result);
    result = CheckId(gyro_, false);
    if (result != imu_status_t::ok) return Fail(result);

    struct Setting { bool accel; uint8_t address, value, mask; uint32_t settle_ms; };
    const Setting settings[]{
        // Leave suspend BEFORE enabling the accel measurement engine.
        {true, kAccelPowerConf, 0x00, 0xFF, 5},
        {true, kAccelPowerCtrl, 0x04, 0xFF, 50},
        {true, kAccelRange, 0x01, 0x03, 1},
        {true, kAccelConf, kAccel400HzNormal, 0xFF, 40},
        {false, kGyroPower, 0x00, 0xA0, 30},
        {false, kGyroRange, 0x00, 0x07, 1},
        // Bit7 is read-only in GYRO_BANDWIDTH; verify the documented BW field.
        {false, kGyroBandwidth, kGyro400Hz47Hz, 0x0F, 10}
    };
    for (const Setting &setting : settings) {
        result = ConfigureRegister(setting.accel ? accel_ : gyro_, setting.accel,
                                   setting.address, setting.value, setting.mask,
                                   setting.settle_ms);
        if (result != imu_status_t::ok) return Fail(result);
    }
    initialized_ = true;
    status_ = imu_status_t::ok;
    return status_;
}

imu_status_t cBMI088::Update(imu_snapshot_t &out) {
    // Clear caller's previous successful sample even if this call fails early.
    out = {};
    out.sequence = sequence_;
    if (!initialized_) {
        out.error = status_;
        return status_;
    }
    operation_start_ms_ = clock_.now_ms(clock_.context);
    operation_budget_ms_ = kUpdateBudgetMs;
    uint8_t accel_bytes[6]{};
    uint8_t gyro_bytes[6]{};
    uint8_t temperature_bytes[2]{};
    imu_status_t result = Read(accel_, true, kAccelData, accel_bytes, sizeof(accel_bytes));
    if (result == imu_status_t::ok)
        result = Read(gyro_, false, kGyroData, gyro_bytes, sizeof(gyro_bytes));
    if (result == imu_status_t::ok)
        result = Read(accel_, true, kTemperature, temperature_bytes, sizeof(temperature_bytes));
    if (result != imu_status_t::ok) {
        out.error = Fail(result);
        return result;
    }
    imu_snapshot_t next{};
    next.accel = Vector(accel_bytes, kAccelScale);
    next.gyro = Vector(gyro_bytes, kGyroScale);
    int32_t temperature = (static_cast<int32_t>(temperature_bytes[0]) << 3) |
                          (temperature_bytes[1] >> 5);
    if (temperature & 0x400) temperature -= 2048;
    next.temperature_c = 23.0f + temperature * 0.125f;
    next.timestamp_ms = clock_.now_ms(clock_.context);
    next.sequence = ++sequence_;
    next.error = imu_status_t::ok;
    next.valid = true;
    out = next;
    status_ = imu_status_t::ok;
    return status_;
}
} // namespace BMI088
