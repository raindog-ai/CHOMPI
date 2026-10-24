#!/usr/bin/env python3
"""Package the compiled firmware, exact factory rollback and reproducible evidence."""
from pathlib import Path
import hashlib, json, shutil, struct, subprocess, zipfile
root=Path(__file__).resolve().parents[1]
upstream='a73d732613da684e4de844619b690776f0f50ccf'
src=root/'firmware/chompi-tempo/code/src'
factory=root/'firmware/card-profiles/tempo-1.0'
out=root.parent/'CHOMPI-Tempo-Grid16-1.1-test'
out.mkdir(exist_ok=True)
for name in ('firmware-only','sd-card','rollback','debug','validation','source-changes'):
 (out/name).mkdir(exist_ok=True)
binary=(src/'build/CHOMPI.bin').read_bytes()
stack,reset=struct.unpack('<II',binary[:8])
assert stack==0x20020000,hex(stack)
assert reset&1 and 0x24000000<=reset<0x24000000+len(binary),hex(reset)
assert len(binary)<=282*1024,len(binary)
name='CHOMPI_TEMPO_GRID16.bin'
(out/'firmware-only'/name).write_bytes(binary)
shutil.copytree(factory,out/'sd-card',dirs_exist_ok=True)
# Only remove .bin files inside this generated release directory.
for p in (out/'sd-card').glob('*.bin'):p.unlink()
(out/'sd-card'/name).write_bytes(binary)
shutil.copy2(factory/'CHOMPI_TEMPOv1_0.bin',out/'rollback/CHOMPI_TEMPOv1_0.bin')
for ext in ('elf','map'):
 shutil.copy2(src/f'build/CHOMPI.{ext}',out/f'debug/CHOMPI_TEMPO_GRID16.{ext}')
for name in ('LICENSE','THIRD_PARTY.md','TRADEMARKS.md'):
 shutil.copy2(root/name,out/name)
shutil.copy2(root/'TEMPO-GRID.md',out/'START-HERE.md')
for name in ('grid-build.log','baseline-build.log','host-tests.log','audio-checks.json'):
 evidence=root/'validation'/name
 if evidence.exists():shutil.copy2(evidence,out/'validation'/name)
# Bundle small previews generated from the exact DSP under test.
for p in (root/'validation').glob('tone-*.wav'):
 shutil.copy2(p,out/'validation'/p.name)
tracked=subprocess.check_output(['git','diff',upstream,'--','firmware/chompi-tempo/code/src'],cwd=root)
grid='firmware/chompi-tempo/code/src/StepGrid.h'
indexed=subprocess.run(['git','ls-files','--error-unmatch',grid],cwd=root,capture_output=True)
if indexed.returncode:
 new=subprocess.run(['git','diff','--no-index','--','/dev/null',grid],cwd=root,capture_output=True)
 assert new.returncode==1,new.stderr.decode()
 tracked+=new.stdout
(out/'tempo-grid.patch').write_bytes(tracked)
for name in ('StepGrid.h','SampleManager.h','SliceEngine.h','ArpeggiatorSequencer.h','NormalPage.h','MenuPage.h','clockManager.h','MidiManager.h','chompi_main.cpp'):
 shutil.copy2(src/name,out/'source-changes'/name)
shutil.copytree(root/'tests',out/'tests',dirs_exist_ok=True)
shutil.copytree(root/'tools',out/'tools',dirs_exist_ok=True)
manifest={
 'name':'Tempo Grid 16','version':'1.1-hardware-test','upstream':'https://github.com/CHOMPI-Club/CHOMPI',
 'upstream_commit':upstream,
 'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
 'compiler':'Arm GNU Toolchain 13.3.Rel1, GCC 13.3.1',
 'target':'CHOMPI / STM32H750 Daisy Seed / BOOT_SRAM',
 'binary_bytes':len(binary),'execution_region_bytes':282*1024,
 'stack_pointer':hex(stack),'reset_vector':hex(reset),
 'hardware_tested':False,
 'hardware_smoke_test':'Owner reported boot and Think loop/Grid playback working; full acceptance and CPU measurements remain incomplete.',
 'dependencies':'Official vendored Tempo libDaisy/DaisySP archives',
 'dependency_sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'firmware/chompi-tempo/code/libs/libDaisy/build/libdaisy.a',root/'firmware/chompi-tempo/code/libs/DaisySP/build/libdaisysp.a']},
 'firmware_sha256':hashlib.sha256(binary).hexdigest(),
 'rollback_sha256':hashlib.sha256((factory/'CHOMPI_TEMPOv1_0.bin').read_bytes()).hexdigest()
}
(out/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
# Verify complete card assets, matching rollback and absence of competing firmware.
assert list((out/'sd-card').glob('*.bin'))==[out/'sd-card'/'CHOMPI_TEMPO_GRID16.bin']
for p in factory.rglob('*'):
 if p.is_file() and p.suffix!='.bin':assert p.read_bytes()==(out/'sd-card'/p.relative_to(factory)).read_bytes()
checks=[]
for p in sorted(out.rglob('*')):
 if p.is_file() and p.name!='SHA256SUMS.txt':checks.append(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(out)}')
(out/'SHA256SUMS.txt').write_text('\n'.join(checks)+'\n')
archive=out.parent/(out.name+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p in sorted(out.rglob('*')):
  if p.is_file():z.write(p,p.relative_to(out.parent))
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
print(json.dumps({'package':str(archive),'bytes':archive.stat().st_size,'firmware_bytes':len(binary),'firmware_sha256':manifest['firmware_sha256']},indent=2))
