#pragma once
#include <cstdint>
#include <cstring>

enum HAL_StatusTypeDef { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT };
inline uint32_t mock_ipsr{};
inline uint32_t __get_IPSR() { return mock_ipsr; }
inline uint32_t HAL_GetTick() { return 123; }
constexpr uint32_t DMA_NORMAL=0, DMA_CIRCULAR=1;
constexpr uint32_t HAL_UART_STATE_RESET=0, HAL_UART_STATE_READY=1, HAL_UART_STATE_BUSY_RX=2;
struct DMA_HandleTypeDef { struct { uint32_t Mode=DMA_CIRCULAR; } Init; uint32_t remaining=128; };
struct UART_HandleTypeDef {
    uint32_t RxState=HAL_UART_STATE_RESET, gState=HAL_UART_STATE_RESET;
    DMA_HandleTypeDef *hdmarx{}, *hdmatx{};
    uint8_t *buffer{};
};
extern UART_HandleTypeDef huart1,huart6;
#define __HAL_DMA_GET_COUNTER(h) ((h)->remaining)
extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *,uint16_t);
extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
inline HAL_StatusTypeDef mock_uart_start=HAL_OK, mock_uart_abort=HAL_OK;
inline bool mock_uart_error_during_start{}, mock_uart_event_during_abort{};
inline unsigned mock_uart_starts{}, mock_uart_aborts{};
inline HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h,uint8_t *p,uint16_t n) {
    ++mock_uart_starts;
    if(mock_uart_start!=HAL_OK)return mock_uart_start;
    h->buffer=p;h->hdmarx->remaining=n;h->RxState=HAL_UART_STATE_BUSY_RX;
    if(mock_uart_error_during_start)HAL_UART_ErrorCallback(h);
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) {
    ++mock_uart_aborts;
    if(mock_uart_event_during_abort)HAL_UARTEx_RxEventCallback(h,64);
    if(mock_uart_abort==HAL_OK)h->RxState=HAL_UART_STATE_READY;
    return mock_uart_abort;
}
inline HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *,uint8_t *,uint16_t,uint32_t) {return HAL_OK;}
inline HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *,uint8_t *,uint16_t) {return HAL_OK;}
inline HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *,uint8_t *,uint16_t) {return HAL_OK;}

constexpr uint32_t HAL_CAN_STATE_RESET=0,HAL_CAN_STATE_READY=1,HAL_CAN_STATE_LISTENING=2;
constexpr uint32_t HAL_CAN_ERROR_BOF=1;
constexpr uint32_t CAN_TX_MAILBOX0=1,CAN_TX_MAILBOX1=2,CAN_TX_MAILBOX2=4;
constexpr uint32_t CAN_ID_STD=0,CAN_RTR_DATA=0,CAN_FILTERMODE_IDMASK=0,CAN_FILTERSCALE_32BIT=0;
constexpr uint32_t CAN_FILTER_FIFO0=0,CAN_RX_FIFO0=0,ENABLE=1;
constexpr uint32_t CAN_IT_RX_FIFO0_MSG_PENDING=1,CAN_IT_RX_FIFO0_OVERRUN=2,CAN_IT_BUSOFF=4,
    CAN_IT_ERROR=8,CAN_IT_TX_MAILBOX_EMPTY=16,CAN_IT_LAST_ERROR_CODE=32,
    CAN_IT_ERROR_WARNING=64,CAN_IT_ERROR_PASSIVE=128;
struct CAN_HandleTypeDef {
    uint32_t state=HAL_CAN_STATE_READY,error{},free=3,notifications{},aborted{};
    unsigned abortCalls{},sendCalls{};
};
struct CAN_TxHeaderTypeDef { uint32_t StdId{},IDE{},RTR{},DLC{}; };
struct CAN_RxHeaderTypeDef { uint32_t StdId{},ExtId{},IDE{},RTR{},DLC{}; };
struct CAN_FilterTypeDef {
    uint32_t FilterBank{},SlaveStartFilterBank{},FilterMode{},FilterScale{},
        FilterFIFOAssignment{},FilterActivation{};
};
extern CAN_HandleTypeDef hcan1,hcan2;
extern "C" void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *);
inline bool mock_can_error_during_send{};
inline uint32_t HAL_CAN_GetState(CAN_HandleTypeDef *h) {return h->state;}
inline uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *h) {return h->error;}
inline uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) {return h->free;}
inline HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h,CAN_TxHeaderTypeDef *,uint8_t *,uint32_t *m) {
    ++h->sendCalls;
    if(mock_can_error_during_send)HAL_CAN_ErrorCallback(h);
    *m=CAN_TX_MAILBOX0;--h->free;return HAL_OK;
}
inline HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h,uint32_t m) {
    ++h->abortCalls;h->aborted|=m;return HAL_OK;
}
inline HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *,CAN_FilterTypeDef *) {return HAL_OK;}
inline HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h) {h->state=HAL_CAN_STATE_LISTENING;return HAL_OK;}
inline HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *h) {h->state=HAL_CAN_STATE_READY;return HAL_OK;}
inline HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h,uint32_t n) {h->notifications=n;return HAL_OK;}
inline uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *,uint32_t) {return 0;}
inline HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *,uint32_t,CAN_RxHeaderTypeDef *,uint8_t *) {return HAL_ERROR;}
