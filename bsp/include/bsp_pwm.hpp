#pragma once
#include "bsp_ports.hpp"
namespace bsp {
// C-board servo output: TIM1 CH1; remains stopped after Board initialization.
Status PwmStart();
Status PwmStop();
Status PwmSetPeriod(float seconds); // changes permitted only while stopped
Status PwmSetDuty(float ratio);
Status ServoPulseUs(uint16_t microseconds); // 500..2500 us, requires 20 ms period
}
