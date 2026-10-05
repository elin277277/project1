#include "service_internal.hpp"
#include "app_config.hpp"
#include "bsp.hpp"
#include "LED.hpp"

namespace services::detail {
void alive_entry(ULONG) {
    LED::cLED led{bsp::LedRed(),bsp::LedGreen(),bsp::LedBlue()};
    if(!led.Init())initialized=false;
    sleep_ms(nullptr,1500); // Allow reset/ID/config verification; health still reports initialization.
    bool phase=false;
    for(;;) {
        imu_alive=!APP_ENABLE_IMU || consume_beat(imu_beat,imu_pending);
        control_alive=!APP_ENABLE_CONTROL || consume_beat(control_beat,control_pending);
        monitor_last_beat=now();
        const bool good=initialized.load()&&imu_alive.load()&&control_alive.load()&&
            (!APP_ENABLE_IMU||imu_healthy.load())&&(!APP_ENABLE_CONTROL||control_healthy.load());
        phase=!phase;
        // Red=fault, green blink=all enabled services healthy, blue=monitor alive.
        led.Set(!good,good&&phase,phase);
        sleep_ms(nullptr,app_config::monitor_period_ms);
    }
}
}
