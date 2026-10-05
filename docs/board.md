# Board：RoboMaster C 型开发板

## 配置来源与边界

目标为 STM32F407IGH6（UFBGA176）。从仓库原有空 [board.ioc](<../board/board.ioc>) 配置起步，不复制旧工程。仅参考用户指定 [project1.ioc](</Library/project1/project1/project1.ioc>) 的 RCC 时钟树；其 CAN1 PB8/PB9 等外设映射不沿用。

引脚依据 RoboMaster 官方 [Development-Board-C-Examples](<https://github.com/RoboMaster/Development-Board-C-Examples>) 核对，而非只验证 MCU 是否支持该复用功能。MCU 可复用不代表板上已布线。以下官方映射已修正最初的暂定引脚：

| 外设 | 本工程采用的官方 C 板引脚 | 配置 |
| --- | --- | --- |
| RGB LED | PH12 红、PH11 绿、PH10 蓝 | `LED_RED/GREEN/BLUE`，推挽输出，低有效，初始化高（灭） |
| BMI088 SPI1 | PB3 SCK、PB4 MISO、PA7 MOSI | mode 0、8-bit、MSB first、软件 NSS、APB2 /32 = 2.625 MHz |
| BMI088 CS | PA4 ACC、PB0 GYRO | `BMI088_ACC_CS/BMI088_GYRO_CS`，推挽输出，初始化高（不选中） |
| I2C3 | PA8 SCL、PC9 SDA | 400 kHz Fast mode，开漏、无内部上拉，依赖外部上拉 |
| CAN1 | PD0 RX、PD1 TX | 1 Mbit/s，Normal mode |
| CAN2 | PB5 RX、PB6 TX | 1 Mbit/s，Normal mode |
| USART1 | PA9 TX、PB7 RX | 115200、8N1、无流控 |
| USART6 | PG14 TX、PG9 RX | 115200、8N1、无流控 |
| 舵机 TIM1_CH1 | PE9 | 50 Hz，PSC=167、ARR=19999、CCR1=0；不自动启动 PWM |
| SWD | PA13 SWDIO、PA14 SWCLK | Serial Wire；PB3/PB4 留给 SPI1，不使用 JTAG/SWO |
| HAL timebase | TIM6 | 1 ms HAL tick；为后续 ThreadX 留出 SysTick |

官方核对链接：[LED 标签](<https://github.com/RoboMaster/Development-Board-C-Examples/blob/master/1.light_led/Inc/main.h>)、[SPI 引脚](<https://github.com/RoboMaster/Development-Board-C-Examples/blob/master/13.spi_bmi088/Src/spi.c>)、[BMI088 片选](<https://github.com/RoboMaster/Development-Board-C-Examples/blob/master/13.spi_bmi088/Inc/main.h>)、[I2C](<https://github.com/RoboMaster/Development-Board-C-Examples/blob/master/11.ist8310/Src/i2c.c>)、[CAN](<https://github.com/RoboMaster/Development-Board-C-Examples/blob/master/14.CAN/Src/can.c>)、[UART](<https://github.com/RoboMaster/Development-Board-C-Examples/blob/master/8.USART_receive_and_send/Src/usart.c>)、[舵机](<https://github.com/RoboMaster/Development-Board-C-Examples/blob/master/5.servo_motor/Src/tim.c>)。只参考引脚；速率、SPI mode 与默认安全状态按本工程需求配置。

不采用暂定的 SPI PA5/PA6、I2C PH7/PH8、CAN2 PB12/PB13 或 USART1 PB6 TX。PB6 实际用于 CAN2 TX。红/蓝标签不能互换。

## 时钟与时序推导

- HSE = 8 MHz；PLLM=4 → VCO 输入 2 MHz；PLLN=168 → VCO=336 MHz。
- PLLP=2 → SYSCLK/HCLK = 168 MHz；PLLQ=7 → 48 MHz（修正参考工程的 84 MHz 辅助域）。
- APB1 = HCLK/8 = 21 MHz；APB1 timer = 42 MHz。
- APB2 = HCLK/2 = 84 MHz；APB2 timer = 168 MHz。
- CAN1/2 用 APB1 21 MHz，不是 timer 时钟。Prescaler=1、SJW=1、BS1=16、BS2=4：`21 MHz / (1 × (1+16+4)) = 1 MHz`；采样点 `17/21 ≈ 80.95%`。bxCAN BS1 最大为 16，不能填 17。
- SPI1 = APB2/32 = 2.625 MHz。
- TIM1 = 168 MHz/(167+1) = 1 MHz，周期 (19999+1) µs = 20 ms = 50 Hz。CCR 单位为 µs；初值 0 且不开启通道，不输出舵机控制脉冲。

## DMA、NVIC 与启动约束

| 请求 | DMA stream / channel | 模式 |
| --- | --- | --- |
| USART1_RX | DMA2 Stream2 / Channel4 | Circular，byte，memory increment，High |
| USART1_TX | DMA2 Stream7 / Channel4 | Normal，byte，memory increment，Low |
| USART6_RX | DMA2 Stream1 / Channel5 | Circular，byte，memory increment，High |
| USART6_TX | DMA2 Stream6 / Channel5 | Normal，byte，memory increment，Low |

DMA2 时钟及 IRQ 初始化在 UART 初始化之前。UART 与 DMA IRQ 优先级 6；CAN RX0/TX/SCE 优先级 5；TIM6 HAL tick 优先级 14（必须能抢占优先级15的ThreadX PendSV空闲循环）；NVIC priority group 4。RX HT/TC 和 UART IRQ 留给后续 BSP 的 ReceiveToIdle DMA 处理；Board 不启动收发。CAN 不配置过滤器、不 Start、不使能应用通知、不发送电机帧；自动 bus-off 恢复、自动重发默认禁用。

主栈保留 0x1000，heap 保留 0x400；后续 Service 应使用静态对象，不因此启用动态分配。DMA buffer 应位于可被 DMA 访问的 SRAM，不能放入 CCMRAM。

## 重新生成

本机 STM32CubeMX 6.18.1、STM32Cube FW_F4 V1.28.3。从项目根目录执行：

```sh
bash board/generate.sh
```

[generate.sh](<../board/generate.sh>) 自动解析 IOC 的绝对路径，并将临时命令与日志放到忽略的构建目录。命令为 `config load`、`project toolchain CMake`、`project generate`、`exit`。不要用 `project path .`：本次尝试曾产生嵌套目录并使 CMake 生成异常。可用环境变量 JAVA/CUBEMX 指定其他安装位置。固件包来自本机 STM32Cube Repository，仅复制使用的 HAL 文件。手写扩展置于 USER CODE 或独立文件；重新生成后重新编译并检查 main 入口、SysTick 调用及 PendSV 重命名仍保留。

## 最小交叉编译

本机使用 CMake 4.4.3、Arm GNU 13.3.Rel1；未安装 Ninja，使用 Unix Makefiles。以下命令从项目根目录执行：

```sh
export PATH="/Applications/ArmGNUToolchain/13.3.rel1/arm-none-eabi/bin:$PATH"
CMAKE=/Applications/CMake.app/Contents/bin/cmake
"$CMAKE" -S board -B board/build-minimal -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Debug -DBOARD_BUILD_APPLICATION=OFF
"$CMAKE" --build board/build-minimal --parallel 4
arm-none-eabi-size board/build-minimal/board.elf
```

已实际通过 CubeMX 生成与最小交叉编译：初次最小 Board FLASH=21548 B、RAM=6096 B（含保留主栈/堆）、CCMRAM=0。随后已通过 BSP、Module 和 ThreadX 服务构建；详细状态见 [logs.md](logs.md)。RTOS审查后将TIM6优先级从15改为14并再次实际生成，确认USER CODE入口仍保留。未声明下载、真实波形、外设通信或电机/舵机运行通过。
