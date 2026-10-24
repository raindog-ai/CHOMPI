#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GCC_PATH=${GCC_PATH:-"$ROOT/../toolchains/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi/bin"}
if [[ $("$GCC_PATH/arm-none-eabi-gcc" -dumpfullversion) != 13.3.1 ]]; then
    echo 'Requires Arm GNU Toolchain 13.3.rel1 (GCC 13.3.1).' >&2
    exit 1
fi
mkdir -p "$ROOT/validation"
make -C "$ROOT/firmware/chompi-tempo/code/src" USB_AUDIO=1 BUILD_DIR=build-usb clean
make -C "$ROOT/firmware/chompi-tempo/code/src" -j4 USB_AUDIO=1 BUILD_DIR=build-usb GCC_PATH="$GCC_PATH" > "$ROOT/validation/usb-build.log" 2>&1
python3 "$ROOT/tools/package-usb.py"
