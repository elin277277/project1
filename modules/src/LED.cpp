#include "LED.hpp"

namespace LED {
cLED::cLED(bsp::DigitalOut red, bsp::DigitalOut green, bsp::DigitalOut blue)
    : red_(red), green_(green), blue_(blue) {}

bool cLED::Init() {
    initialized_ = false;
    if (!red_.write || !green_.write || !blue_.write) return false;
    initialized_ = true;
    return Set(false, false, false);
}

bool cLED::Set(bool red_on, bool green_on, bool blue_on) {
    if (!initialized_) return false;
    red_.write(red_.context, !red_on);
    green_.write(green_.context, !green_on);
    blue_.write(blue_.context, !blue_on);
    return true;
}
} // namespace LED
