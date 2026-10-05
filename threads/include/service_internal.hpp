#pragma once
#include "threads.hpp"
#include "tx_api.h"
#include <atomic>
namespace services::detail {
extern TX_SEMAPHORE imu_beat, control_beat;
extern TX_MUTEX imu_lock, command_lock;
extern imu_snapshot_t imu_sample;
struct Command { ControlMode mode{ControlMode::relax}; float target{}; uint32_t timestamp{}, sequence{}, epoch{}; };
extern Command command;
extern std::atomic<uint32_t> command_epoch;
extern std::atomic<bool> initialized, imu_healthy, control_healthy, imu_alive, control_alive;
extern std::atomic<bool> imu_pending, control_pending;
extern std::atomic<uint32_t> imu_last_beat, control_last_beat, monitor_last_beat;
bool consume_beat(TX_SEMAPHORE &,std::atomic<bool> &pending);
ULONG ticks(uint32_t ms);
uint32_t now();
void sleep_ms(void *,uint32_t ms);
void beat(TX_SEMAPHORE &,std::atomic<bool> &pending);
void alive_entry(ULONG);
void imu_entry(ULONG);
void control_entry(ULONG);
}
