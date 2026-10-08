"""Measure vertical motion on the stationary autosave warning, with capped captures."""
from pathlib import Path
import argparse, json, subprocess, sys, time
import numpy as np
from PIL import Image

GAME=Path(__file__).resolve().parents[1]
RUN=GAME/'logs/inspector'
parser=argparse.ArgumentParser()
parser.add_argument('--scene',choices=('warning','character'),default='warning')
args=parser.parse_args()
seconds,start=(16,11) if args.scene=='warning' else (50,45)
inputs=[] if args.scene=='warning' else ['--press-cross-at','12','--press-cross-at','16','--press-cross-at','20','--press-start-at','24','--press-cross-at','30','--press-cross-at','37']
(GAME/'logs/screen-motion-b.png').unlink(missing_ok=True)
rows=[]; previous=None; previous_capture=None; session=None; reference_pixels=None; saved_variant=False
with (GAME/'logs/screen-motion-run.log').open('w') as log:
 proc=subprocess.Popen([sys.executable,str(GAME/'tools/debug-runtime.py'),'--seconds',str(seconds),'--frame-interval-ms','33']+inputs,stdout=log,stderr=subprocess.STDOUT)
 try:
  started=time.monotonic()
  while proc.poll() is None:
   time.sleep(.03)
   if time.monotonic()-started<start: continue
   try:
    snap=json.loads((RUN/'inspector.json').read_text())
    meta=json.loads((RUN/'frame.png.json').read_text())
    if meta['capture']==previous_capture: continue
    pixels=np.asarray(Image.open(RUN/'frame.png').convert('RGB')).copy()
   except (OSError,ValueError): continue
   if pixels.shape[:2]!=(448,640): continue
   if session is not None and snap['session']!=session: raise RuntimeError('Runner session changed')
   session=snap['session'];previous_capture=meta['capture']
   if reference_pixels is None:
    reference_pixels=pixels.copy()
    Image.fromarray(pixels).save(GAME/'logs/screen-motion-a.png')
   elif not saved_variant and np.abs(pixels.astype(np.int16)-reference_pixels.astype(np.int16)).mean()>2:
    Image.fromarray(pixels).save(GAME/'logs/screen-motion-b.png')
    saved_variant=True
   # Use the text/border region; ignore animated button prompts below it.
   crop=(pixels[60:190,45:595] if args.scene=='warning' else pixels[10:70,45:595]).astype(np.int16)
   shifts=[]
   if previous is not None:
    for shift in range(-6,7):
     a=crop[6:-6];b=previous[6+shift:len(previous)-6+shift]
     shifts.append((float(np.abs(a-b).mean()),shift))
   g=snap['graphics']
   rows.append({'capture':meta['capture'],'elapsed':round(time.monotonic()-started,2),
                'best_shift':min(shifts)[1] if shifts else None,'error':min(shifts)[0] if shifts else None,
                'registers':{k:g[k] for k in ('smode2','dispfb1','dispfb2','display1','display2')}})
   previous=crop
   if len(rows)>=32: break
 finally:
  # The bounded driver owns/cleans up its runner; allow it to finish normally.
  proc.wait(timeout=30)
assert len(rows)>=3,rows
report={'session':session,'scene':args.scene,'samples':rows,'scope':'Native texture motion of stationary heading/text; inspect saved image to confirm scene', 'mean_difference':float(np.mean([r['error'] for r in rows if r['error'] is not None])), 'max_difference':max(r['error'] for r in rows if r['error'] is not None)}
if not saved_variant:
 Image.fromarray(reference_pixels).save(GAME/'logs/screen-motion-b.png')
(GAME/'logs/screen-motion-probe.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
