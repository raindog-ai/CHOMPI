#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p validation/usb
export UBSAN_OPTIONS=halt_on_error=1
CORE=firmware/chompi-tempo/code/libs/libDaisy/Middlewares/ST/STM32_USB_Device_Library/Core
FLAGS=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -DCHOMPI_USB_AUDIO -I tests/usb/mock -I "$CORE/Inc" -I firmware/chompi-tempo/code/src)
for src in usbd_core usbd_ctlreq usbd_ioreq; do
    clang "${FLAGS[@]}" -c "$CORE/Src/$src.c" -o "validation/usb/$src.o"
done
clang++ -std=c++14 "${FLAGS[@]}" -Wall -Wextra -Werror tests/usb/usb_test.cpp validation/usb/*.o -o validation/usb/test
validation/usb/test | tee validation/usb-tests.log
python3 - <<'PY'
from pathlib import Path
s=Path('firmware/chompi-tempo/code/src/FxEngine.h').read_text()
Path('validation/usb/FxEngine.host.h').write_text('\n'.join(l for l in s.splitlines() if not l.startswith('#include')))
PY
clang++ -std=c++14 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -I validation/usb tests/usb/fx_route_test.cpp -o validation/usb/fx-test
validation/usb/fx-test | tee -a validation/usb-tests.log
clang++ -std=c++14 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -I firmware/chompi-tempo/code/src tests/usb/iso_recovery_test.cpp -o validation/usb/recovery-test
validation/usb/recovery-test | tee -a validation/usb-tests.log
