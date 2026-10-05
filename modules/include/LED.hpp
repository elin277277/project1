#pragma once
#include "bsp_ports.hpp"

namespace LED {
// Active-low RGB output. Board owns pins; a service owns sequencing/scheduling.
class cLED final {
public:
    // No GPIO access during construction; port contexts must outlive this object.
    cLED(bsp::DigitalOut red, bsp::DigitalOut green, bsp::DigitalOut blue);
    cLED(const cLED &) = delete;
    cLED &operator=(const cLED &) = delete;
    // Call after board initialization. Starts with all colors off.
    bool Init();
    // True means illuminated (physical low). Refuses use before successful Init.
    bool Set(bool red_on, bool green_on, bool blue_on);
    bool initialized() const { return initialized_; }

private:
    bsp::DigitalOut red_;
    bsp::DigitalOut green_;
    bsp::DigitalOut blue_;
    bool initialized_{};
};
} // namespace LED
