"""Objective duration/pitch/stereo checks on renders from the production DSP."""
from pathlib import Path
import json, wave
import numpy as np
root=Path(__file__).resolve().parents[1]
results=[]
for duration in (3000,6000,12000):
 p=root/'validation'/f'tone-{duration}.f32'
 a=np.fromfile(p,dtype='<f4').reshape(-1,2)
 assert len(a)==duration*16
 assert np.isfinite(a).all() and np.max(np.abs(a))<=1
 result={'frames_per_step':duration,'duration_seconds':len(a)/48000,'channels':[]}
 for c,expected in enumerate((440,660)):
  spectrum=np.abs(np.fft.rfft(a[:,c]*np.hanning(len(a))))
  fr=np.fft.rfftfreq(len(a),1/48000)
  peak=float(fr[spectrum.argmax()]);rms=float(np.sqrt(np.mean(a[:,c]**2)))
  assert abs(peak-expected)/expected<.025,(duration,c,peak)
  assert rms>.15
  result['channels'].append({'expected_hz':expected,'peak_hz':peak,'rms':rms})
 with wave.open(str(p.with_suffix('.wav')),'wb') as f:
  f.setparams((2,2,48000,0,'NONE','not compressed'));f.writeframes((a*32767).astype('<i2').tobytes())
 results.append(result)
(root/'validation'/'audio-checks.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: exact 1/2/4-second renders, stereo separation, finite bounded output, dominant pitch within 2.5% at 0.5x/1x/2x duration')
