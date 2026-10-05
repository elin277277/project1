#include "bsp_can.hpp"
#include "can.h"
#include <atomic>

namespace {
using bsp::Status;
struct Bus { CAN_HandleTypeDef *handle; bsp::CanReceive receive{}; void *context{}; bool started{}; std::atomic<uint32_t> errors{0}; };
Bus buses[2]{{&hcan1},{&hcan2}};
constexpr uint32_t allMailboxes=CAN_TX_MAILBOX0|CAN_TX_MAILBOX1|CAN_TX_MAILBOX2;
Status status(HAL_StatusTypeDef s) {
    return s==HAL_OK?Status::ok:s==HAL_BUSY?Status::busy:s==HAL_TIMEOUT?Status::timeout:Status::io_error;
}
Status send(void *ctx, const bsp::CanFrame &frame) {
    auto &bus=*static_cast<Bus *>(ctx);
    const auto errors=bus.errors.load();
    if(frame.extended || frame.remote || frame.id>0x7ff || frame.size>8) return Status::invalid;
    if(!bus.started || HAL_CAN_GetState(bus.handle)!=HAL_CAN_STATE_LISTENING) return Status::not_ready;
    if(HAL_CAN_GetError(bus.handle)&HAL_CAN_ERROR_BOF) return Status::io_error;
    if(!HAL_CAN_GetTxMailboxesFreeLevel(bus.handle)) return Status::busy;
    CAN_TxHeaderTypeDef header{};
    header.StdId=frame.id; header.IDE=CAN_ID_STD; header.RTR=CAN_RTR_DATA; header.DLC=frame.size;
    uint32_t mailbox{};
    const auto result=status(HAL_CAN_AddTxMessage(bus.handle,&header,const_cast<uint8_t *>(frame.data),&mailbox));
    // If an error IRQ interrupted submission, do not leave a just-enqueued
    // command behind the ISR's abort request. Already-on-wire frames cannot be recalled.
    if(bus.errors.load()!=errors) {
        HAL_CAN_AbortTxRequest(bus.handle,allMailboxes);
        return Status::io_error;
    }
    return result;
}
Status start(void *ctx, bsp::CanReceive callback, void *receiver) {
    auto &bus=*static_cast<Bus *>(ctx);
    if(!callback)return Status::invalid;
    if(bus.started) return Status::busy;
    if(HAL_CAN_GetState(bus.handle)!=HAL_CAN_STATE_READY) return Status::not_ready;
    CAN_FilterTypeDef filter{};
    filter.FilterBank=bus.handle==&hcan1?0:14;
    filter.SlaveStartFilterBank=14;
    filter.FilterMode=CAN_FILTERMODE_IDMASK; filter.FilterScale=CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment=CAN_FILTER_FIFO0; filter.FilterActivation=ENABLE;
    // Accept all frames, validate ID/type/DLC in the registered protocol consumer.
    if(HAL_CAN_ConfigFilter(bus.handle,&filter)!=HAL_OK) return Status::io_error;
    bus.receive=callback; bus.context=receiver;
    if(HAL_CAN_Start(bus.handle)!=HAL_OK) return Status::io_error;
    bus.started=true;
    // NART is enabled by Board: a lost arbitration/error ends that attempt.
    // Without TX/LEC notifications the dropped command looks like a success.
    constexpr uint32_t flags=CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN |
        CAN_IT_TX_MAILBOX_EMPTY | CAN_IT_LAST_ERROR_CODE | CAN_IT_ERROR_WARNING |
        CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF | CAN_IT_ERROR;
    if(HAL_CAN_ActivateNotification(bus.handle,flags)!=HAL_OK) {
        bus.started=false; HAL_CAN_Stop(bus.handle); return Status::io_error;
    }
    return Status::ok;
}
Bus *lookup(CAN_HandleTypeDef *h) { for(auto &b:buses) if(b.handle==h)return &b; return nullptr; }
}
namespace bsp {
CanPort Can(unsigned bus) { return bus<2?CanPort{&buses[bus],send,start}:CanPort{}; }
uint32_t CanErrors(unsigned bus) { return bus<2?buses[bus].errors.load():0; }
Status CanAbort(unsigned bus) {
    if(bus>=2)return Status::invalid;
    if(!buses[bus].started)return Status::not_ready;
    return status(HAL_CAN_AbortTxRequest(buses[bus].handle,allMailboxes));
}
}
extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h) {
    auto *bus=lookup(h); if(!bus)return;
    // Hardware FIFO depth is three. Bound interrupt work under bus saturation.
    for(unsigned i=0;i<3 && HAL_CAN_GetRxFifoFillLevel(h,CAN_RX_FIFO0);++i) {
        CAN_RxHeaderTypeDef header{}; bsp::CanFrame frame{};
        if(HAL_CAN_GetRxMessage(h,CAN_RX_FIFO0,&header,frame.data)!=HAL_OK) {++bus->errors; break;}
        frame.extended=header.IDE!=CAN_ID_STD; frame.remote=header.RTR!=CAN_RTR_DATA;
        frame.id=frame.extended?header.ExtId:header.StdId; frame.size=static_cast<uint8_t>(header.DLC);
        if(bus->receive)bus->receive(bus->context,frame,HAL_GetTick());
    }
}
extern "C" void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *h) {
    if(auto *bus=lookup(h)) {
        ++bus->errors;
        // Nonblocking register request: no retry, bus-off auto-recovery, or
        // stale control mailboxes left queued after a detected transport fault.
        HAL_CAN_AbortTxRequest(h,allMailboxes);
    }
}
