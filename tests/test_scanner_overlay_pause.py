#!/usr/bin/env python3
"""Real worker: retain model/result and observe edges while trigger-gated."""
import pathlib,tempfile,shutil
from test_scanner_process import BUNDLE,ROOT
from test_scanner_process import Scanner
for profile in ('orange','y02','y07','melon','s6'):
 with tempfile.TemporaryDirectory(prefix='penchat-overlay-scan-') as folder:
  resources=pathlib.Path(folder)/'resources';resources.mkdir()
  for f in (BUNDLE/'resources/illumination').iterdir():(resources/f.name).symlink_to(f)
  shutil.copyfile(ROOT/'native/scanner/calibration/y07-sc2356.json',resources/'y07-sc2356.json')
  s=Scanner(pathlib.Path(folder),profile=profile,resources=resources)
  try:
   s.send('overlay_pause',paused=True,observe_only=True);first=s.receive('overlay_pause')
   assert first['model_ready'] and first['overlay_paused'] and first['overlay_observing']
   identity=(s.process.pid,first['model_prepared_us'],first['model_prepare_requests'])
   s.send('test.press',pressed=True);s.send('overlay_pause',paused=True,observe_only=True)
   held=s.receive('overlay_pause');assert held['scanId']==0 and held['observed_key_edges']==1
   s.send('overlay_pause',paused=False);s.receive('overlay_pause')
   # A duplicate down after re-enable must not create a scan.
   s.send('test.press',pressed=True);s.send('overlay_pause',paused=False)
   assert s.receive('overlay_pause')['scanId']==0
   s.send('test.press',pressed=False);s.press();s.frames(33)
   s.send('overlay_pause',paused=True,observe_only=True);paused=s.receive('overlay_pause');r=s.receive('finished')
   assert r['usable'] and r['counts']['received']==33 and r['counts']['discarded_backlog']==0
   assert (s.process.pid,r['model_prepared_us'],r['model_prepare_requests'])==identity
   s.send('prepare_review');original=s.receive('review_ready')['image']
   path=pathlib.Path(original['path']);stamp=path.stat().st_mtime_ns
   s.send('overlay_pause',paused=False);s.receive('overlay_pause')
   s.send('prepare_review');assert s.receive('review_ready')['image']==original
   assert path.stat().st_mtime_ns==stamp
  finally:s.close()
 print('Overlay retains worker/model/normal-sealed result, ignores held/replayed press:',profile,'passed',flush=True)
