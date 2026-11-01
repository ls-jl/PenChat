#!/usr/bin/env python3
"""Actual child process: Y02 native frame dimensions, image export and discard."""
import pathlib,struct,tempfile
from test_scanner_process import Scanner

def run():
    with tempfile.TemporaryDirectory(prefix='penchat-y02-scanner-') as tmp:
        s=Scanner(tmp,profile='y02')
        try:
            s.press();s.frames(40);s.release()
            result=s.receive('finished')
            assert result['usable'] and result['counts']['received']==40,result
            s.send('confirm');image=s.receive('exported')['image'];m=image['metadata']
            assert m['profile']=='y02-sc035gs' and (m['sensor_width'],m['sensor_height'])==(640,480)
            assert m['calibration']=='y02-sc035gs-native-neutral-v1'
            # The mocked motion translates frames; output keeps full native height
            # after the prescribed sensor rotation, rather than 288x192 or 496x360.
            with pathlib.Path(image['path']).open('rb') as f:
                header=f.read(24);assert header[:8]==b'\x89PNG\r\n\x1a\n'
                w,h=struct.unpack('>II',header[16:24])
            assert (w,h)==(image['width'],image['height']) and h>=640,(w,h)
            s.send('discard');s.receive('ready')
            assert not pathlib.Path(image['path']).exists()
        finally:s.close()
    print('Y02 process: native 640x480 frames, explicit neutral profile, full-size PNG, review/confirmation and discard passed.')

if __name__=='__main__':run()
