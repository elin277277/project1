#pragma once
#include "bsp_ports.hpp"
namespace bsp {
enum class UartMode { blocking, interrupt, dma };
// ISR callback, data valid only during call; copy if needed, never wait.
using UartReceive = void (*)(void *, const uint8_t *, size_t);
Status UartStartReceive(unsigned port, UartReceive callback, void *context);
Status UartRecoverReceive(unsigned port); // call from service after errors, not ISR.
// DMA/IT caller must retain buffer unchanged until UartTxReady returns true.
Status UartTransmit(unsigned port, const uint8_t *, size_t, UartMode, uint32_t timeout_ms=20);
bool UartTxReady(unsigned port);
uint32_t UartErrors(unsigned port);
}
