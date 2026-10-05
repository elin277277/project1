cmake_minimum_required(VERSION 3.22)
# The upstream root entry referenced a non-existent cmake/ directory.
# Board owns its generated toolchain, startup and linker script.
message(FATAL_ERROR
    "Use the Board entry: cmake -S board -B board/build-minimal "
    "-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake . "
    "For host tests: cmake -S tests -B tests/build . See docs/build.md.")
