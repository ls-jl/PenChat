#!/usr/bin/env python3
"""Composer menu context occupancy: same estimate, saturated ring, real compression."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_context import fixture,Server,checkpoint,wait_idle
from test_image_viewer import shot
from test_reasoning_ui import seed
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))

def menu(a):
    s=a.diag()
    if not s['attachments']:a.tap(128,s['height']-34)
    t.wait_for(lambda:a.diag()['context_usage'] and a.diag()['context_usage']['ready'])
    for _ in range(15):
        s=a.diag();b=s['attachment_actions']['compress'];v=s['attachment_viewport']
        if b[1]>=v[1] and b[3]<=v[3]:return s['context_usage']
        start=v[3]-10 if b[3]>v[3] else v[1]+10
        end=max(v[1]+10,start-90) if b[3]>v[3] else min(v[3]-10,start+90)
        for i in range(7):
            a.send('touch',x=220,y=start+(end-start)*i//6,pressed=True);time.sleep(.025)
        time.sleep(.15);a.send('touch',x=220,y=end,pressed=False);time.sleep(.1)
    raise AssertionError('Compression row unreachable')

def check(s):
    ratio=s['estimated_tokens']/s['budget']
    assert s['caption']==f'压缩上下文（{ratio*100:.1f}%）',s
    assert abs(s['value']-round(min(1,ratio)*1000))<=1,s
    assert s['ring'][2]-s['ring'][0]+1==24 and s['ring'][3]-s['ring'][1]+1==24

def run(w,h):
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server,tempfile.TemporaryDirectory(prefix='penchat-ring-') as directory:
        Server.requests=[];Server.fail=False;Server.delay=0;Server.verbose=False
        threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(directory)
        fixture(p,server.server_port,rounds=16,budget=4096,image=True)
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
        try:
            before=menu(a);check(before);assert before['value']==1000 and before['estimated_tokens']>before['budget']
            shot(a,f'context-ring-{w}-over-budget')
            a.tap(w-50,90);assert a.diag()['context_usage'] is None
            a.edit('保留草稿');with_draft=menu(a);check(with_draft)
            assert with_draft['estimated_tokens']>before['estimated_tokens'] and a.diag()['draft_length']==4
            # The ring itself retains the original 44px compression action.
            r=with_draft['ring'];a.tap((r[0]+r[2])//2,(r[1]+r[3])//2)
            t.wait_for(lambda:checkpoint(p) is not None);wait_idle(a);a.tap(w-71,h-44)
            after=menu(a);check(after);assert 0<after['value']<1000 and after['estimated_tokens']<with_draft['estimated_tokens']
            assert a.diag()['draft_length']==4;shot(a,f'context-ring-{w}-compressed')
            a.attachment_action('session_info');t.wait_for(lambda:not a.diag()['session_info_busy'] and a.diag()['session_info'])
            info=a.diag()['session_info'];assert info['estimated_tokens']==after['estimated_tokens'] and info['budget']==after['budget']
            assert len(Server.requests)>=1 and all('PenChat_CONTEXT_COMPRESSION_V1' in json.dumps(r[1]) for r in Server.requests)
            a.tap(40,32)
            for _ in range(5):
                a.tap(128,h-34);a.tap(w-40,80);assert not a.diag()['attachments']
            time.sleep(.2);assert a.diag()['view']=='chat' and a.diag()['context_usage'] is None
        finally:a.close();server.shutdown()

def empty():
    for configured in (False,True):
        with tempfile.TemporaryDirectory(prefix='penchat-ring-empty-') as directory:
            p=pathlib.Path(directory)
            if configured:
                seed(p,1)
                with sqlite3.connect(p/'penchat.db') as db:
                    db.execute('update model_configs set capabilities_json=?',(json.dumps({'context_budget':2097152}),))
            t.port=1;a=t.Native(BINARY,p,seed_model=False)
            try:
                data=menu(a)
                if configured:check(data);assert data['value']==0 and data['caption']=='压缩上下文（0.0%）'
                else:assert data['caption']=='压缩上下文（未配置）' and data['value']==0 and data['budget'] is None
                assert a.diag()['summary']['sessions']==0;shot(a,'context-ring-empty-'+str(configured))
            finally:a.close()

if __name__=='__main__':
    empty();run(960,266);run(936,280)
    print('Context usage ring: missing/zero, displayed percentage, bounded arc, draft/image/context estimate, compression click and reduced occupancy, info agreement, late-result/close safety passed at both sizes.')
