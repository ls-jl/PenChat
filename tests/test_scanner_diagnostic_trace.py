#!/usr/bin/env python3
"""Opt-in diagnostic build only: unchanged raw pixels, one-shot and cancel."""
import json
import pathlib
import struct
import tempfile
import time
from test_scanner_process import Scanner

with tempfile.TemporaryDirectory(prefix='penchat-raw-trace-') as directory:
    worker=Scanner(directory,profile='s6')
    pending=pathlib.Path(directory)/'pending'
    try:
        worker.press();worker.frames(1);worker.release();worker.receive('finished')
        assert not list(pending.glob('*.ps32'))
        worker.send('discard');worker.receive('ready')
        (pending/'.capture-next').touch()
        worker.press();worker.frames(40);worker.release();worker.receive('finished')
        end=time.monotonic()+5
        while time.monotonic()<end:
            traces=list(pending.glob('trace-*.json'))
            if traces:break
            time.sleep(.02)
        assert len(traces)==1,'diagnostic must be built with SCANNER_DIAGNOSTIC_TRACE=ON'
        trace=traces[0].with_suffix('.ps32');data=trace.read_bytes()
        assert struct.unpack_from('<4sII6I',data)==(b'PS32',1,1,320,240,0,40,0,0)
        offset=36+160*288*4
        expected=bytearray([210]*(320*240))
        for y in range(20,220):
            for x in range(20,300):
                if (x+y)%11<3:expected[y*320+x]=30
        assert data[offset:]==bytes(expected)*40,'raw trace must precede gain, rotation and blend'
        assert len(json.loads(traces[0].read_text()))==40
        assert not (pending/'.capture-next').exists()
        worker.send('discard');worker.receive('ready')
        worker.press();worker.frames(1);worker.release();worker.receive('finished')
        time.sleep(.05);assert len(list(pending.glob('*.ps32')))==1,'one marker, one scan only'
        worker.send('discard');worker.receive('ready')
        (pending/'.capture-next').touch()
        worker.press();worker.frames(1);worker.send('cancel');worker.receive('released')
        time.sleep(.05);assert len(list(pending.glob('*.ps32')))==1,'cancelled raw recording must not publish'
    finally:worker.close()
print('SCANNER_OPT_IN_RAW_TRACE_UNCHANGED_AND_ONESHOT_OK')
