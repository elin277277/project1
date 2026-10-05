#include "service_internal.hpp"
#include "app_config.hpp"
#include "bsp.hpp"
#include "BMI088.hpp"

namespace services::detail {
void imu_entry(ULONG) {
    auto clock=bsp::SystemClock();clock.sleep_ms=sleep_ms;
    BMI088::cBMI088 imu{bsp::ImuAccelPort(),bsp::ImuGyroPort(),clock};
    for(;;) {
        imu_snapshot_t sample{};
        if(!imu.initialized()) {
            sample.error=imu.Config();
            imu_healthy=sample.error==imu_status_t::ok;
        }
        if(imu.initialized()) {
            imu.Update(sample);imu_healthy=sample.valid;
        }
        if(tx_mutex_get(&imu_lock,TX_WAIT_FOREVER)==TX_SUCCESS) {
            imu_sample=sample;tx_mutex_put(&imu_lock);
        } else imu_healthy=false;
        beat(imu_beat,imu_pending);
        // Failed hardware initialization retries slowly; heartbeat still runs.
        if(!imu_healthy.load()) {
            for(unsigned i=0;i<10;++i) {sleep_ms(nullptr,100);beat(imu_beat,imu_pending);}
        } else sleep_ms(nullptr,app_config::imu_period_ms);
    }
}
}
