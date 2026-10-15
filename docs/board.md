# Board：引脚接线表

这块板子是 RoboMaster C 型开发板，主控 STM32F407IGH6。这里只记每个外设接到哪个引脚，接线的时候对着看。

引脚是按 RoboMaster 官方例程 [Development-Board-C-Examples](<https://github.com/RoboMaster/Development-Board-C-Examples>) 核对的。注意：芯片支持某个功能不代表板上真的把线布过去了，所以要以官方例程为准。

| 外设 | 引脚 | 备注 |
| --- | --- | --- |
| RGB LED | 红 PH12、绿 PH11、蓝 PH10 | 推挽输出，低电平点亮，初始先灭 |
| BMI088 (SPI1) | SCK PB3、MISO PB4、MOSI PA7 | mode 0、8 位、高位先发、软件 NSS |
| BMI088 片选 | 加速度计 CS PA4、陀螺仪 CS PB0 | 推挽输出，初始拉高（不选中） |
| I2C3 | SCL PA8、SDA PC9 | 400 kHz，开漏，靠外部上拉 |
| CAN1 | RX PD0、TX PD1 | 1 Mbit/s，电机用这条 |
| CAN2 | RX PB5、TX PB6 | 1 Mbit/s |
| USART1 | TX PA9、RX PB7 | 115200 8N1 |
| USART6 | TX PG14、RX PG9 | 115200 8N1 |
| 舵机 TIM1_CH1 | PE9 | 50 Hz PWM，上电默认不出波形 |
| SWD 调试 | SWDIO PA13、SWCLK PA14 | 烧录/调试口 |

几个容易搞错的地方：

- SPI 的 PB3/PB4 同时也是 JTAG 的脚，所以调试只能用 SWD，不能用 JTAG。
- USART1 的 TX 是 PA9 不是 PB6——PB6 其实是 CAN2 的 TX，别接错。
- LED 是低电平点亮（引脚拉低灯才亮），别写反了。
