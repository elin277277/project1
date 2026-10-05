#!/usr/bin/env bash
# CubeMX 6.18 CMake generation requires canonical project paths.
set -euo pipefail
BOARD_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
JAVA="${JAVA:-/Applications/STMicroelectronics/STM32CubeMX.app/Contents/Resources/jre/Contents/Home/bin/java}"
CUBEMX="${CUBEMX:-/Applications/STMicroelectronics/STM32CubeMX.app/Contents/Resources/STM32CubeMX}"
mkdir -p "$BOARD_DIR/build"
SCRIPT="$BOARD_DIR/build/generate.script"
printf 'config load %s\nproject toolchain CMake\nproject generate\nexit\n' \
  "$BOARD_DIR/board.ioc" > "$SCRIPT"
"$JAVA" -jar "$CUBEMX" -q "$SCRIPT" 2>&1 | tee "$BOARD_DIR/build/cubemx-generation.log"
