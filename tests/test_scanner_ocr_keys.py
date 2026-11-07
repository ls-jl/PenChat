#!/usr/bin/env python3
"""Scanner process retains only its pen lease during portrait keyboard OCR."""
import tempfile,pathlib
from test_scanner_process import Scanner

def run():
    with tempfile.TemporaryDirectory(prefix='penchat-ocr-keys-') as directory:
        s=Scanner(directory,profile='y07',resources=pathlib.Path(__file__).resolve().parents[1]/'assets/scanner')
        try:
            s.generation+=1;s.send('ocr_keys');result=s.receive('released')
            assert result['ocr_keys'] and not result['hardware_armed']
            s.send('test.press',pressed=True);down=s.receive('ocr_key');assert down['down']
            s.send('test.press',pressed=False);up=s.receive('ocr_key');assert not up['down'] and up['sequence']==down['sequence']
            s.send('disarm');s.receive('released')
            s.generation+=1;s.send('ocr_keys');assert s.receive('released')['ocr_keys']
            s.send('test.press',pressed=True);assert s.receive('ocr_key')['sequence']>up['sequence']
            s.send('disarm');s.receive('released')
            s.generation+=1;s.send('arm');s.receive('ready');s.press();s.release();assert not s.receive('finished')['usable']
        finally:s.close()
    print('Scanner OCR keys: scan-to-key-only, direct key boundaries, idle acquisition, disarm and scan rearm passed (host simulation).')
if __name__=='__main__':run()
