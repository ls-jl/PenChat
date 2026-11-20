#!/usr/bin/env python3
"""OCR mode changes and scanner eligibility wait for the matching cleanup ACK."""
import pathlib,tempfile,time
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
        # A terminal route failure for an already-delivered capture is not a
        # duplicate text result: withdraw readiness so the retry entry works.
        a.tap(400,232);third=a.receive('ocr.start')
        a.send('ocr.result',requestId=third['requestId'],session=third['session'],mode=third['mode'],capture=1,confirmed=True,text='OCR')
        t.wait_for(lambda:a.diag()['editor']['length']==3)
        a.send('ocr.result',requestId=third['requestId'],session=third['session'],mode=third['mode'],capture=1,confirmed=False,terminal=True,error='fixture route failed')
        t.wait_for(lambda:not a.diag()['editor']['ocr_active'])
        assert a.diag()['editor']['length']==3 and a.diag()['editor']['notice_present']
    finally:a.close()
print('OCR_CLEANUP_ACK_BLOCKS_MODE_REARM_AND_RAW_SCAN_UNTIL_MATCHED_OK')

# A brief launcher stall must cancel touch without unregistering an in-flight
# recognition. Explicit media/focus loss must still clean up even if touch had
# already expired. No touchscreen/device dependencies in this fixture.
with tempfile.TemporaryDirectory(prefix='penchat-ocr-focus-') as directory:
    a=t.Native(t.ROOT/'build/host/penchat-lvgl',pathlib.Path(directory),seed_model=False,official_adapter=False)
    try:
        a.tap(400,232);request=a.receive('ocr.start')
        a.send('input.focus',sequence=1,active=True)
        time.sleep(.38)
        state=a.diag()
        assert not state['input_focused'] and state['editor']['ocr_active']
        assert not state['ocr_release_pending'] and state['editor']['ocr_request']==request['requestId']
        a.send('input.focus',sequence=2,active=True)
        assert a.diag()['editor']['ocr_request']==request['requestId']
        a.ocr_cancel_ack=False
        a.send('input.focus',sequence=3,active=False)
        t.wait_for(lambda:a.diag()['ocr_release_pending'])
        assert not a.diag()['editor']['ocr_active']
    finally:a.close()
print('OCR survives short input-only expiry and releases on explicit media focus loss (host fixture).')
