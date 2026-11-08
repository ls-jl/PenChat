#!/usr/bin/env python3
"""OCR mode changes and scanner eligibility wait for the matching cleanup ACK."""
import pathlib,tempfile
import test_lvgl_app as t

t.port=1
with tempfile.TemporaryDirectory(prefix='penchat-ocr-handoff-') as directory:
    a=t.Native(t.ROOT/'build/host/penchat-lvgl',pathlib.Path(directory),seed_model=False,official_adapter=False)
    try:
        a.ocr_cancel_ack=False
        a.tap(400,232);first=a.receive('ocr.start')
        t.wait_for(lambda:a.diag()['editor']['ocr_active'])
        a.editor_action('ocr-mode');cancel=a.receive('ocr.cancel')
        assert cancel['requestId']==first['requestId']
        d=a.diag();assert d['ocr_release_pending'] and not d['editor']['ocr_active']
        a.send('ocr.stopped',requestId='stale',ok=True);assert a.diag()['ocr_release_pending']
        a.send('ocr.stopped',requestId=cancel['requestId'],ok=False);assert a.diag()['ocr_release_pending']
        a.send('ocr.stopped',requestId=cancel['requestId'],ok=True)
        second=a.receive('ocr.start');assert second['requestId']!=first['requestId'] and second['mode']=='multi'
        a.editor_action('done');a.receive('ocr.cancel')
        d=a.diag();assert not d['editor']['active'] and d['ocr_release_pending']
        a.send('ocr.stopped',requestId=second['requestId'],ok=True)
        t.wait_for(lambda:not a.diag()['ocr_release_pending'])
    finally:a.close()
print('OCR_CLEANUP_ACK_BLOCKS_MODE_REARM_AND_RAW_SCAN_UNTIL_MATCHED_OK')
