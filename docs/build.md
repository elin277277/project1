# 怎么编译

工程的入口是 `board/` 文件夹，不是仓库最外面。电脑上要装 ARM GCC 工具链、CMake 和 Make。macOS 上先把工具链加进 PATH：

```sh
export PATH="/Applications/ArmGNUToolchain/13.3.rel1/arm-none-eabi/bin:/Applications/CMake.app/Contents/bin:$PATH"
```

## 一层一层编

在仓库根目录下跑这些命令。第一次要指定工具链文件。

```sh
# Board：只有外设初始化，不启动任何通信
cmake -S board -B board/build-minimal -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build board/build-minimal -j4

# BSP：把 HAL 适配层编进来
cmake -S board -B board/build-bsp -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DBOARD_BUILD_APPLICATION=ON
cmake --build board/build-bsp -j4

# Module：编 PID、BMI088、M2006、LED 这些模块
cmake -S board -B board/build-modules -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DBOARD_BUILD_APPLICATION=ON -DAPP_BUILD_MODULES=ON
cmake --build board/build-modules -j4

# Service：默认开 alive + imu 线程，control（电机）明确关掉
cmake -S board -B board/build-services -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DBOARD_BUILD_APPLICATION=ON -DAPP_BUILD_SERVICES=ON \
  -DAPP_ENABLE_IMU=ON -DAPP_ENABLE_CONTROL=OFF
cmake --build board/build-services -j4
arm-none-eabi-size board/build-services/board.elf
```

注意：CMake 会记住上次的开关，切换的时候要把 ON/OFF 写清楚。还有，把 control 编进去也不代表电机会转，具体看 [电机清单](motor_changes.md)。

## ThreadX 的事

第一次编译 Service 的时候，CMake 会自动从网上下载 [ThreadX 6.4.1](https://github.com/eclipse-threadx/threadx)，所以第一次要联网。源码不会提交进仓库。

几个要记住的点：

- ThreadX 的优先级**数字越小优先级越高**（跟直觉相反）：timer=0，control=5，imu=8，alive=12。这跟 NVIC 中断优先级是两码事。
- 每个线程的栈是固定的：timer 1024B、control 3072B、imu 3072B、alive 1536B。
- LED 含义：红灯 = 出故障了，绿灯闪 = 服务正常跑着，蓝灯闪 = 监控线程在跑。

## 在电脑上跑测试

不用板子也能测逻辑：

```sh
cmake -S tests -B tests/build -G 'Unix Makefiles'
cmake --build tests/build -j4
ctest --test-dir tests/build --output-on-failure
```

这些测试用假端口验证协议、单位、限幅这些逻辑，但不能代替真板子的电气测试。

## 烧到板子上之前

先别急着开电机。用 CubeProgrammer 连上 ST-LINK，确认能读到芯片信息，再下载编译好的 elf。然后按 [引脚表](board.md)、[SPI](spi.md)、[I²C](iic.md) 一项一项在真板上验证。
