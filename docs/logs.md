# 配置与调试记录

只记录实际做过的操作。没有连接实板测试，不填写虚构芯片 ID、波形或转速。

## 初始检查与需求修订

- 克隆上游 `HKUSTGZ-ROBOMASTER-PNX/rm27_training_framework_hw`，起点 `f6e5a1d`。Board IOC 只有 SYS/RCC/NVIC，默认 16 MHz；不是完整驱动工程。
- 本机已安装 CubeMX、ARM GCC、CMake、STM32F4 固件和 X-CUBE-AZRTOS-F4。部分工具未加入 PATH，使用安装路径执行。
- 用户确认 RoboMaster C 型板、STM32F407IGH6；要求 Board→BSP→Module→Service 四层。此前部分 PID/电机修改仅为未验收草稿，需要去除 HAL 耦合并测试。
- 参考 `/Library/project1/project1/project1.ioc`：HSE 8 MHz，PLLM=4，PLLN=168，PLLP=2，SYSCLK=168 MHz，APB1 /8=21 MHz，APB2 /2=84 MHz。保留该分频方案，但 PLLQ 改为7得到48 MHz；参考工程的 PLLQ 输出84 MHz不能当作 USB 48 MHz 时钟。
- 参考工程 CAN1 为500 kbit/s；不照抄旧工程参数，新作业示例目标1 Mbit/s，电机最终确定后重新核对。
- 对照 RoboMaster 官方例程，修正暂定的“合法复用但板上未连接”的引脚：CAN1 PD0/PD1，CAN2 PB5/PB6；USART1 PA9/PB7，USART6 PG14/PG9；SPI1 PB3/PB4/PA7；I2C3 PA8/PC9；LED 红PH12、绿PH11、蓝PH10。CS PA4/PB0，舵机 PE9。
- 用户提供的培训网页未登录访问会重定向登录页，尚未读取其电控正文；凭据不写入工程。公开官方资料与本地 IOC 是目前配置依据。
- 首次 CubeMX `-h` 探测打开 GUI 而不是帮助，已停止该次进程；之后确认 `-q` 脚本模式可运行。代码生成结果以随后记录为准。

## 本机验证结果（2026-10-05）

- CubeMX6.18.1通过脚本真正重新生成Board；生成期间有数据库RIF/DMA导入警告，最终generate返回OK。逐项检查生成代码：官方引脚、四路DMA、UART、CAN、TIM1与HAL TIM6配置均保留。早期相对project path导致嵌套目录/生成异常，改为解析绝对IOC路径后成功。
- 逐层交叉编译通过：Board-only，Board+BSP，Board+BSP+Module，以及ThreadX完整Service；ARM GNU13.3.Rel1、CMake4.4.3。模块未被服务引用时会被`--gc-sections`丢弃，不能把小镜像当作模块功能已运行。
- 最终Debug、alive+imu、control关闭：FLASH **48684 B**，RAM **13712 B**，CCMRAM 0；RAM统计含链接器保留主栈/堆。`nm`确认ThreadX PendSV、SysTick、TIM6、应用初始化入口已链接；未链接control入口、malloc或sbrk。
- control开启的编译检查通过：FLASH64116 B、RAM17392 B。硬件确认仍false、电流限幅0、PID0，没有因开关编译测试而启用真实驱动。最终本地镜像已切回control关闭。
- CTest **5/5通过**：PID、BMI088/LED、电机、Service、BSP。相同五组在ASan+UBSan下**5/5通过**。Service测试用假ThreadX执行真实控制逻辑，包含47个脚本步骤；不验证Cortex-M上下文切换、电气信号或实际线程时序。
- 审查并回归修复：TIM6必须抢占ThreadX空闲PendSV，故优先级改14并重新生成；反馈故障epoch及命令epoch防止普通目标刷新重新驱动；mutex暂忙时沿用原命令但不延长租期；心跳put/get与待消费位在短临界区同步，防止积压；健康快照检查监控与生产者时间。
- BSP回归：UART根据实时DMA NDTR游标而非可能乱序的HT/TC/IDLE回调Size消费数据；首次启动失败可恢复；CAN启用发送错误通知，错误和松开请求中止待发邮箱；SPI超时后禁用、清理并重新初始化控制器，不吞掉原错误。
- UART限制：128字节环形缓冲，115200/8N1下IRQ响应加回调复制必须小于半缓冲约5.56ms；NDTR不能发现漏掉的完整一圈，必须实测延迟并用上层序号/CRC识别丢帧。CAN中止不能撤回已经在线上传输的帧；硬件急停和电调自身超时仍必需。
- 厂商I2C文件与本机FW_F4_V1.28.3逐字节一致，原始差异是换行符；不手改HAL。CubeMX只复制所用库，删去的其它型号/外设模板不是功能代码丢失。保留许可证，区分生成代码空白与手写代码格式检查。
- 用户指定提交仓库`elin277277/project1`，已有初始提交，仅含gitignore。ST-LINK尚未连接。GitHub HTTPS密码认证不可用于推送，采用官方CLI浏览器授权，不保存聊天中提供的凭据。

