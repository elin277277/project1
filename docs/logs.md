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
