#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p validation
CXX=${CXX:-clang++}
FLAGS=(-std=c++14 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I firmware/chompi-tempo/code/src)
"$CXX" "${FLAGS[@]}" -Wall -Wextra -Werror tests/step_grid_test.cpp -o validation/step-grid-test
validation/step-grid-test validation | tee validation/host-tests.log
python3 - <<'PY'
from pathlib import Path
s=Path('firmware/chompi-tempo/code/src/ArpeggiatorSequencer.h').read_text()
Path('validation/ArpeggiatorSequencer.host.h').write_text('\n'.join(l for l in s.splitlines() if not l.startswith('#include')))
PY
"$CXX" "${FLAGS[@]}" -I tests -I validation tests/arp_integration_test.cpp -o validation/arp-integration-test
validation/arp-integration-test | tee -a validation/host-tests.log
python3 - <<'PYGEN'
from pathlib import Path
s=Path('firmware/chompi-tempo/code/src/clockManager.h').read_text()
Path('validation/clockManager.host.h').write_text('\n'.join(l for l in s.splitlines() if l not in ('#include "daisy.h"','#include "daisysp.h"')))
PYGEN
"$CXX" "${FLAGS[@]}" -I tests -I validation tests/clock_test.cpp -o validation/clock-test
validation/clock-test | tee -a validation/host-tests.log
python3 tools/check-audio.py
python3 - <<'PYBUDGET'
from pathlib import Path
import subprocess
old=subprocess.check_output(['git','show','8610a8c:firmware/chompi-tempo/code/src/StepGrid.h'],text=True)
old=old.replace('namespace tempo_grid {','namespace tempo_grid_11 {')
old=old.replace('float cost = 0.f;', '++oldCalls; float cost = 0.f;')
Path('validation/StepGrid11.host.h').write_text(old)
PYBUDGET
"$CXX" "${FLAGS[@]}" -I validation -Wall -Wextra -Werror tests/grid_budget_test.cpp -o validation/grid-budget-test
validation/grid-budget-test | tee validation/grid-budget.log
