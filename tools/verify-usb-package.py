#!/usr/bin/env python3
"""Verify package contents, binary memory bounds, and checked-out source hashes."""
import hashlib
import json
from pathlib import Path
import zipfile
ROOT=Path(__file__).resolve().parents[1]
PACKAGE=ROOT.parent/'CHOMPI-USB-FX-MIDI-0.1-test'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((PACKAGE/'build-manifest.json').read_text())
for folder in (PACKAGE,ROOT/'releases/usb-fx-midi-0.1'):
    listed=set()
    for line in (folder/'SHA256SUMS.txt').read_text().splitlines():
        expected,name=line.split('  ',1)
        assert digest(folder/name)==expected, name
        listed.add(name)
    actual={str(p.relative_to(folder)) for p in folder.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt'}
    assert listed==actual, 'Checksum coverage differs'
for name,expected in manifest['source_files'].items():assert digest(ROOT/name)==expected,name
binary=PACKAGE/'firmware/CHOMPI_USB_FX_MIDI.bin'
assert binary.stat().st_size==manifest['binary_bytes']<=manifest['sram_exec_limit']
assert digest(binary)==manifest['binary_sha256']
assert digest(PACKAGE/'rollback/CHOMPI_TEMPO_GRID16.bin')==manifest['rollback_sha256']
with zipfile.ZipFile(PACKAGE.with_suffix('.zip')) as z:
    assert z.testzip() is None
    names={str(p.relative_to(PACKAGE.parent)) for p in PACKAGE.rglob('*') if p.is_file()}
    assert set(z.namelist())==names
    for name in names:assert z.read(name)==(PACKAGE.parent/name).read_bytes(),name
assert manifest['hardware_tested'] is False
print('PASS package: complete hashes, exact binary/source/rollback identity, memory bound, ZIP byte identity')
