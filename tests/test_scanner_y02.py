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
            # Reproduce 119fps SC035GS bursts without inserting missing frames.
            for missing,elapsed,stable,repeated,reset,okay in [
                (1,16800,16,False,False,True),
                (2,25200,32,False,False,True),
                (3,33600,32,False,False,True),
                (3,40000,32,False,False,True),
                (3,40001,32,False,False,True),
                (4,42000,32,False,False,True),
                (7,67200,32,False,False,True),
                (7,150000,32,False,False,True),
                (8,75600,32,False,False,True),
                (9,84000,32,False,False,False),
                (10,92400,32,False,False,False),
                (1,16800,2,True,False,True),
                (1,0,32,False,False,False),
                (1,-1,32,False,False,False),
                (0,8400,32,False,True,False),
            ]:
                s.press()
                for i in range(stable):
                    s.send('test.frame',rawSequence=i,rawTimestamp=100000+i*8400)
                sequence=0 if reset else stable+missing
                timestamp=100000+(stable-1)*8400+elapsed
                s.send('test.frame',rawSequence=sequence,rawTimestamp=timestamp)
                if repeated:s.send('test.frame',rawSequence=sequence+2,rawTimestamp=timestamp+16800)
                s.release();result=s.receive('finished')
                assert (result['status']==1)==okay,result
                gaps=result['frame_gaps'];assert gaps and len(gaps)<=16,result
                assert gaps[0]['elapsed_us']==elapsed and gaps[0]['stable_frames']==16,gaps
                if okay:
                    assert result['counts']['received']==stable+1+int(repeated),result
                    assert result['counts']['dropped_raw']==missing+int(repeated),result
                    assert result['counts']['recovered_frames']==missing+int(repeated),result
                    assert result['counts']['recovered_gaps']==1+int(repeated),result
                    assert all(gap['recovered'] for gap in gaps),gaps
                else:
                    assert result['reason']==('sensor_sequence_reset' if reset else 'sensor_sequence_gap'),result
                    assert not gaps[-1]['recovered'],gaps
                s.send('discard');s.receive('ready')
            # The reported device case: two single losses followed by 3 lost
            # frames. Keep accepting real frames until the pen is released.
            s.press()
            sequences=list(range(32))+list(range(33,54))+list(range(55,76))+[79,80]
            for seq in sequences:
                s.send('test.frame',rawSequence=seq,rawTimestamp=100000+seq*8400)
            s.release();result=s.receive('finished')
            assert result['status']==1 and result['counts']['received']==len(sequences),result
            assert result['counts']['dropped_raw']==5 and result['counts']['recovered_frames']==5,result
            assert result['counts']['recovered_gaps']==3 and len(result['frame_gaps'])==3,result
            assert all(gap['recovered'] for gap in result['frame_gaps']),result
            s.send('discard');s.receive('ready')
            # Total missing frames may exceed eight; only a single continuous
            # gap reaching nine stops capture. Repeated small gaps are visible.
            s.press();sequences=list(range(32))+list(range(33,50,2))
            for seq in sequences:
                s.send('test.frame',rawSequence=seq,rawTimestamp=100000+seq*8400)
            s.release();result=s.receive('finished')
            assert result['status']==1 and result['counts']['received']==len(sequences),result
            assert result['counts']['dropped_raw']==9 and result['counts']['recovered_frames']==9,result
            assert len(result['frame_gaps'])==9 and all(g['recovered'] for g in result['frame_gaps']),result
            s.send('discard');s.receive('ready')
        finally:s.close()
    print('Y02 process: native 640x480 frames, explicit neutral profile, full-size PNG, review/confirmation and discard passed.')

if __name__=='__main__':run()
