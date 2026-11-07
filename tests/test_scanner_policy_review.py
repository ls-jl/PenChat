#!/usr/bin/env python3
"""Host mock sensor/inference, real worker and row-PNG. Never accesses devices."""
import json,pathlib,shutil,struct,tempfile
from test_scanner_process import Scanner,BUNDLE,ROOT

def run():
    with tempfile.TemporaryDirectory(prefix='penchat-scan-policies-') as temporary:
        root=pathlib.Path(temporary);resources=root/'resources';resources.mkdir()
        for f in (BUNDLE/'resources/illumination').iterdir():
            (resources/f.name).symlink_to(f)
        shutil.copyfile(ROOT/'native/scanner/calibration/y07-sc2356.json',resources/'y07-sc2356.json')
        for profile in ('orange','y02','y07','melon'):
            directory=root/profile;directory.mkdir();s=Scanner(directory,profile=profile,resources=resources)
            try:
                s.press();s.release();assert not s.receive('finished')['usable']
                s.send('prepare_review');assert s.receive('review_error')['requestId']==s.request
                s.send('discard');s.receive('ready')
                for missing in (1,7,8,9,10):
                    s.press();s.send('test.frame',rawSequence=0,rawTimestamp=100000)
                    s.send('test.frame',rawSequence=1,rawTimestamp=108400)
                    s.send('test.frame',rawSequence=2+missing,rawTimestamp=200000)
                    s.release();r=s.receive('finished')
                    assert r['counts']['dropped_raw']==missing,r
                    assert (r['status']==1)==(missing<=8),r
                    assert r['counts']['received']==(3 if missing<=8 else 2),r
                    if missing>=9:assert r['reason']=='sensor_sequence_gap',r
                    assert r['usable']
                    # Review exports real canvas rows, preserving result and request identity.
                    s.send('prepare_review');ticket=s.request;ready=s.receive('review_ready')
                    assert ready['requestId']==ticket and ready['scanId']==s.scan,ready
                    image=ready['image'];path=pathlib.Path(image['path'])
                    assert image['metadata']['counts']['dropped_raw']==missing
                    assert struct.unpack('>II',path.read_bytes()[16:24])==(image['width'],image['height'])
                    before=path.stat().st_mtime_ns
                    s.send('cancel_review');s.send('prepare_review');again=s.receive('review_ready')
                    assert again['requestId']==s.request and again['image']==image
                    assert path.stat().st_mtime_ns==before # Cached original, not rewritten.
                    s.send('confirm');assert s.receive('exported')['image']==image
                    s.send('discard');s.receive('ready');assert not path.exists()
                s.press()
                for seq in [0,2,10,19,21]:s.send('test.frame',rawSequence=seq,rawTimestamp=100000+seq*8400)
                s.release();r=s.receive('finished')
                assert r['status']==1 and r['counts']['dropped_raw']==17,r
                s.send('discard');s.receive('ready')
                # A failed review is recoverable; it must not consume the scan.
                s.press();s.frames(1);s.release();assert s.receive('finished')['usable']
                pending=directory/'pending';pending.rmdir();pending.write_text('not a directory')
                s.send('prepare_review');failure=s.receive('review_error')
                assert failure['requestId']==s.request
                pending.unlink();pending.mkdir()
                s.send('prepare_review');image=s.receive('review_ready')['image']
                s.send('cancel');s.receive('released')
                assert not list(pending.iterdir())
                # EOF while preparation is queued/in flight joins before releasing canvas.
                s.generation+=1;s.send('arm');s.receive('ready');s.press();s.frames(56);s.release();s.receive('finished')
                s.send('prepare_review')
            finally:s.close()
            assert not list((directory/'pending').iterdir())
    print('All four mock profiles: missing 1/7/8 continue, 9/10 stop, repeated gaps, original review/cache, failure/retry, cancel and EOF cleanup passed.')

if __name__=='__main__':run()
