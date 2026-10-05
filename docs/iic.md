# I²C：地址、ACK 与共享总线

I²C 用 SCL 时钟和 SDA 双向数据两根线通信。SDA 分时由主机或从机驱动，通常按半双工理解；不同于四线 SPI 同时移入和移出。两线均为开漏，通过外部上拉获得高电平，不允许把高电平强推到总线上。主机产生时钟；从机可能拉低 SCL 延长等待（clock stretching）。

SCL高时SDA下降为START，上升为STOP；其余数据通常在SCL低时变化、高时采样。每字节8位后有第9位ACK/NACK。寄存器读取常见流程：START → 地址+W → 寄存器号 → repeated START → 地址+R → 数据 → 最后字节NACK → STOP。NACK既可能表示设备不存在，也可能是接收方正常结束读取，需结合所在阶段判断。

## C 板配置

- I2C3：PA8 SCL、PC9 SDA，400 kHz、7位地址、开漏、外部上拉。
- PCLK1使用参考工程的21 MHz，不能直接复用42 MHz下手算的时序常数；由CubeMX/HAL按实际时钟设置。
- BSP接口接收未移位7位地址；只有STM32 HAL适配处左移1位，避免重复移位。IST8310常用7位地址0x0e，HAL参数为0x1c；最终以手册和板卡连接为准。
- IST8310为选做，当前仅提供寄存器访问端口，不宣称已完成其复位、初始化或磁场读取。

## 为什么不能只“开一个 I²C”

上拉过弱会导致总线电容充电慢，过强则使低电平灌电流过大。选值需检查上升时间与器件电流，近似 `tr≈0.8473*Rp*Cb`，400 kHz Fast-mode要求上升时间不超过300 ns。没有测量电容与波形时，不凭空断言某个阻值必然适用。

调试依次检查供电、共同地、两线空闲高 → 地址ACK → repeated START及数据 → 拔除设备返回有界超时。不要无限重试；SDA长期被拉低需要分析复位/总线恢复，而不是简单改成更长等待。当前没有实板波形或ACK记录。

资料：[NXP UM10204](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)、[C板IST8310例程](https://github.com/RoboMaster/Development-Board-C-Examples/tree/master/11.ist8310)。
