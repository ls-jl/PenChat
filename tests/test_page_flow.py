#!/usr/bin/env python3
"""Every section uses a flowing title/action row and an unobstructed viewport."""
import json,os,pathlib,signal,sqlite3,struct,tempfile,time,zlib
import test_lvgl_app as t
from test_reasoning_ui import seed
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/page-flow';OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def png(path):
    def chunk(k,v):return struct.pack('>I',len(v))+k+v+struct.pack('>I',zlib.crc32(k+v))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',32,24,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([80,130,160])*32)*24))+chunk(b'IEND',b''))

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-page-flow-') as directory:
        p=pathlib.Path(directory);seed(p,1)
        presets=[{'id':'default','name':'默认预设','prompt_mode':'inherit'}]+[
            {'id':f'fixture-{i}','name':f'测试预设 {i}','prompt_mode':'inherit'} for i in range(12)]
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("insert into app_settings values('chat_presets',?,1)",(json.dumps(presets),))
            for i in range(24):
                id=f'fixture-session-{i}';status='archived' if i<12 else 'active'
                db.execute('insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,?,1,?,1)',(id,'测试会话 '+str(i),status,i+1))
                db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,?,1,'user','complete','text','fixture',1,1)",(id+'-message',id))
        (p/'DCIM').mkdir()
        for i in range(12):png(p/'DCIM'/f'fixture-{i}.png')
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
        def shot(name):
            file=p/'lvgl-diagnostic.png'
            if file.exists():file.unlink()
            a.process.send_signal(signal.SIGUSR1);t.wait_for(file.exists)
            (OUT/f'{w}-{h}-{name}.png').write_bytes(file.read_bytes())
        def top():
            t.wait_for(lambda:not a.diag()["page"]["scrolling"])
            for _ in range(30):
                if a.diag()['scroll_y']<=0:
                    t.wait_for(lambda:a.diag()['scroll_y']==0);return
                a.settings_swipe(120);t.wait_for(lambda:not a.diag()["page"]["scrolling"])
            raise AssertionError('Cannot return to title')
        def inspect(name,scroll=True):
            d=a.diag();page=d['page'];assert page and page['viewport'][1:4:2]==[0,h-1],name
            assert page['heading'][1]==7,name
            for bounds in page['actions'].values():
                assert page['heading'][2]<bounds[0] and bounds[3]-bounds[1]+1>=44,(name,page)
            shot(name+'-top')
            if scroll:
                maximum=d['scroll_bottom']+d['scroll_y'];a.settings_swipe(-120);t.wait_for(lambda:not a.diag()["page"]["scrolling"])
                d=a.diag();page=d['page']
                assert page['heading'][1]==7-d['scroll_y'],(name,d['scroll_y'],page)
                if maximum>=48:assert page['heading'][3]<0,(name,maximum,d['scroll_y'])
                for bounds in page['actions'].values():assert bounds[1]==2-d['scroll_y']
                shot(name+'-scrolled')
            top()
        try:
            a.open_settings();inspect('settings')
            a.setting('models');inspect('provider-list')
            a.tap(870,120);assert a.diag()['view']=='model';inspect('model')
            a.page_action('参数映射');inspect('model-mappings')
            a.page_action('保存');assert a.diag()['view']=='provider'
            a.page_action('添加 Provider');inspect('provider-form')
            a.settings_swipe(-100);t.wait_for(lambda:not a.diag()["page"]["scrolling"]);before=a.diag()['scroll_y']
            # A same-form rebuild must preserve the reading position and keep
            # the title/actions in the flow, rather than jumping to a pinned bar.
            a.tap(w-180,238-before);time.sleep(.15)
            assert a.diag()['view']=='provider' and a.diag()['scroll_y']==before
            a.to_chat();a.open_settings();a.setting('presets');inspect('presets')
            a.tap(300,140);assert a.diag()['view']=='preset_edit';inspect('preset-editor')
            a.page_action('保存');assert a.diag()['view']=='presets'
            a.to_chat();a.open_settings();a.setting('archives');inspect('archives')
            a.to_chat();a.tap(128,h-34);a.tap(200,82);assert a.diag()['view']=='album';inspect('album')
            a.to_chat();a.open_settings();a.setting('transfer');t.wait_for(lambda:not a.diag()['transfer_busy']);inspect('transfer')
            a.tap(250,105);t.wait_for(lambda:not a.diag()['transfer_busy'])
            assert a.diag()['transfer_screen']=='export_chats';inspect('transfer-export')
            # Camera and crop have no passive page title bar already.
            a.to_chat();a.tap(128,h-34);a.tap(200,38);t.wait_for(lambda:a.diag()['view']=='camera')
            assert a.diag()['page'] is None;shot('camera')
            a.send('camera.test',photo={'path':str(p/'DCIM/fixture-0.png'),'width':32,'height':24})
            t.wait_for(lambda:a.diag()['view']=='crop');assert a.diag()['page'] is None;shot('crop')
        finally:a.close()

if __name__=='__main__':
    for w,h in [(960,266),(936,280)]:run(w,h)
    print('Section pages: titles/actions flow with content; full-height menus, forms, archives, album, transfer and workspaces checked at both device sizes.')
