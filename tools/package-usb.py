#!/usr/bin/env python3
"""Package only the USB test build; never reads/writes a mounted card."""
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import wave
import zipfile

ROOT=Path(__file__).resolve().parents[1]
NAME='CHOMPI-USB-FX-MIDI-0.1-test'
PACKAGE=ROOT.parent/NAME
RELEASE=ROOT/'releases/usb-fx-midi-0.1'
BUILD=ROOT/'firmware/chompi-tempo/code/src/build-usb'
BINARY=BUILD/'CHOMPI_USB_FX_MIDI.bin'
BASE='8610a8c0fa5f9559c3f03d79c702939462e03a23'

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def git(*args): return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()

def source_files():
    # Hash the complete firmware source/library input set, not generated objects.
    base=ROOT/'firmware/chompi-tempo/code'
    for p in sorted(base.rglob('*')):
        if not p.is_file() or any(x in ('build-usb','build') for x in p.relative_to(base).parts): continue
        if p.suffix.lower() in ('.h','.hpp','.c','.cpp','.s','.lds','.ld','.a') or p.name=='Makefile': yield p
    for p in sorted((ROOT/'tests').rglob('*')):
        if p.is_file(): yield p
    for name in ('libDaisy/build/libdaisy.a','DaisySP/build/libdaisysp.a'):
        yield base/'libs'/name
    for name in ('test-usb.sh','build-usb.sh','package-usb.py','verify-usb-package.py','usb-diagnostics.py'):
        yield ROOT/'tools'/name
    yield ROOT/'USB-FX-MIDI.md'

def checksums(folder):
    files={str(p.relative_to(folder)):digest(p) for p in sorted(folder.rglob('*')) if p.is_file() and p.name!='SHA256SUMS.txt'}
    (folder/'SHA256SUMS.txt').write_text(''.join(f'{h}  {p}\n' for p,h in files.items()))

def wav(path, seconds, mode):
    with wave.open(str(path),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(48000)
        block=bytearray()
        for i in range(seconds*48000):
            t=i/48000
            l=r=0.0
            if mode=='channels':
                if t<2: l=.125*math.sin(2*math.pi*440*t)
                elif t<4: r=.125*math.sin(2*math.pi*660*t)
                else: l=.125*math.sin(2*math.pi*440*t);r=-l
            elif mode=='impulses':
                if i%48000==0:l=.25
                if i%48000==17:r=.25
            block.extend(struct.pack('<hh',int(l*32767),int(r*32767)))
            if len(block)>=16384:f.writeframes(block);block.clear()
        if block:f.writeframes(block)

def main():
    if not BINARY.is_file() or BINARY.stat().st_size>282*1024: raise SystemExit('Missing or oversized USB binary')
    if PACKAGE.exists() and any(PACKAGE.iterdir()) and not (PACKAGE/'build-manifest.json').exists():
        raise SystemExit(f'Refusing to overwrite an unrelated directory: {PACKAGE}')
    inputs={str(p.relative_to(ROOT)):digest(p) for p in source_files()}
    log=(ROOT/'validation/usb-build.log').read_text()
    match=re.search(r'SRAM_EXEC:\s+(\d+) B',log)
    if not match: raise SystemExit('Missing successful ARM memory report')
    tests=(ROOT/'validation/usb-tests.log').read_text()
    required=('ALL USB SOFTWARE TESTS PASSED','production FX routing','8 recovery scenarios passed','3600000 ms')
    if not all(x in tests for x in required): raise SystemExit('Missing complete USB test evidence')
    rollback=ROOT/'releases/tempo-grid16-1.1/CHOMPI_TEMPO_GRID16.bin'
    expected='e77609c8ef9fa0f50e72ba6bc6ec7ae817a4315a629f376f42218aeb66993b44'
    if digest(rollback)!=expected: raise SystemExit('Rollback binary identity mismatch')
    manifest={'name':NAME,'status':'experimental-hardware-test-candidate','hardware_tested':False,
              'source_commit':git('rev-parse','HEAD'),'base_commit':BASE,
              'toolchain':'Arm GNU 13.3.rel1 / GCC 13.3.1','target':'Daisy Seed CHOMPI BOOT_SRAM',
              'binary_bytes':BINARY.stat().st_size,'sram_exec_bytes':int(match[1]),'sram_exec_limit':282*1024,
              'binary_sha256':digest(BINARY),'rollback_sha256':expected,'source_files':inputs}
    for folder in (PACKAGE,RELEASE): folder.mkdir(parents=True,exist_ok=True)
    for folder in (PACKAGE/'firmware',PACKAGE/'rollback',PACKAGE/'debug',PACKAGE/'evidence',PACKAGE/'tools',PACKAGE/'test-audio'): folder.mkdir(exist_ok=True)
    shutil.copy2(BINARY,PACKAGE/'firmware'/BINARY.name)
    shutil.copy2(BINARY,RELEASE/BINARY.name)
    shutil.copy2(rollback,PACKAGE/'rollback'/rollback.name)
    for suffix in ('elf','map'):shutil.copy2(BUILD/f'CHOMPI_USB_FX_MIDI.{suffix}',PACKAGE/'debug')
    shutil.copy2(ROOT/'USB-FX-MIDI.md',PACKAGE/'README.md')
    shutil.copy2(ROOT/'USB-FX-MIDI.md',RELEASE/'README.md')
    for name in ('usb-tests.log','grid-regression-usb.log','usb-build.log'):
        shutil.copy2(ROOT/'validation'/name,PACKAGE/'evidence'/name)
    shutil.copy2(ROOT/'validation/usb-tests.log',RELEASE/'software-tests.log')
    shutil.copy2(ROOT/'tools/usb-diagnostics.py',PACKAGE/'tools')
    for name,seconds,mode in (('channel-id.wav',6,'channels'),('latency-impulses.wav',5,'impulses'),('silence.wav',4,'silence')):wav(PACKAGE/'test-audio'/name,seconds,mode)
    patch=subprocess.check_output(['git','diff','--binary',BASE,'--','firmware/chompi-tempo/code','tests','tools','USB-FX-MIDI.md','.gitignore'],cwd=ROOT)
    (PACKAGE/'source.patch').write_bytes(patch)
    for folder in (PACKAGE,RELEASE):
        (folder/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        checksums(folder)
    zip_path=Path(str(PACKAGE)+'.zip')
    with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(PACKAGE.rglob('*')):
            if p.is_file():z.write(p,str(p.relative_to(PACKAGE.parent)))
    print(f'Packaged {BINARY.stat().st_size:,} bytes; {manifest["sram_exec_limit"]-manifest["sram_exec_bytes"]:,} execution bytes free')
    print(zip_path)
if __name__=='__main__': main()
