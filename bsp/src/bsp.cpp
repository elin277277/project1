#include "bsp.hpp"
#include "main.h"
#include "spi.h"
#include "i2c.h"

namespace {
using bsp::Status;
Status result(HAL_StatusTypeDef s) { return s==HAL_OK?Status::ok:s==HAL_BUSY?Status::busy:s==HAL_TIMEOUT?Status::timeout:Status::io_error; }
struct Pin { GPIO_TypeDef *port; uint16_t pin; };
Pin acc{BMI088_ACC_CS_GPIO_Port,BMI088_ACC_CS_Pin},gyro{BMI088_GYRO_CS_GPIO_Port,BMI088_GYRO_CS_Pin};
Pin red{LED_RED_GPIO_Port,LED_RED_Pin},green{LED_GREEN_GPIO_Port,LED_GREEN_Pin},blue{LED_BLUE_GPIO_Port,LED_BLUE_Pin};
void writePin(void *ctx,bool high) { auto &p=*static_cast<Pin *>(ctx); HAL_GPIO_WritePin(p.port,p.pin,high?GPIO_PIN_SET:GPIO_PIN_RESET); }
uint32_t now(void *) { return HAL_GetTick(); }
void delay(void *,uint32_t ms) { HAL_Delay(ms); }
Status exchange(void *ctx,const uint8_t *tx,uint8_t *rx,size_t size,uint32_t timeout) {
    if(!tx||!rx||!size||size>65535||!timeout)return Status::invalid;
    auto *h=static_cast<SPI_HandleTypeDef *>(ctx);
    if(!h || HAL_SPI_GetState(h)==HAL_SPI_STATE_RESET)return Status::not_ready;
    const auto status=HAL_SPI_TransmitReceive(h,const_cast<uint8_t *>(tx),rx,static_cast<uint16_t>(size),timeout);
    if(status==HAL_ERROR||status==HAL_TIMEOUT) {
        // Polling HAL may return READY with a pending RX byte. Quiesce and drain
        // before a later explicit device reinitialization; never hide this failure.
        __HAL_SPI_DISABLE(h);
        if(HAL_SPI_Abort(h)!=HAL_OK || HAL_SPI_Init(h)!=HAL_OK)h->State=HAL_SPI_STATE_RESET;
    }
    return result(status);
}
Status readReg(void *ctx,uint8_t address,uint8_t reg,uint8_t *data,size_t size,uint32_t timeout) {
    if(address>0x7f||!data||!size||size>65535||!timeout)return Status::invalid;
    auto *h=static_cast<I2C_HandleTypeDef *>(ctx);
    if(!h||HAL_I2C_GetState(h)==HAL_I2C_STATE_RESET)return Status::not_ready;
    return result(HAL_I2C_Mem_Read(h,static_cast<uint16_t>(address<<1),reg,I2C_MEMADD_SIZE_8BIT,data,static_cast<uint16_t>(size),timeout));
}
Status writeReg(void *ctx,uint8_t address,uint8_t reg,const uint8_t *data,size_t size,uint32_t timeout) {
    if(address>0x7f||!data||!size||size>65535||!timeout)return Status::invalid;
    auto *h=static_cast<I2C_HandleTypeDef *>(ctx);
    if(!h||HAL_I2C_GetState(h)==HAL_I2C_STATE_RESET)return Status::not_ready;
    return result(HAL_I2C_Mem_Write(h,static_cast<uint16_t>(address<<1),reg,I2C_MEMADD_SIZE_8BIT,const_cast<uint8_t *>(data),static_cast<uint16_t>(size),timeout));
}
}
namespace bsp {
Clock SystemClock() { return {nullptr,now,delay}; }
SpiDevice ImuAccelPort() { return {&hspi1,exchange,{&acc,writePin}}; }
SpiDevice ImuGyroPort() { return {&hspi1,exchange,{&gyro,writePin}}; }
DigitalOut LedRed() { return {&red,writePin}; }
DigitalOut LedGreen() { return {&green,writePin}; }
DigitalOut LedBlue() { return {&blue,writePin}; }
I2cPort MagnetometerPort() { return {&hi2c3,readReg,writeReg}; }
}
