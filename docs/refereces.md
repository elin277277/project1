# 参考资料与使用边界

保留原文件名中的 `refereces` 拼写。以下优先列厂商、项目维护者或教材作者的一手来源，供作业推导和实现时查阅；**资料来源可靠不等于本地硬件已经逐项验证**。未据此声称完成下载运行、芯片 ID 读取、波形测量、校准或闭环调参。

## 底盘运动学

| 来源 | 阅读重点 |
| --- | --- |
| [Modern Robotics，§13.2 全向移动机器人](<https://modernrobotics.northwestern.edu/nu-gm-book-resource/13-2-omnidirectional-wheeled-mobile-robots-part-1-of-2/>) | 教材作者给出的主轮驱动与辊子被动运动分解、速度矩阵和满秩条件。先检查符号顺序，再对照公式。 |
| [WPILib：Mecanum Drive Kinematics](<https://docs.wpilib.org/en/stable/docs/software/kinematics-and-odometry/mecanum-drive-kinematics.html>) | x 向前、y 向左；FL/FR/RL/RR 轮序及车体速度与轮速的转换。几何尺寸应采用实际轮心位置。 |
| [WPILib：Swerve Drive Kinematics](<https://docs.wpilib.org/en/stable/docs/software/kinematics-and-odometry/swerve-drive-kinematics.html>) | 舵轮线速度/角度定义，以及转向超过 90° 时翻转速度以减少转向的等价状态。 |

以上网页已通过 `web_fetch` 获取（HTTP 200）。学习结论应由坐标、接地约束和量纲重新推导，不应把库的矩阵直接复制为任意轮型、安装方向都适用的公式。

## 芯片、总线与开发板

| 一手资料 | 用途与核验范围 |
| --- | --- |
| [Bosch BMI088 官方产品页](<https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi088/>)；[BMI088 数据手册](<https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi088-ds001.pdf>) | 寄存器、供电、量程、灵敏度和 SPI/I²C 时序应以数据手册为准。产品页获取成功；PDF 请求返回 `application/pdf`，本次文本抓取工具不支持解析，未逐页核验。 |
| [Bosch BMI08x 官方 SensorAPI](<https://github.com/boschsensortec/BMI08x_SensorAPI>)；[官方说明原文](<https://raw.githubusercontent.com/boschsensortec/BMI08x_SensorAPI/master/README.md>) | 官方驱动实现的参考入口；说明确认包含 BMI085/BMI088 支持。仓库页与说明获取成功，不能据此宣称全部代码或本地初始化流程已逐行核对。 |
| [ST STM32F407IG 官方产品页](<https://www.st.com/en/microcontrollers-microprocessors/stm32f407ig.html>)；[RM0090 参考手册](<https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>) | 查 RCC 时钟树、GPIO、SPI、I²C、bxCAN、DMA 和中断等外设行为。产品页获取成功；手册 PDF 返回 `application/pdf`，本次未解析正文。还需按确切型号核对数据手册、勘误与板级原理图，不能由 MCU 手册推断板上接线。 |
| [NXP UM10204：I²C-bus specification and user manual](<https://www.nxp.com/docs/en/user-guide/UM10204.pdf>) | 查 START/STOP、地址、ACK/NACK、开漏与上拉、时序和时钟延展。官方 PDF 请求返回 `application/pdf`，本次未解析正文；具体器件支持范围仍须查该器件手册。 |
| [RoboMaster C 型开发板官方产品页](<https://www.robomaster.com/en-US/products/components/general/development-board-type-c>)；[官方 C 板示例仓库](<https://github.com/RoboMaster/Development-Board-C-Examples>) | 产品页与仓库页获取成功；产品页列出接口、供电范围与 SWD/DFU 下载说明。示例可辅助定位板级配置，但不替代对应硬件版本的原理图与用户手册；本次没有逐文件核对示例。 |

## 查阅到工程验证的顺序

1. 记录使用资料的版本、芯片型号和开发板版本；网页或默认分支会更新，实际实现时应固定驱动版本。
2. 将资料结论变成明确检查项：引脚/片选、上电时序、协议长度与字节序、量程、单位、超时和失效处理；区分数据手册规定与板级连接。
3. 区分“公式自检”“本机编译或模拟测试”“待硬件实测”。本索引没有证明实际传感器、电机或电调型号，M2006/C610 只能作为协议示例，不能据此启用未知电机。
4. PDF 链接虽为官方资源，但本次未解析正文，不在笔记中编造其页码、修订号或逐页阅读结论；引用具体寄存器、延时或电气限值前应人工打开核对。
