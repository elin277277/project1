# 构建与验证

工程入口是 `board/`，不是仓库根目录。需要 ARM GNU 工具链、CMake≥3.22 与 Make（或自行选择 Ninja）。macOS 当前安装可用：

```sh
export PATH="/Applications/ArmGNUToolchain/13.3.rel1/arm-none-eabi/bin:/Applications/CMake.app/Contents/bin:$PATH"
```

## 逐层构建

下面从仓库根目录执行。第一次配置指定工具链；更换工具链时换构建目录。

```sh
# Board：只有外设初始化，不启动通信或执行器
cmake -S board -B board/build-minimal -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build board/build-minimal -j4

# BSP：编译 HAL 适配
cmake -S board -B board/build-bsp -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DBOARD_BUILD_APPLICATION=ON
cmake --build board/build-bsp -j4

# Module：编译 PID、BMI088、M2006 与 LED；未被调用的段可被链接器丢弃
cmake -S board -B board/build-modules -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DBOARD_BUILD_APPLICATION=ON -DAPP_BUILD_MODULES=ON
cmake --build board/build-modules -j4

# Service：默认 alive + imu，control 明确关闭
cmake -S board -B board/build-services -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DBOARD_BUILD_APPLICATION=ON -DAPP_BUILD_SERVICES=ON \
  -DAPP_ENABLE_IMU=ON -DAPP_ENABLE_CONTROL=OFF
cmake --build board/build-services -j4
arm-none-eabi-size board/build-services/board.elf
```

注意 CMake cache 会保留上次开关；切换测试模式应明确传入 ON/OFF。启动 control 服务也不等于允许驱动，见 [电机清单](motor_changes.md)。UART接收、CAN启动和PWM输出均需调用对应接口，不会因编译进去就自动开始。

## ThreadX 依赖

Service 首次配置通过 CMake 下载 [Eclipse ThreadX 6.4.1](https://github.com/eclipse-threadx/threadx/tree/07eac307405dbf09d409844e31c37b0649d6c074)，固定提交与 SHA256；构建目录包含原始许可证，依赖源码不重复提交。旧的本机 X-CUBE-AZRTOS 包不是本构建输入。离线可用 `-DFETCHCONTENT_SOURCE_DIR_THREADX_SOURCE=/path/to/threadx` 指向同一版本完整源码。首次在线配置要能访问 GitHub，不需要提供密码。

- GNU Cortex-M4移植，硬浮点；SysTick=1 kHz/IRQ优先级4，PendSV=15由ThreadX提供；TIM6=1 kHz/优先级14供HAL使用。
- ThreadX优先级数字越小越高：timer=0，control=5，imu=8，alive=12。注意与NVIC中断优先级是不同概念。
- 静态栈：timer 1024 B、control 3072 B、imu 3072 B、alive 1536 B；禁用线程不分配其栈。主栈4096 B、链接器保留堆1024 B，应用不使用动态分配。
- 启用ThreadX栈检查，仍需实板测栈最大占用。初始化失败不启动驱动；LED红为故障、绿闪表示已启用服务健康、蓝闪表示监控循环运行。

## 主机测试

```sh
cmake -S tests -B tests/build -G 'Unix Makefiles'
cmake --build tests/build -j4
ctest --test-dir tests/build --output-on-failure
```

假端口验证协议、单位、限幅和故障路径，不仿真STM32电气特性。以实际输出为准，记录在 [logs.md](logs.md)。

## 上板前

不要直接启动电机。先用CubeProgrammer选择正确的ST-LINK/J-Link与SWD并确认目标电压，读取设备信息；下载明确选择的构建产物后，按 [Board配置](board.md)、[SPI检查](spi.md)、[I²C检查](iic.md) 逐项实测。当前未执行擦除、下载或运行设备操作。
