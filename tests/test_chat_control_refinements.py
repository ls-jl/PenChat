#!/usr/bin/env python3
"""Arrow-only reasoning, unified history footer and doubled portrait pull travel."""
import json,math,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
from provider_ui import capture
OUT=t.ROOT/'.work/chat-control-refinements';OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def tap(a,b):a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def reasoning(a):return a.diag()['reasoning_controls'][0]
def check_reason(a,w,tag):
    c=reasoning(a);assert not c['expanded'] and c['angle']==0
    tap(a,c['title']);assert not reasoning(a)['expanded']
    tap(a,c['arrow']);t.wait_for(lambda:reasoning(a)['expanded'] and reasoning(a)['angle']==900)
    t.wait_for(lambda:reasoning(a)['detail'] and reasoning(a)['detail'][3]>reasoning(a)['detail'][1]);time.sleep(.08)
    capture(a,OUT,f'{w}-{tag}-expanded')
    tap(a,reasoning(a)['title']);assert reasoning(a)['expanded']
    for _ in range(3):
        tap(a,reasoning(a)['arrow']);t.wait_for(lambda:not reasoning(a)['expanded'] and reasoning(a)['angle']==0)
        tap(a,reasoning(a)['arrow']);t.wait_for(lambda:reasoning(a)['expanded'] and reasoning(a)['angle']==900)
    tap(a,reasoning(a)['arrow']);t.wait_for(lambda:not reasoning(a)['expanded'] and reasoning(a)['angle']==0)
    tap(a,reasoning(a)['arrow']);tap(a,reasoning(a)['arrow'])
    t.wait_for(lambda:not reasoning(a)['expanded'] and reasoning(a)['angle']==0)
    time.sleep(.08);capture(a,OUT,f'{w}-{tag}-collapsed')

def history(a):
    d=a.diag()
    if d['navigation_open']:
        t.wait_for(lambda:not a.diag().get('surface_motion',False));return
    x=d['safe_insets'][0]+24 if d['height']>d['width'] else 40
    y=d['safe_insets'][1]+24 if d['height']>d['width'] else 32
    a.tap(x,y);t.wait_for(lambda:a.diag()['navigation_open'])
    t.wait_for(lambda:not a.diag().get('surface_motion',False))

for w,h in [(960,266),(640,260),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-chat-controls-') as folder:
        p=pathlib.Path(folder);seed(p,'简短回答。')
        reason='公开思考。\n\n- 先核实条件\n- 再给出结论\n\n公式：$x^2+1$'
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("update chat_messages set reasoning_text=?,metadata_json=?",(reason,json.dumps({'reasoning_duration_ms':12000})))
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('archive','归档测试','archived',1,1,1)")
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,reasoning_text,metadata_json,created_at,updated_at) values('archived-reply','archive',1,'assistant','complete','text','归档回答',?,'{}',1,1)",(reason,))
        previous=os.environ.get('PENCHAT_TEST_Y07_ROUNDED')
        if h>w:os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',p,seed_model=False,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            d=a.diag();x=w//2;y=d['composer_bounds'][1]-12
            a.send('touch',x=x,y=y,pressed=True);g=a.diag()
            extent=min(g['pull_reader_height'],(w-4)/2) if h>w else g['pull_reader_height']
            old=math.floor(142/.85*extent/208+.5)
            assert g['pull_full_distance']==old*(2 if h>w else 1),(g['pull_full_distance'],old)
            a.send('touch',x=x,y=y,pressed=False,cancelled=True);time.sleep(.2)
            check_reason(a,w,'chat')
            history(a);d=a.diag();panel=w*2//3
            b=d['navigation_footer']['settings'];assert b['bounds'][2]<panel
            for part in ['caption','glyph']:
                p0=b[part];outer=b['bounds'];assert outer[0]<=p0[0]<=p0[2]<=outer[2] and outer[1]<=p0[1]<=p0[3]<=outer[3],b
            assert b['caption'][2]-b['caption'][0]+1>=40,b
            capture(a,OUT,f'{w}-history-footer')
            # Icon and text both press the same outer control and open Settings.
            for part in ['glyph','caption']:
                b=a.diag()['navigation_footer']['settings'];p0=b[part];x=(p0[0]+p0[2])//2;y=(p0[1]+p0[3])//2
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.08)
                assert a.diag()['navigation_footer']['settings']['pressed']
                capture(a,OUT,f'{w}-settings-pressed-{part}')
                a.send('touch',x=x,y=y,pressed=False);t.wait_for(lambda:a.diag()['view']=='settings')
                if part=='glyph':
                    a.tap(d['safe_insets'][0]+24,d['safe_insets'][1]+24) if h>w else a.tap(40,32)
                    t.wait_for(lambda:a.diag()['view']=='chat');history(a)
            a.setting('archives');views=a.diag()['archive_views'];tap(a,views['archive'])
            t.wait_for(lambda:a.diag()['view']=='archive_chat');check_reason(a,w,'archive')
        finally:
            a.close()
            if previous is None:os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
            else:os.environ['PENCHAT_TEST_Y07_ROUNDED']=previous
        print('Chat refinements passed:',w,h,flush=True)
print('ARROW_ONLY_ANIMATED_REASONING_UNIFIED_FOOTER_SAFE_BOUNDS_AND_PORTRAIT_DOUBLE_TRAVEL_OK')
