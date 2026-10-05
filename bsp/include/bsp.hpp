#pragma once
#include "bsp_ports.hpp"
namespace bsp {
Clock SystemClock(); // HAL tick and blocking sleep, inject RTOS sleep in services.
SpiDevice ImuAccelPort();
SpiDevice ImuGyroPort();
DigitalOut LedRed();
DigitalOut LedGreen();
DigitalOut LedBlue();
I2cPort MagnetometerPort();
}