## 尚待实板验证

- 下载、复位启动、HSE 频率、三色 LED、舵机 PWM。
- 两路 UART/DMA 收发与空闲中断；两路 CAN 实际波特率、终端电阻和错误恢复。
- BMI088 ID、量程寄存器、正负轴数据、温度及断线错误；I²C 实际 ACK/上拉。
- ThreadX 心跳、线程阻塞与错误灯效、最大栈占用。
- 电机型号、电调与接线确定后，按 [替换清单](motor_changes.md) 低电流验证松开、速度环和双环；没有任何已整定 PID 参数。

## 实板调试（2026-10-14，ST-LINK/V2 + OpenOCD telnet 直连）

以下均为真实硬件实测，工具：OpenOCD 0.12.0（telnet 50002）、arm-none-eabi-gdb/addr2line。

### 旧 C 板：HardFault，判定为 SRAM 硬件故障

- 现象：固件下载校验通过后，运行约 10 秒内必进 `HardFault_Handler`，三种不同崩溃位置（`tx_mutex_get`、`BMI088::Config`、`BMI088::Exchange`），无固定规律。
- 关键证据：复位后**尚未运行**，读 IMU 栈地址 `0x20001808` 即得 `0x28001954`（应为 0 或 ThreadX 填充 0xEFEFEFEF）。所有被破坏的指针都是 `0x20xxxxxx` 变成 `0x28xxxxxx`，即固定 `+0x08000000`（第 27 位置位）。
- CFSR=0x8200（精确总线错误），BFAR=0x28001800（非法地址）。
- 排除项：栈溢出（IMU 栈 3072 B，实测用量约 71 B）；数组越界（BMI088 `size+prefix≤8`）；旧 ELF（重新编译下载后仍复现）。
- 结论：**该板 SRAM 在 0x20001808 附近有物理故障位**，软件无法修复，更换 C 板。

### 新 C 板：BMI088 全部正常

- CFSR=0，无 HardFault；`imu_sample.sequence` 持续增长（3 秒 +908）。
- 加速度 `(0.18, -0.01, 9.87)` m/s²，模长 9.87（≈1g）；板子翻转 180° 后 Z 由 +9.86 变 -9.68，轴向正确；静止陀螺 ≈0；温度 23.25 °C。
- 之前旧板上“accel=(-1.088,0,0)、Y/Z 恒 0”的异常，证实也是 SRAM 故障导致的数据损坏，非驱动 bug。

### CAN（M2006+C610，ID1=0x201）：物理层故障，反馈收不到

- 环回模式（`CAN_MODE_LOOPBACK`）自发自收，ESR=0 → **C 板 CAN 控制器+收发器正常**。
- 正常模式接电调：ESR=0xff180023（REC=255 溢出、LEC=ACK/格式错误、被动错误态），接收 FIFO 挂号数恒 0；MSR bit RXM=1（总线上**有信号**、控制器在尝试接收），但每帧都判错误丢弃。
- 已排除：波特率（1 Mbit/s 正确，试 500 kbit/s 现象相同）；CANH/CANL 接反（对调无变化）；共地（已连）；电调上电（指示灯亮、总线有活动）；ID（0x201 匹配）。
- 框架行为正确：收不到反馈 → `AliveCheck()=OFFLINE` → 控制线程每周期 `revoke()` 松开电机，输出恒 0，电机不转。**这是故障闭锁设计，不是 bug，不应绕过。**
- 待查（下次接板）：**断电用万用表量 CANH-CANL 电阻**——≈60 Ω 为两端各 120 Ω 正常；≈120 Ω 为单端；开路为无终端（最可疑，信号反射导致全帧畸变）。其次换短双绞线、检查端子压接。

### 本次结论

- 旧板问题 = SRAM 硬件故障（已换板解决）；代码、ThreadX 配置、BMI088 驱动均正常。
- 电机调不通 = CAN 物理层信号完整性问题（终端电阻/线缆最可疑），与软件无关。
- 调试期间一度把 `motor_current_limit=20/speed_kp=5` 编进固件（无反馈时电机实际也不会转）；排查结束后已恢复安全默认（`hardware_confirmed=false`、`current_limit=0`、PID=0），工作区干净。
