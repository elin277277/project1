#pragma once
#include <cstddef>
#include <cstdint>

// HAL/RTOS-free ports. The context and buffers must outlive asynchronous use.
namespace bsp {
enum class Status : uint8_t { ok, invalid, not_ready, busy, timeout, io_error };
struct Clock {
    void *context{};
    uint32_t (*now_ms)(void *){};
    void (*sleep_ms)(void *, uint32_t){};
};
struct DigitalOut {
    void *context{};
    void (*write)(void *, bool high){};
};
struct SpiDevice {
    void *context{};
    Status (*exchange)(void *, const uint8_t *tx, uint8_t *rx, size_t size, uint32_t timeout_ms){};
    DigitalOut cs{}; // Active low. One service owns a bus for the whole transaction.
};
struct CanFrame {
    uint32_t id{};
    uint8_t size{};
    uint8_t data[8]{};
    bool extended{};
    bool remote{};
};
using CanReceive = void (*)(void *, const CanFrame &, uint32_t timestamp_ms);
struct CanPort {
    void *context{};
    Status (*send)(void *, const CanFrame &){}; // Nonblocking, copies into mailbox.
    Status (*start)(void *, CanReceive, void *receiver_context){};
};
struct I2cPort {
    void *context{};
    Status (*read_register)(void *, uint8_t address7, uint8_t reg, uint8_t *, size_t, uint32_t){};
    Status (*write_register)(void *, uint8_t address7, uint8_t reg, const uint8_t *, size_t, uint32_t){};
};
}
