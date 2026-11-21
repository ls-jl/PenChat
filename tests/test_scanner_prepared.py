#!/usr/bin/env python3
"""One background original serves review/save; cancellation cannot lose a reply."""
import os,pathlib,struct,tempfile,time
from test_scanner_process import Scanner
os.environ['PENCHAT_TEST_SCAN_EXPORT_MS']='200'
for profile in ('orange','s6'):
 with tempfile.TemporaryDirectory(prefix='penchat-scan-prepared-') as folder:
  s=Scanner(folder,profile=profile)
  try:
   s.press()
   if profile=='s6':
    for _ in range(32):s.send('test.frame',darkPaper=True,paperGlare=True)
    live=s.receive('preview')
    assert live['counts']['committed']>=24,live
    _,w,h=struct.unpack_from('<III',s.memory)
    pixels=s.memory[32:32+w*h]
    assert pixels.count(148)>len(pixels)//3,sorted(set(pixels))
   else:s.frames(33)
   s.release();assert s.receive('finished')['usable']
   # Both requests arrive while the same preparation is deliberately pending.
   s.send('prepare_review');old=s.request
   s.send('cancel_review');s.send('prepare_review');review=s.request
   s.send('confirm');confirm=s.request
   prepared=s.receive('prepared');image=prepared['image'];path=pathlib.Path(image['path'])
   assert path.exists() and prepared['prepare_us']>=190000
   ready=s.receive('review_ready');assert ready['requestId']==review and ready['requestId']!=old
   done=s.receive('exported');assert done['requestId']==confirm and done['image']==image
   stamp=path.stat().st_mtime_ns;start=time.monotonic()
   s.send('confirm');cached=s.receive('exported')
   assert cached['cached'] and cached['image']==image and path.stat().st_mtime_ns==stamp
   assert time.monotonic()-start<.15
   s.send('discard');s.receive('ready');assert not path.exists()
   # A viewer close without a save does not throw away the prepared original.
   s.press();s.frames(1);s.release();s.receive('finished')
   s.send('prepare_review');s.send('cancel_review');image=s.receive('prepared')['image']
   s.send('prepare_review');assert s.receive('review_ready')['cached']
   s.send('discard');s.receive('ready');assert not pathlib.Path(image['path']).exists()
   # Real discard joins/cancels a background reader and removes temporary files.
   s.press();s.frames(1);s.release();s.receive('finished');s.send('discard');s.receive('ready')
   assert not list((pathlib.Path(folder)/'pending').iterdir())
  finally:s.close()
 print('Prepared scan original: shared review/save, exact request IDs, cached reuse and cancellation:',profile,'passed',flush=True)
