#!/usr/bin/env python3
"""Portrait title/status row, full-width reader and docked keyboard boundaries."""
import os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
from test_scanner_ui import BUNDLE
from image_gestures import capture

BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/portrait-chat-header'
TITLE='会话标题与扫描状态单行展示：'+('较长标题测试'*10)

def run(width,height):
    with tempfile.TemporaryDirectory(prefix='penchat-chat-heading-') as directory:
        folder=pathlib.Path(directory)
        seed(directory,'这段正文应该使用完整的竖屏阅读宽度，顶部只显示会话标题及扫描状态。\n\n'*24)
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute('update chat_sessions set title=?',(TITLE,))
        app=t.Native(BINARY,folder,screen={'width':width,'height':height,'direction':0},official_adapter=False)
        try:
            t.wait_for(lambda:app.diag()['scanner'].get('type')=='ready')
            if height>width:t.wait_for(lambda:app.diag()['chat_header']['scan']=='扫描就绪')
            d=app.diag();head=d['chat_header'];header_height=0
            if height>width:
                assert head['title'].startswith('会话标题与扫描状态单行展示'), head
                assert head['scan']=='扫描就绪' and '\n' not in head['scan']
                header_height=head['bounds'][3]+1
                assert head['reader_bounds'][1]==header_height
                assert head['reader_bounds'][3]==height-1
                assert head['message_width']==width-20
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
            capture(app,OUT/f'{width}-title.png')
            d=app.diag();r=d['composer_text_bounds']
            app.tap((r[0]+r[2])//2,(r[1]+r[3])//2)
            t.wait_for(lambda:app.diag()['editor']['active'] and bool(app.diag()['editor'].get('keys')))
            t.wait_for(lambda:app.diag()['chat_header']['reader_bounds'][3]<app.diag()['composer_bounds'][1])
            d=app.diag();reader=d['chat_header']['reader_bounds']
            assert reader[1]==header_height and reader[3]<d['composer_bounds'][1], (reader,header_height,d['composer_bounds'],d['editor'].get('docked'))
            assert d['chat_header']['title'].startswith('会话标题与扫描状态单行展示')
            time.sleep(.25)
            capture(app,OUT/f'{width}-keyboard.png')
            app.editor_action('done')
            t.wait_for(lambda:not app.diag()['editor']['active'])
            assert app.diag()['chat_header']['reader_bounds'][3]==height-1
            app.open_settings();app.to_chat()
            assert app.diag()['chat_header']['title'].startswith('会话标题与扫描状态单行展示')
        finally:app.close()

if __name__=='__main__':
    env={'PENCHAT_SCANNER_BINARY':str(t.ROOT/'build/scanner-host/penchat-scanner'),
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
    print('Portrait title/status, full-width reader, keyboard/return and unchanged landscape passed')
