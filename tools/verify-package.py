#!/usr/bin/env python3
from pathlib import Path
import hashlib, json, subprocess, tempfile, zipfile
root=Path(__file__).resolve().parents[1]
release=root.parent/'CHOMPI-Tempo-Grid16-1.1-test'
manifest=json.loads((release/'build-manifest.json').read_text())
upstream=manifest['upstream_commit']
for line in (release/'SHA256SUMS.txt').read_text().splitlines():
 digest,name=line.split('  ',1)
 assert hashlib.sha256((release/name).read_bytes()).hexdigest()==digest,name
assert hashlib.sha256((root/'firmware/chompi-tempo/code/src/build/CHOMPI.bin').read_bytes()).hexdigest()==manifest['firmware_sha256']
with tempfile.TemporaryDirectory(prefix='tempo-grid-patch-') as folder:
 clean=Path(folder)
 source=Path('firmware/chompi-tempo/code/src')
 for p in (release/'source-changes').glob('*.h'):
  if p.name=='StepGrid.h':continue
  relative=source/p.name;target=clean/relative;target.parent.mkdir(parents=True,exist_ok=True)
  target.write_bytes(subprocess.check_output(['git','show',f'{upstream}:{relative}'],cwd=root))
 relative=source/'chompi_main.cpp'
 (clean/relative).write_bytes(subprocess.check_output(['git','show',f'{upstream}:{relative}'],cwd=root))
 subprocess.run(['git','apply','--check',str(release/'tempo-grid.patch')],cwd=clean,check=True)
 subprocess.run(['git','apply',str(release/'tempo-grid.patch')],cwd=clean,check=True)
 for p in (release/'source-changes').iterdir():
  assert p.read_bytes()==(clean/source/p.name).read_bytes(),p.name
with zipfile.ZipFile(release.parent/(release.name+'.zip')) as z:
 assert z.testzip() is None
 for name in z.namelist():
  relative=Path(name).relative_to(release.name)
  assert z.read(name)==(release/relative).read_bytes(),str(relative)
print('PASS: every package checksum, built-binary identity, clean-upstream patch replay, source snapshot identity, ZIP contents and CRC')
