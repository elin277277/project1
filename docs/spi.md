# SPI：从电气时序到 BMI088 事务

全双工是收发可同时进行，半双工是在共享通道上分时改变方向。SPI 常见四线制由 SCK、MOSI、MISO、CS 组成；每个时钟同时移出与移入一位，因此“只读”也必须发送 dummy 产生时钟。三线共享数据线的 SPI 可以是半双工，不能认为所有 SPI 都是全双工。

SPI 没有统一设备地址或 ACK，主机通过片选选择从机；正确收到字节不代表正确设备，应校验芯片 ID、配置回读及状态。CPOL 决定空闲时钟电平，CPHA 决定采样边沿；主从必须匹配。

## C 板配置

- SPI1：PB3 SCK、PB4 MISO、PA7 MOSI；这是板上实际接线，不是随便选 PA5/PA6 的合法复用。
- 8 bit、MSB first、全双工主机、软件片选、Mode 0（CPOL=0、CPHA=0）。PCLK2=84 MHz，/32 得2.625 MHz，低于 BMI088 的10 MHz上限。
- 加速度计 CS=PA4，陀螺仪 CS=PB0；上电配置推挽高电平，事务期间仅拉低一个。
- 保留 SWD，关闭完整 JTAG 以释放 PB3/PB4。接线与时钟来源见 [Board记录](board.md)。

## 易错点

加速度计上电/软复位后需一次 CS 事务切入 SPI，此次读数丢弃。之后每次加速度计读取时，地址后还要丢弃 **一个 dummy 字节**；陀螺仪读取没有该额外字节。以加速度 X/Y/Z 为例，整个 CS 拉低期间发送 `0x12|0x80`、一个 dummy、六个 dummy 时钟字节，取最后六字节。不能每个字节都切换 CS。

写寄存器使用 `reg & 0x7f` 加数据；复位/电源模式切换后按手册等待，再检查 ID 与配置回读。加速度小端有符号16位；±6g 时比例 `6*9.80665/32768 m/s²/LSB`，陀螺仪±2000°/s时比例 `2000*pi/180/32768 rad/s/LSB`。温度为带符号11位，`23 + raw*0.125 °C`，不是普通16位。

## 验证顺序（待实板）

先看空闲高片选和 SCK 时序 → 读加速度 ID 0x1e、陀螺仪 ID 0x0f → 回读量程/采样配置 → 静止加速度模长约g、转动角速度符号合理 → 断线或错误 ID 必须返回错误，不发布上一帧为新有效数据。主机协议模拟不等于这些硬件检查已通过。

资料：[Bosch官方驱动](https://github.com/boschsensortec/BMI08x_SensorAPI)、[官方数据手册](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi088-ds001.pdf)、[C板SPI例程](https://github.com/RoboMaster/Development-Board-C-Examples/tree/master/13.spi_bmi088)。
