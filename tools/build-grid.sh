#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GCC_PATH=${GCC_PATH:-"$ROOT/../toolchains/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi/bin"}
if ! "$GCC_PATH/arm-none-eabi-gcc" -dumpfullversion | grep -qx '13.3.1'; then
  echo 'TEMPO requires Arm GNU Toolchain 13.3.rel1 (GCC 13.3.1). Set GCC_PATH to its bin directory.' >&2
  exit 1
fi
mkdir -p "$ROOT/validation"
make -C "$ROOT/firmware/chompi-tempo/code/src" clean
make -C "$ROOT/firmware/chompi-tempo/code/src" -j4 GCC_PATH="$GCC_PATH" > "$ROOT/validation/grid-build.log" 2>&1
python3 "$ROOT/tools/package-grid.py"
