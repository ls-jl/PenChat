#!/usr/bin/env python3
"""Real ORANGE touches with isolated DB, assets and transfer folders."""
import base64,json,pathlib,shlex,sqlite3,sys,tempfile,time,zipfile
import acceptance as d
from chat_acceptance import close_app,start,pull,push,isolate_fixture_assets,restore_fixture_assets
sys.path.insert(0,str(d.ROOT/'tests'))
from test_reasoning_ui import seed
OUT=d.ROOT/'.work/transfer-device';OUT.mkdir(parents=True,exist_ok=True)
ROOT='/userdisk/Favorite/PenChat';SAVED='/userdisk/Favorite/.PenChat.transfer-acceptance-saved'
def ready(screen):return d.wait(lambda: not d.state()['transfer_busy'] and d.state()['transfer_screen']==screen,timeout=20)
def capture(name):
    time.sleep(.3);d.state(screenshot=True);pull(d.DATA+'/lvgl-diagnostic.png',OUT/(name+'.png'))
def home():
    for _ in range(8):
        screen=d.state()['transfer_screen']
        if screen=='home':return
        if screen=='done':d.tap(300,212)
        else:d.tap(40,32)
        time.sleep(.12)
    raise AssertionError('Transfer home unavailable')
def swipe(y1,y2,x=550):d.adb('/tmp/penchat-ui-tap '+str(y1+113)+' '+str(959-x)+' '+str(y2+113)+' '+str(959-x)+' 2>/dev/null');time.sleep(.25)
if __name__=='__main__':
    state=d.state();assert state['view']=='chat' and not state['keyboard'] and not state['editor']['active'] and not state['generating'] and state['draft_length']==0
    host=d.adb('pidof miniapp');close_app();pull(d.DATA+'/penchat.db',OUT/'before.db')
    isolated=False;folders=False;results=[]
    try:
        isolate_fixture_assets();isolated=True
        assert d.adb('test ! -e '+SAVED+' && echo ready')=='ready'
        assert d.adb('mv '+ROOT+' '+SAVED+' && echo saved')=='saved';folders=True
        with tempfile.TemporaryDirectory() as temp:
            folder=pathlib.Path(temp);seed(folder,1)
            preset={'id':'default','name':'默认预设','prompt_mode':'custom','prompt':'导入导出测试提示','model_id':'a','model_when':'always','effort':'high','effort_when':'empty'}
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at,default_model_config_id,default_provider_id,message_count,metadata_json) values('transfer-device','导入导出设备测试','active',1,1,1,'a','p',4,?)",(json.dumps({'title_state':'manual','chat_preferences':{'model_id':'a','system_prompt':'会话提示'}}),))
                for seq,role,text in [(1,'user','验证图片与公式'),(2,'assistant','**公开回复** $x^2$'),(3,'user',''),(4,'assistant','图片已收到')]:
                    db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,reasoning_text,metadata_json,created_at,updated_at) values(?,'transfer-device',?,?,'complete',?,?,?,?,1,1)",('t'+str(seq),seq,role,'image' if seq==3 else 'text',text,'公开思考' if seq==2 else '',json.dumps({'reasoning_duration_ms':4200,'reasoning_observed':True,'reasoning_complete':True,'response_reasoning_items':[{'encrypted_content':'opaque-test-only'}]}) if seq==2 else '{}'))
                    db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,text,asset_id) values(?,'transfer-device',?,0,?,?,?)",('part'+str(seq),'t'+str(seq),'image' if seq==3 else 'text',text,'photo' if seq==3 else None))
                image=d.DATA+'/assets/fixture.png'
                db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,width,height,created_at) values('photo','transfer-device','t3','image','import',?,?,1,1,1)",(image,image))
                for key,value in [('active_session_id','transfer-device'),('auto_title_enabled',False),('chat_presets',[preset])]:db.execute('insert into app_settings(key,value_json,updated_at) values(?,?,1)',(key,json.dumps(value)))
            png=folder/'fixture.png';png.write_bytes(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Zl1sAAAAASUVORK5CYII='))
            push(png,image);push(folder/'penchat.db',d.DATA+'/penchat.db')
        start();d.open_settings();d.setting("transfer");assert d.state()['view']=='transfer';capture('home')
        assert len(d.adb('find '+ROOT+' -type d').splitlines())==7
        d.tap(300,108);ready('export_chats');d.wait(lambda:len(d.state()['transfer_data'].get('items',[]))==1)
        d.tap(300,184);capture('chat-selected');d.tap(300,72);ready('done');export=d.state()['transfer_data']['path'];capture('export-done')
        pull(export,OUT/'roundtrip.penchat.zip')
        with zipfile.ZipFile(OUT/'roundtrip.penchat.zip') as z:
            text=b'\n'.join(z.read(n) for n in z.namelist());assert b'opaque-test-only' not in text and b'test-only' not in text
            assert any(n.startswith('assets/') for n in z.namelist())
        results.append('chat-export-with-image-and-no-credentials')
        target=ROOT+'/导入/聊天/设备测试.penchat.zip';d.adb('cp -- '+shlex.quote(export)+' '+shlex.quote(target))
        home();d.tap(740,108);ready('import_chats');d.wait(lambda:len(d.state()['transfer_data'].get('items',[]))==1);d.tap(300,128);ready('preview');capture('chat-preview');d.tap(300,140);ready('done');assert d.state()['transfer_data']['added']==1;results.append('chat-import-roundtrip')
        home();d.tap(300,168);ready('export_presets');d.wait(lambda:len(d.state()['transfer_data'].get('items',[]))==1);d.tap(740,72);ready('done');export=d.state()['transfer_data']['path']
        d.adb('cp -- '+shlex.quote(export)+' '+shlex.quote(ROOT+'/导入/预设/设备预设.penchat-presets.json'))
        home();d.tap(740,168);ready('import_presets');d.wait(lambda:len(d.state()['transfer_data'].get('items',[]))==1);d.tap(300,128);ready('preview');capture('preset-preview')
        d.tap(300,140);d.wait(lambda:d.state()['modal']);capture('default-confirmation');d.tap(890,222);d.wait(lambda:not d.state()['modal'])
        swipe(230,130);print('checkbox scroll',d.state()['scroll_y'],flush=True);capture('preset-scrolled');d.tap(300,256-int(d.state()['scroll_y']));capture('preset-after-checkbox')
        swipe(90,230);swipe(90,230);capture('preset-before-import');print('top scroll',d.state()['scroll_y'],flush=True);d.tap(300,140);ready('done');assert d.state()['transfer_data']['added']==1;results.append('preset-export-and-explicit-default-confirmation')
        home();d.tap(40,32);d.to_chat();d.open_navigation();row=d.state()['history_rows'][0]
        d.adb('/tmp/penchat-ui-tap --hold '+str(row['y']+113)+' '+str(959-row['x'])+' 650 2>/dev/null');d.wait(lambda:d.state()['modal']);capture('history-export');d.tap(670,147);ready('export_chats');results.append('history-single-row-export-entry')
        pull(d.DATA+'/penchat.db',OUT/'check.db')
        with sqlite3.connect(OUT/'check.db') as db:
            assert db.execute('select count(*) from chat_sessions').fetchone()[0]==2
            assert not db.execute('pragma foreign_key_check').fetchall()
            assert db.execute("select count(*) from chat_messages where reasoning_text='公开思考'").fetchone()[0]==2
        assert d.adb('pidof miniapp')==host
    except BaseException:
        if d.pid():
            capture('failure')
            state=d.state();print({k:state.get(k) for k in ['view','transfer_screen','transfer_busy','transfer_error','modal','scroll_y','touch']},flush=True)
        raise
    finally:
        close_app();push(OUT/'before.db',d.DATA+'/penchat.db')
        if isolated:restore_fixture_assets()
        if folders:
            assert d.adb('test -d '+SAVED+' && echo saved')=='saved'
            d.adb('rm -rf -- '+ROOT+'; mv '+SAVED+' '+ROOT)
        start();pull(d.DATA+'/penchat.db',OUT/'restored.db')
        with sqlite3.connect(OUT/'before.db') as before,sqlite3.connect(OUT/'restored.db') as after:
            for table in ['chat_sessions','chat_messages','message_parts','message_assets','providers','model_configs','app_settings']:
                assert before.execute('select * from '+table+' order by 1').fetchall()==after.execute('select * from '+table+' order by 1').fetchall(),table
        assert d.adb('pidof miniapp')==host
        for name in ['before.db','check.db','restored.db']:
            (OUT/name).unlink(missing_ok=True)
        d.adb('rm -f /tmp/penchat-db-snapshot.db')
        (OUT/'results.json').write_text(json.dumps({'passed':results,'original_data_restored':True,'host_unchanged':True},ensure_ascii=False,indent=2)+'\n')
    print('ORANGE transfer: '+', '.join(results)+'; original DB/assets/folders restored, host unchanged.')
