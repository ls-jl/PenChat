#!/usr/bin/env python3
"""An arm queued while waiting for display release must wake a disarmed worker."""
import json,tempfile
from test_scanner_process import Scanner
with tempfile.TemporaryDirectory(prefix='penchat-media-queue-') as directory:
    worker=Scanner(directory)
    try:
        worker.send('test.media_disarm');ticket=worker.receive('media_yield')
        worker.generation+=1;worker.send('arm')
        worker.send('media_ack',token=ticket['token'])
        # The old released packet precedes the new generation. No extra command
        # is sent to wake poll; the pending arm must be drained on its own.
        while True:
            packet=json.loads(worker.socket.recv(32768))
            assert packet['type'] not in ('fatal','error'),packet
            if packet['type']=='ready' and packet['generation']==worker.generation:break
        worker.press();worker.frames(1);worker.release()
        assert worker.receive('finished')['counts']['received']==1
    finally:worker.close()
print('SCANNER_DEFERRED_ARM_AFTER_MEDIA_RELEASE_OK')
