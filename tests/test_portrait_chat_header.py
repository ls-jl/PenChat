#!/usr/bin/env python3
"""Portrait title/status row, full-width reader and docked keyboard boundaries."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
from test_scanner_ui import BUNDLE
from image_gestures import capture

BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/portrait-chat-header'
TITLE='会话标题与扫描状态单行展示：'+('较长标题测试'*10)

def run(width,height,ui=100,text=100):
    with tempfile.TemporaryDirectory(prefix='penchat-chat-heading-') as directory:
        folder=pathlib.Path(directory)
        seed(directory,'这段正文应该使用完整的竖屏阅读宽度，顶部只显示会话标题及扫描状态。\n\n'*24)
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute('update chat_sessions set title=?',(TITLE,))
            for key,value in [('ui_scale_percent',ui),('reading_font_percent',text)]:db.execute('insert into app_settings values(?,?,1)',(key,json.dumps(value)))
        app=t.Native(BINARY,folder,screen={'width':width,'height':height,'direction':0},official_adapter=False)
        try:
            t.wait_for(lambda:app.diag()['scanner'].get('type')=='ready')
            if height>width:t.wait_for(lambda:app.diag()['chat_header']['scan']=='扫描就绪')
            d=app.diag();head=d['chat_header'];header_height=0
            scan=head['scan_metrics'];bounds=head['scan_bounds']
            assert bounds[2]-bounds[0]+1>=scan['width'] and bounds[3]-bounds[1]+1>=scan['height'],(ui,text,head)
            if height>width:
                assert head['title'].startswith('会话'), head
                assert head['scan']=='扫描就绪' and '\n' not in head['scan']
                header_height=head['bounds'][3]+1
                assert head['reader_bounds'][1]==header_height
                assert head['reader_bounds'][3]==height-1
                assert head['message_width']==width-(20*ui+50)//100
                assert head['title_bounds'][2]<head['scan_bounds'][0]
                assert head['title_bounds'][3]<header_height
                old_scroll=d['scroll_y']
                for y in range(320,621,50):
                    app.send('touch',x=width//2,y=y,pressed=True);time.sleep(.025)
                app.send('touch',x=width//2,y=620,pressed=False)
                t.wait_for(lambda:not app.diag()['reader_interacting'])
                assert app.diag()['scroll_y']<old_scroll
                assert app.diag()['chat_header']['bounds']==head['bounds']
            else:
                assert head['bounds'] is None and head['message_width']==width-112
                return
            capture(app,OUT/f'{width}-{ui}-{text}-title.png')
            d=app.diag();r=d['composer_text_bounds']
            app.tap((r[0]+r[2])//2,(r[1]+r[3])//2)
            t.wait_for(lambda:app.diag()['editor']['active'] and bool(app.diag()['editor'].get('keys')))
            t.wait_for(lambda:not app.diag()['surface_motion'])
            d=app.diag();reader=d['chat_header']['reader_bounds']
            assert reader[1]==header_height and reader[3]==height-1, (reader,header_height,d['composer_bounds'],d['editor'].get('docked'))
            assert d['chat_header']['title'].startswith('会话')
            time.sleep(.25)
            capture(app,OUT/f'{width}-{ui}-{text}-keyboard.png')
            app.editor_action('done')
            t.wait_for(lambda:not app.diag()['editor']['active'])
            assert app.diag()['chat_header']['reader_bounds'][3]==height-1
            app.open_settings();app.to_chat()
            assert app.diag()['chat_header']['title'].startswith('会话')
            app.open_navigation();t.wait_for(lambda:not app.diag()['surface_motion']);d=app.diag()
            rows=[]
            for key in ['newchat','settings']:
                control=d['navigation_footer'][key];caption=control['caption'];metrics=control['text_metrics']
                assert caption[2]-caption[0]+1>=metrics['width'] and caption[3]-caption[1]+1>=metrics['height'],(ui,text,key,control)
                rows.append(control['bounds'])
            assert d['navigation_footer']['newchat']['text_metrics']['text']=='新聊天'
            assert d['navigation_footer']['settings']['text_metrics']['text']=='设置'
            rows.append(d['balances']['shortcut'])
            for i,a in enumerate(rows):
                for b in rows[i+1:]:assert a[2]<b[0] or b[2]<a[0] or a[3]<b[1] or b[3]<a[1],(ui,text,rows)
            capture(app,OUT/f'{width}-{ui}-{text}-footer.png')
            print(width,height,ui,text,'footer and scan captions fit',flush=True)
        finally:app.close()

if __name__=='__main__':
    env={'PENCHAT_TEST_Y07_ROUNDED':'1','PENCHAT_SCANNER_BINARY':str(t.ROOT/'build/scanner-host/penchat-scanner'),
         'PENCHAT_SCAN_RESOURCES':str(t.ROOT/'assets/scanner'),'PENCHAT_SCANNER_TEST':'1',
         'LD_LIBRARY_PATH':str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread')}
    t.port=1;OUT.mkdir(parents=True,exist_ok=True)
    wrapper=OUT/'scanner-y07'
    worker=str(t.ROOT/'build/scanner-host/penchat-scanner')
    wrapper.write_text('#!/usr/bin/env python3\nimport os,sys\nworker='+repr(worker)+'\nos.execv(worker,[worker]+["--simulate-y07" if a=="--simulate" else a for a in sys.argv[1:]])\n')
    wrapper.chmod(0o700)
    env['PENCHAT_SCANNER_BINARY']=str(wrapper)
    os.environ.update(env)
    for size in [(568,1210),(640,260),(960,266)]:run(*size)
    for ui,text in [(100,125),(100,150),(125,150),(150,150)]:run(568,1210,ui,text)
    print('Portrait title/status, full-width reader, keyboard/return and unchanged landscape passed')
