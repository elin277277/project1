#include "bsp_usart.hpp"
#include "usart.h"
#include <atomic>

namespace {
using bsp::Status;
struct Uart {
    UART_HandleTypeDef *handle;
    alignas(4) uint8_t data[128]{};
    uint16_t position{};
    bsp::UartReceive callback{};
    void *context{};
    std::atomic<uint32_t> errors{0};
    std::atomic<bool> fault{false};
    bool started{};
};
Uart uarts[2]{{&huart1},{&huart6}};
Uart *lookup(UART_HandleTypeDef *h) {for(auto &u:uarts)if(u.handle==h)return &u;return nullptr;}
Status result(HAL_StatusTypeDef s) { return s==HAL_OK?Status::ok:s==HAL_BUSY?Status::busy:s==HAL_TIMEOUT?Status::timeout:Status::io_error; }
Status receive(Uart &u) {
    // One service owns start/recovery. Keep callbacks suppressed until the old
    // stream is stopped and the new cursor is initialized.
    u.fault=true;
    if(u.handle->RxState==HAL_UART_STATE_RESET || !u.handle->hdmarx ||
       u.handle->hdmarx->Init.Mode!=DMA_CIRCULAR)return Status::not_ready;
    if(u.handle->RxState!=HAL_UART_STATE_READY)return Status::busy;
    u.position=0;
    u.fault=false;
    const auto r=result(HAL_UARTEx_ReceiveToIdle_DMA(u.handle,u.data,sizeof(u.data)));
    u.started=r==Status::ok;
    if(r!=Status::ok)u.fault=true;
    // An error IRQ may run inside the HAL start. Never clear its fault here.
    return r==Status::ok&&u.fault.load()?Status::io_error:r;
}
}
namespace bsp {
Status UartStartReceive(unsigned port,UartReceive callback,void *ctx) {
    if(port>=2||!callback||__get_IPSR()!=0)return Status::invalid;
    auto &u=uarts[port]; if(u.started)return Status::busy;
    u.fault=true;u.callback=callback;u.context=ctx;
    return receive(u);
}
Status UartRecoverReceive(unsigned port) {
    if(port>=2||__get_IPSR()!=0)return Status::invalid;
    auto &u=uarts[port];if(!u.callback)return Status::not_ready;
    if(!u.fault.load())return u.started&&u.handle->RxState==HAL_UART_STATE_BUSY_RX?
        Status::ok:Status::not_ready;
    if(u.handle->RxState==HAL_UART_STATE_RESET)return Status::not_ready;
    // Blocking abort only in service context. Keep fault set during abort.
    if(HAL_UART_AbortReceive(u.handle)!=HAL_OK)return Status::io_error;
    u.started=false;
    return receive(u);
}
Status UartTransmit(unsigned port,const uint8_t *data,size_t size,UartMode mode,uint32_t timeout) {
    if(port>=2||!data||!size||size>65535)return Status::invalid;
    auto *h=uarts[port].handle;
    if(h->gState==HAL_UART_STATE_RESET)return Status::not_ready;
    if(h->gState!=HAL_UART_STATE_READY)return Status::busy;
    auto *buffer=const_cast<uint8_t *>(data);
    switch(mode) {
    case UartMode::blocking:
        if(!timeout || __get_IPSR()!=0)return Status::invalid;
        return result(HAL_UART_Transmit(h,buffer,static_cast<uint16_t>(size),timeout));
    case UartMode::interrupt: return result(HAL_UART_Transmit_IT(h,buffer,static_cast<uint16_t>(size)));
    case UartMode::dma:
        if(!h->hdmatx || h->hdmatx->Init.Mode!=DMA_NORMAL)return Status::not_ready;
        return result(HAL_UART_Transmit_DMA(h,buffer,static_cast<uint16_t>(size)));
    }
    return Status::invalid;
}
bool UartTxReady(unsigned port) {return port<2&&uarts[port].handle->gState==HAL_UART_STATE_READY;}
uint32_t UartErrors(unsigned port) {return port<2?uarts[port].errors.load():0;}
}
extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *h,uint16_t) {
    auto *u=lookup(h);if(!u||u->fault.load()||!u->callback||!h->hdmarx)return;
    // HT/TC have fixed HAL Size values and may be serviced AFTER a newer IDLE.
    // Sample the same live producer cursor for every event instead. RX DMA and
    // UART IRQs must remain non-preempting peers (Board uses priority 6).
    // Required latency bound: IRQ delay + callback copy time < half-buffer wire
    // time (64*10/115200 = 5.56 ms at 8N1). NDTR cannot detect a missed full lap;
    // callers must not wait in this callback or retain pointers to the DMA ring.
    const uint32_t remaining=__HAL_DMA_GET_COUNTER(h->hdmarx);
    if(remaining>sizeof(u->data)) {++u->errors;u->fault=true;return;}
    const uint16_t position=static_cast<uint16_t>((sizeof(u->data)-remaining)%sizeof(u->data));
    const auto old=u->position;
    if(position==old)return;
    u->position=position;
    if(position<old) {
        u->callback(u->context,u->data+old,sizeof(u->data)-old);
        if(position)u->callback(u->context,u->data,position);
    } else u->callback(u->context,u->data+old,position-old);
}
extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *h) {
    if(auto *u=lookup(h)) { ++u->errors;u->fault=true; }
}
