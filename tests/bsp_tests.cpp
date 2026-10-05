#include "bsp_can.hpp"
#include "bsp_usart.hpp"
#include "mock_hal.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

UART_HandleTypeDef huart1,huart6;
CAN_HandleTypeDef hcan1,hcan2;
namespace {
std::vector<uint8_t> received;
void rx(void *,const uint8_t *data,size_t size) {received.insert(received.end(),data,data+size);}
void canRx(void *,const bsp::CanFrame &,uint32_t) {}
void uartTests() {
    using bsp::Status;
    DMA_HandleTypeDef rx1,rx6,tx1;
    tx1.Init.Mode=DMA_NORMAL;
    huart1.hdmarx=&rx1;huart1.hdmatx=&tx1;huart6.hdmarx=&rx6;
    // A failed first start must not make Recover report success without DMA.
    assert(bsp::UartStartReceive(0,rx,nullptr)==Status::not_ready);
    assert(bsp::UartRecoverReceive(0)==Status::not_ready);
    huart1.RxState=huart1.gState=HAL_UART_STATE_READY;
    mock_uart_start=HAL_ERROR;
    assert(bsp::UartRecoverReceive(0)==Status::io_error);
    mock_uart_start=HAL_OK;
    assert(bsp::UartRecoverReceive(0)==Status::ok);
    assert(bsp::UartStartReceive(0,rx,nullptr)==Status::busy);
    for(unsigned i=0;i<128;++i)huart1.buffer[i]=static_cast<uint8_t>(i);
    // IDLE beats a pending HT. The fixed HT Size must not fabricate a wrap.
    rx1.remaining=58;HAL_UARTEx_RxEventCallback(&huart1,70);
    assert(received.size()==70);
    HAL_UARTEx_RxEventCallback(&huart1,64);
    assert(received.size()==70);
    rx1.remaining=40;HAL_UARTEx_RxEventCallback(&huart1,64);
    assert(received.size()==88);
    rx1.remaining=128;HAL_UARTEx_RxEventCallback(&huart1,128);
    HAL_UARTEx_RxEventCallback(&huart1,128);
    assert(received.size()==128);
    for(unsigned i=0;i<128;++i)assert(received[i]==i);
    // Current cursor advances a little even if a delayed TC reports Size=128.
    rx1.remaining=64;HAL_UARTEx_RxEventCallback(&huart1,64);
    rx1.remaining=60;HAL_UARTEx_RxEventCallback(&huart1,128);
    assert(received.size()==196);
    for(unsigned i=0;i<68;++i)assert(received[128+i]==i);
    const auto count=received.size();
    HAL_UART_ErrorCallback(&huart1);
    rx1.remaining=50;HAL_UARTEx_RxEventCallback(&huart1,78);
    assert(received.size()==count&&bsp::UartErrors(0)==1);
    mock_uart_event_during_abort=true;
    mock_uart_abort=HAL_ERROR;
    assert(bsp::UartRecoverReceive(0)==Status::io_error);
    assert(received.size()==count);
    mock_uart_abort=HAL_OK;
    assert(bsp::UartRecoverReceive(0)==Status::ok);
    assert(received.size()==count);
    mock_uart_event_during_abort=false;
    rx1.remaining=125;HAL_UARTEx_RxEventCallback(&huart1,3);
    assert(received.size()==count+3);
    // HAL may raise an error IRQ during successful DMA start; retain its fault.
    huart6.RxState=HAL_UART_STATE_READY;
    mock_uart_error_during_start=true;
    assert(bsp::UartStartReceive(1,rx,nullptr)==Status::io_error);
    rx6.remaining=120;HAL_UARTEx_RxEventCallback(&huart6,8);
    assert(received.size()==count+3);
    mock_uart_error_during_start=false;
    assert(bsp::UartRecoverReceive(1)==Status::ok);
    // Malformed hardware/mock counter is faulted instead of underflowing size.
    rx6.remaining=129;HAL_UARTEx_RxEventCallback(&huart6,8);
    assert(bsp::UartErrors(1)==2);
    assert(bsp::UartRecoverReceive(1)==Status::ok);
    mock_ipsr=1;
    assert(bsp::UartRecoverReceive(0)==Status::invalid);
    assert(bsp::UartStartReceive(1,rx,nullptr)==Status::invalid);
    uint8_t byte{};
    assert(bsp::UartTransmit(0,&byte,1,bsp::UartMode::blocking)==Status::invalid);
    mock_ipsr=0;
    assert(bsp::UartTransmit(0,&byte,1,bsp::UartMode::dma)==Status::ok);
}
void canTests() {
    using bsp::Status;
    const auto p=bsp::Can(0);
    bsp::CanFrame frame{};frame.id=0x200;frame.size=8;
    assert(p.send(p.context,frame)==Status::not_ready);
    assert(bsp::CanAbort(0)==Status::not_ready);
    assert(bsp::CanAbort(2)==Status::invalid);
    assert(p.start(p.context,nullptr,nullptr)==Status::invalid);
    assert(p.start(p.context,canRx,nullptr)==Status::ok);
    assert((hcan1.notifications&CAN_IT_TX_MAILBOX_EMPTY)!=0);
    assert((hcan1.notifications&CAN_IT_LAST_ERROR_CODE)!=0);
    assert((hcan1.notifications&CAN_IT_BUSOFF)!=0);
    assert(p.send(p.context,frame)==Status::ok);
    assert(bsp::CanAbort(0)==Status::ok&&hcan1.aborted==7);
    const auto aborts=hcan1.abortCalls;
    HAL_CAN_ErrorCallback(&hcan1);
    assert(bsp::CanErrors(0)==1&&hcan1.abortCalls==aborts+1);
    // Error ISR aborts old mailboxes; post-submit epoch check aborts the new one.
    mock_can_error_during_send=true;
    assert(p.send(p.context,frame)==Status::io_error);
    assert(hcan1.abortCalls==aborts+3&&bsp::CanErrors(0)==2);
    mock_can_error_during_send=false;
    hcan1.error=HAL_CAN_ERROR_BOF;
    const auto sends=hcan1.sendCalls;
    assert(p.send(p.context,frame)==Status::io_error&&hcan1.sendCalls==sends);
    hcan1.error=0;hcan1.free=0;
    assert(p.send(p.context,frame)==Status::busy);
    frame.extended=true;assert(p.send(p.context,frame)==Status::invalid);
    CAN_HandleTypeDef unknown;
    HAL_CAN_ErrorCallback(&unknown);
    assert(unknown.abortCalls==0);
}
}
int main() {
    uartTests();canTests();
    std::puts("BSP host mocks passed: UART ordering/recovery, CAN notification/abort races");
}
