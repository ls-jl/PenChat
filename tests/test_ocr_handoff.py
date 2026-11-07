#!/usr/bin/env python3
"""Portrait OCR registration must finish before native scan ownership releases.
Uses the actual host process and explicit launcher acknowledgements; no devices.
"""
import json,os,pathlib,tempfile,time
import test_lvgl_app as t
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
def acknowledge(a,request,ok=True):
    a.send('ocr.prepared',requestId=request['requestId'],session=request['session'],mode=request['mode'],ok=ok)
def ready(a):
    return t.wait_for(lambda:a.diag()['editor'] if a.diag()['editor']['keys'] else None)
def run():
    t.port=1
    with tempfile.TemporaryDirectory(prefix='penchat-ocr-handoff-') as directory:
        a=t.Native(BINARY,pathlib.Path(directory),screen={'width':568,'height':1210},official_adapter=False,ocr_prepare_adapter=False)
        try:
            a.tap(280,1140);request=a.receive('ocr.prepare')
            e=a.diag()['editor'];assert e['active'] and e['ocr_preparing'] and not e['keys']
            assert not any(m.get('type')=='ocr.start' for m in a.pending)
            a.send('ocr.prepared',requestId='obsolete',session=request['session'],mode='single',ok=True)
            assert a.diag()['editor']['ocr_preparing']
            acknowledge(a,request);start=a.receive('ocr.start');assert start['requestId']==request['requestId']
            e=ready(a);assert not e['ocr_preparing'] and e['ocr_active']
            a.send('ocr.result',requestId=request['requestId'],session=request['session'],mode='single',capture=1,confirmed=True,text='测试')
            t.wait_for(lambda:a.diag()['editor']['length']==2)
            e=a.diag()['editor'];assert not e['shared_candidate_row'] and 'ocr-mode' in e['actions']
            a.editor_action('ocr-mode');replacement=a.receive('ocr.prepare')
            assert replacement['requestId']!=request['requestId'] and replacement['mode']=='multi'
            assert not any(m.get('type')=='ocr.cancel' and m.get('requestId')==request['requestId'] for m in a.pending)
            acknowledge(a,request);assert a.diag()['editor']['ocr_preparing']
            acknowledge(a,replacement,False)
            e=ready(a);assert e['ocr_preparing'] and not e['ocr_active'] and e['notice_present']
            a.editor_action('ocr-mode');retry=a.receive('ocr.prepare');acknowledge(a,retry)
            assert a.receive('ocr.start')['requestId']==retry['requestId'];ready(a)
            a.send('ocr.result',requestId=request['requestId'],session=request['session'],mode='single',capture=1,confirmed=True,text='stale')
            assert a.diag()['editor']['length']==2
            a.send('visibility',visible=False);t.wait_for(lambda:not a.diag()['editor']['ocr_preparing'])
            cancel=a.receive('ocr.cancel');assert cancel['requestId']==retry['requestId']
            a.send('visibility',visible=True);resumed=a.receive('ocr.prepare');acknowledge(a,resumed)
            assert a.receive('ocr.start')['requestId']==resumed['requestId'];ready(a)
            a.editor_action('done');t.wait_for(lambda:not a.diag()['editor']['active'])
            assert not a.diag()['editor']['ocr_preparing'] and not a.read_messages()
        finally:a.close()
    print('OCR handoff: ordered prepare/ack/start, stale IDs, mode replacement, failure/retry and lifecycle cleanup passed (host simulation).')
if __name__=='__main__':run()
