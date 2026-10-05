#include "service_internal.hpp"
#include "app_config.hpp"
#include "bsp.hpp"
#include "LED.hpp"
#include <cmath>

namespace services::detail {
TX_SEMAPHORE imu_beat{},control_beat{};
TX_MUTEX imu_lock{},command_lock{};
imu_snapshot_t imu_sample{};
Command command{};
std::atomic<uint32_t> command_epoch{0};
std::atomic<bool> initialized{false},imu_healthy{false},control_healthy{false},imu_alive{false},control_alive{false};
std::atomic<bool> imu_pending{false},control_pending{false};
std::atomic<uint32_t> imu_last_beat{0},control_last_beat{0},monitor_last_beat{0};
ULONG ticks(uint32_t ms) { return static_cast<ULONG>((static_cast<uint64_t>(ms)*TX_TIMER_TICKS_PER_SECOND+999)/1000); }
uint32_t now() { const auto clock=bsp::SystemClock();return clock.now_ms(clock.context); }
void sleep_ms(void *,uint32_t ms) { tx_thread_sleep(ticks(ms)?ticks(ms):1); }
void beat(TX_SEMAPHORE &sem,std::atomic<bool> &pending) {
    (&sem==&imu_beat?imu_last_beat:control_last_beat).store(now());
    // Token and pending flag form one short atomic operation; no waiter blocks here.
    const UINT mask=tx_interrupt_control(TX_INT_DISABLE);
    if(!pending.exchange(true) && tx_semaphore_put(&sem)!=TX_SUCCESS)pending=false;
    tx_interrupt_control(mask);
}
bool consume_beat(TX_SEMAPHORE &sem,std::atomic<bool> &pending) {
    const UINT mask=tx_interrupt_control(TX_INT_DISABLE);
    const bool got=tx_semaphore_get(&sem,TX_NO_WAIT)==TX_SUCCESS;
    pending=false;
    tx_interrupt_control(mask);
    return got;
}
namespace {
TX_THREAD alive_thread{};
alignas(8) UCHAR alive_stack[1536];
#if APP_ENABLE_IMU
TX_THREAD imu_thread{};
alignas(8) UCHAR imu_stack[3072];
#endif
#if APP_ENABLE_CONTROL
TX_THREAD control_thread{};
alignas(8) UCHAR control_stack[3072];
#endif
[[noreturn]] void fatal() {
    LED::cLED led{bsp::LedRed(),bsp::LedGreen(),bsp::LedBlue()};led.Init();led.Set(true,false,false);
    for(;;){} // Kernel objects failed; no hardware drive is started.
}
void checked(UINT result) { if(result!=TX_SUCCESS)fatal(); }
}
}
extern "C" void tx_application_define(void *) {
    using namespace services::detail;
    checked(tx_mutex_create(&imu_lock,const_cast<CHAR *>("imu"),TX_INHERIT));
    checked(tx_mutex_create(&command_lock,const_cast<CHAR *>("command"),TX_INHERIT));
    checked(tx_semaphore_create(&imu_beat,const_cast<CHAR *>("imu_beat"),0));
    checked(tx_semaphore_create(&control_beat,const_cast<CHAR *>("control_beat"),0));
    checked(tx_thread_create(&alive_thread,const_cast<CHAR *>("alive"),alive_entry,0,alive_stack,sizeof alive_stack,12,12,TX_NO_TIME_SLICE,TX_AUTO_START));
#if APP_ENABLE_IMU
    checked(tx_thread_create(&imu_thread,const_cast<CHAR *>("imu"),imu_entry,0,imu_stack,sizeof imu_stack,8,8,TX_NO_TIME_SLICE,TX_AUTO_START));
#endif
#if APP_ENABLE_CONTROL
    checked(tx_thread_create(&control_thread,const_cast<CHAR *>("control"),control_entry,0,control_stack,sizeof control_stack,5,5,TX_NO_TIME_SLICE,TX_AUTO_START));
#endif
    initialized=true;
}
extern "C" void app_start(void) { tx_kernel_enter(); }
namespace services {
bool RequestMotor(ControlMode mode,float target) {
    using namespace detail;
    if(!APP_ENABLE_CONTROL||!initialized.load()||!std::isfinite(target)||
       (mode!=ControlMode::relax&&mode!=ControlMode::speed&&mode!=ControlMode::position))return false;
    if(tx_mutex_get(&command_lock,TX_NO_WAIT)!=TX_SUCCESS)return false;
    command.mode=mode;command.target=target;command.timestamp=now();
    command.epoch=command_epoch.load();++command.sequence;
    tx_mutex_put(&command_lock);return true;
}
bool ImuSnapshot(imu_snapshot_t &out) {
    using namespace detail;out={};
    if(!initialized.load()||!APP_ENABLE_IMU||tx_mutex_get(&imu_lock,TX_NO_WAIT)!=TX_SUCCESS)return false;
    out=imu_sample;tx_mutex_put(&imu_lock);
    if(!out.valid||static_cast<uint32_t>(now()-out.timestamp_ms)>50) {out.valid=false;return false;}
    return true;
}
bool HealthSnapshot(Health &out) {
    using namespace detail;
    const uint32_t time=now(),monitor=monitor_last_beat.load();
    const bool monitorFresh=monitor!=0 && time-monitor<2*app_config::monitor_period_ms;
    const bool imuFresh=!APP_ENABLE_IMU || (imu_alive.load()&&time-imu_last_beat.load()<2*app_config::monitor_period_ms);
    const bool controlFresh=!APP_ENABLE_CONTROL || (control_alive.load()&&time-control_last_beat.load()<2*app_config::monitor_period_ms);
    out={initialized.load(),monitorFresh&&imuFresh,monitorFresh&&controlFresh,
         monitorFresh&&imuFresh&&(!APP_ENABLE_IMU||imu_healthy.load()),
         monitorFresh&&controlFresh&&(!APP_ENABLE_CONTROL||control_healthy.load())};
    return out.initialized&&monitorFresh;
}
}
