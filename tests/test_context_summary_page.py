#!/usr/bin/env python3
"""Compact portrait effort and black summary reader; no model/network requests."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
OUT=t.ROOT/'.work/y07-popup-fixes/screenshots';OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def back(a):
    d=a.diag();a.tap(d['safe_insets'][0]+24,d['safe_insets'][1]+24) if d['height']>d['width'] else a.tap(40,32)

def summary_page(a):
    d=a.diag()
    if not d['attachments']:
        b=d['attachment_button'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    a.attachment_action('summary')

def gauge(a):
    d=a.diag();a.tap(d['send_center_x2']//2-72,d['send_center_y2']//2)
    t.wait_for(lambda:a.diag()['popup']=='effort')

for w,h in [(568,1210),(960,266),(640,260)]:
    for compressed in [False,True]:
        with tempfile.TemporaryDirectory(prefix='penchat-context-reader-') as folder:
            p=pathlib.Path(folder);seed(p,1)
            summary='事实、约束与尚未解决的问题。\n\n'+('这是可滚动、可选择的完整上下文摘要。\n'*70)
            source='保留阅读位置。\n\n'*90
            raw=json.dumps([1,'assistant','complete',source,[]],ensure_ascii=False,separators=(',',':')).encode()
            checksum=14695981039346656037
            for byte in raw+b'\xff':checksum=((checksum^byte)*1099511628211)&((1<<64)-1)
            cp={'version':1,'checksum':f'{checksum:016x}','summary':summary,'through':1,'created_at':1794547200000,'enabled':True,'archive_id':'archive'}
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute("insert into chat_sessions(id,title,status,metadata_json,created_at,updated_at,last_message_at) values('s','Summary fixture','active',?,1,1,1)",(json.dumps({'context_compression':cp} if compressed else {}),))
                db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('a','s',1,'assistant','complete','text',?,1,1)",('保留阅读位置。\n\n'*90,))
                db.execute("insert into app_settings(key,value_json,updated_at) values('active_session_id','\"s\"',1)")
                db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('archive','Pre-compression copy','archived',1,1,1)")
                db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('old','archive',1,'assistant','complete','text','Original',1,1)")
                caps={'reasoning':{'levels':{k:k for k in ['none','minimal','low','medium','high','xhigh','max']}}}
                db.execute("update model_configs set capabilities_json=? where id='a'",(json.dumps(caps),))
            (p/'.updates').mkdir();app=json.loads((t.ROOT/'package.json').read_text())
            (p/'.updates/restore.json').write_text(json.dumps({'appid':app['appid'],'target':app['version'],'snapshot':{'session':'s','draft':'保留草稿','preferences':{'model':'a','gauge':True},'scroll':0,'offset':0,'following':True}}))
            if h>w:os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
            a=t.Native(t.ROOT/'build/host/penchat-lvgl',p,seed_model=False,official_adapter=False,screen={'width':w,'height':h,'direction':0})
            try:
                if h>w:
                    gauge(a);d=a.diag();b=d['popup_bounds'];assert b[3]-b[1]+1<=218 and not d['keyboard'],d
                    capture(a,OUT/f'{w}-effort-compact.png')
                    # Last stop and inherited default remain selectable.
                    a.tap(b[2]-40,b[1]+98);assert a.diag()['effective_effort']=='max'
                    a.tap(b[0]+8,max(16,b[1]-20));assert not a.diag()['attachments']
                if h>w:
                    for y in [500,460,420,380,340,300]:
                        a.send('touch',x=w//2,y=y,pressed=True);time.sleep(.025)
                    time.sleep(.2);a.send('touch',x=w//2,y=300,pressed=False);time.sleep(.2)
                before=a.diag()
                summary_page(a);d=a.diag()
                assert d['view']=='context_summary' and not d['modal'] and not d['attachments']
                capture(a,OUT/f'{w}-summary-{compressed}.png')
                back(a);t.wait_for(lambda:a.diag()['view']=='chat')
                assert abs(a.diag()['scroll_y']-before['scroll_y'])<=10
                summary_page(a)
                if compressed:
                    from PIL import Image
                    image=Image.open(OUT/f'{w}-summary-{compressed}.png').convert('RGB')
                    assert image.getpixel((w-2,h//2))==(0,0,0)
                    assert '对应归档' in d['page']['actions']
                    a.page_action('对应归档');assert a.diag()['view']=='archives'
                    a.to_chat();summary_page(a)
                back(a);t.wait_for(lambda:a.diag()['view']=='chat')
                assert a.diag()['draft_length']==len('保留草稿') and a.diag()['session']=='s'
                assert not a.diag()['generating'] and not a.diag()['keyboard']
            finally:a.close();os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
        print('Compact effort and summary reader passed:',w,h,compressed,flush=True)
