# 参考资料

（文件名里的 `refereces` 拼写沿用了原来的，不改了。）

这里收的是官方手册、教材这类靠谱的一手资料。但要注意：**资料是对的，不代表我的板子就验证过了**，最后还是要以实测为准。

## 底盘运动学

| 资料 | 主要看什么 |
| --- | --- |
| [Modern Robotics §13.2 全向轮机器人](<https://modernrobotics.northwestern.edu/nu-gm-book-resource/13-2-omnidirectional-wheeled-mobile-robots-part-1-of-2/>) | 麦克纳姆轮的速度分解、速度矩阵。看的时候先搞清楚坐标和轮子顺序再套公式。 |
| [WPILib：Mecanum 运动学](<https://docs.wpilib.org/en/stable/docs/software/kinematics-and-odometry/mecanum-drive-kinematics.html>) | x 向前、y 向左的约定，车体速度和四个轮速怎么换算。 |
| [WPILib：Swerve 运动学](<https://docs.wpilib.org/en/stable/docs/software/kinematics-and-odometry/swerve-drive-kinematics.html>) | 舵轮的速度和角度定义。 |

学这些要自己从坐标和几何推一遍，别直接把库里的矩阵抄过来就当万能公式用。

## 芯片和板子

| 资料 | 用途 |
| --- | --- |
| [BMI088 产品页](<https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi088/>) 和 [数据手册](<https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi088-ds001.pdf>) | 寄存器、量程、灵敏度、SPI 时序都以手册为准。 |
| [Bosch 官方驱动 BMI08x_SensorAPI](<https://github.com/boschsensortec/BMI08x_SensorAPI>) | 官方驱动怎么写的可以参考。 |
| [STM32F407IG 产品页](<https://www.st.com/en/microcontrollers-microprocessors/stm32f407ig.html>) 和 [RM0090 参考手册](<https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>) | 查时钟树、GPIO、SPI、I²C、CAN、DMA、中断这些外设怎么用。 |
| [NXP I²C 手册 UM10204](<https://www.nxp.com/docs/en/user-guide/UM10204.pdf>) | 查 START/STOP、地址、ACK、开漏上拉这些。 |
| [RoboMaster C 板产品页](<https://www.robomaster.com/en-US/products/components/general/development-board-type-c>) 和 [官方例程](<https://github.com/RoboMaster/Development-Board-C-Examples>) | 查板子的接口和引脚接法。 |

## 用这些资料的正确姿势

1. 记一下用的资料版本和芯片、板子型号，网页会更新，别用错版本。
2. 把看来的结论变成能检查的项：引脚、时序、字节序、量程、单位、超时，一条条对。
3. 分清三种状态：「公式自己推过」「电脑上编译/模拟过」「真板上量过」。本笔记里 M2006/C610 只是协议示例，不能拿它当任何电机都能用。
