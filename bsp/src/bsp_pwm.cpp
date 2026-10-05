#include "bsp_pwm.hpp"
#include "tim.h"
#include <cmath>

namespace {
bool running{};
bool ready() { return htim1.Instance==TIM1 && htim1.State!=HAL_TIM_STATE_RESET; }
uint32_t timerClock() {
    uint32_t clock=HAL_RCC_GetPCLK2Freq();
    if((RCC->CFGR&RCC_CFGR_PPRE2)!=0)clock*=2;
    return clock;
}
}
namespace bsp {
Status PwmStart() {
    if(!ready())return Status::not_ready;
    if(running)return Status::busy;
    if(HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_1)!=HAL_OK)return Status::io_error;
    running=true; return Status::ok;
}
Status PwmStop() {
    if(!ready())return Status::not_ready;
    __HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_1,0);
    if(HAL_TIM_PWM_Stop(&htim1,TIM_CHANNEL_1)!=HAL_OK)return Status::io_error;
    running=false; return Status::ok;
}
Status PwmSetPeriod(float seconds) {
    if(!ready())return Status::not_ready;
    if(running)return Status::busy;
    const double counts=static_cast<double>(seconds)*timerClock()/(htim1.Instance->PSC+1U);
    if(!std::isfinite(counts)||counts<1||counts>65536)return Status::invalid;
    const auto period=static_cast<uint32_t>(std::llround(counts));
    __HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_1,0);
    __HAL_TIM_SET_AUTORELOAD(&htim1,period-1);
    htim1.Init.Period=period-1;
    __HAL_TIM_SET_COUNTER(&htim1,0);
    htim1.Instance->EGR=TIM_EGR_UG;
    return Status::ok;
}
Status PwmSetDuty(float ratio) {
    if(!ready())return Status::not_ready;
    if(!std::isfinite(ratio)||ratio<0||ratio>1)return Status::invalid;
    const uint32_t counts=htim1.Instance->ARR+1;
    const auto compare=static_cast<uint32_t>(std::lround(ratio*counts));
    // TIM1 CCR is 16-bit; ARR=65535 cannot encode exact 100% in PWM1.
    if(compare>65535)return Status::invalid;
    __HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_1,compare);
    return Status::ok;
}
Status ServoPulseUs(uint16_t us) {
    if(!ready())return Status::not_ready;
    if(us<500||us>2500)return Status::invalid;
    const double period=static_cast<double>(htim1.Instance->ARR+1)*(htim1.Instance->PSC+1)/timerClock();
    if(std::fabs(period-.02)>.00001)return Status::invalid;
    return PwmSetDuty(static_cast<float>(us/1e6/period));
}
}
