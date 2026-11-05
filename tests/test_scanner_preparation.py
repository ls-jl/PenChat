#!/usr/bin/env python3
"""Warmup is acknowledged before capture and cannot rearm after cancellation."""
import json, socket, tempfile, time
from test_scanner_process import Scanner

def run():
    with tempfile.TemporaryDirectory(prefix='penchat-preparation-') as directory:
        s=Scanner(directory,slow_ms=300,wait_ready=False)
        try:
            preparing=s.receive('preparing')
            assert preparing['phase']=='model_warmup' and not preparing['hardware_armed']
            s.send('disarm');assert not s.receive('released')['hardware_armed']
            time.sleep(.4)
            s.socket.settimeout(.1)
            try:
                unexpected=json.loads(s.socket.recv(32768))
                raise AssertionError(('cancelled preparation emitted an event',unexpected))
            except socket.timeout:pass
            finally:s.socket.settimeout(10)
            s.generation+=1;s.send('arm');ready=s.receive('ready')
            assert ready['model_prepared_us']>0
            s.press();s.frames(1);s.release();result=s.receive('finished')
            assert result['counts']['received']==1 and result['counts']['batches']==0
            assert result['firstBatchUs']==0 # Synthetic inference is never a real scan batch.
        finally:s.close()
    with tempfile.TemporaryDirectory(prefix='penchat-preparation-failure-') as directory:
        s=Scanner(directory,wait_ready=False,fail_prepare=True)
        try:
            assert json.loads(s.socket.recv(32768))['type']=='preparing'
            failure=json.loads(s.socket.recv(32768))
            assert failure['type']=='error' and failure['error']=='injected_model_preparation_failure'
            assert not failure['hardware_armed']
            s.generation+=1;s.send('arm');s.receive('ready')
            s.press();s.frames(1);s.release();assert s.receive('finished')['usable']
        finally:s.close()
    print('SCANNER_PREPARATION_OK')

if __name__=='__main__':run()
