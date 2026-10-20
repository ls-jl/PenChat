#!/usr/bin/env python3
"""Supplement functional-test screenshots with every form and empty state."""
import argparse,json,os,pathlib,signal,sqlite3,struct,tempfile,time,zlib
from test_lvgl_app import Native,ROOT,wait_for
from test_chat_style import drag
import test_lvgl_app as transport
parser=argparse.ArgumentParser();parser.add_argument('--output',required=True);parser.add_argument('--legacy',action='store_true');args=parser.parse_args()
out=pathlib.Path(args.output);out.mkdir(parents=True,exist_ok=True)
transport.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
def snap(app,name):
    app.process.send_signal(signal.SIGUSR1);time.sleep(.2)
    (out/(name+'.png')).write_bytes((app.directory/'lvgl-diagnostic.png').read_bytes())
with tempfile.TemporaryDirectory(prefix='penchat-audit-') as directory:
    p=pathlib.Path(directory);app=Native(binary,p)
    try:
        app.open_settings();app.tap(300,192);snap(app,'title-model-picker');app.tap(900,232)
        app.tap(180,80);app.tap(870,70);snap(app,'provider-edit')
        app.tap(430,120);app.receive('ocr.start');snap(app,'keyscan')
        app.tap(40,32);app.tap(40,32);app.tap(790,24);snap(app,'provider-new')
        app.tap(*( (505,223) if args.legacy else (870,24)));time.sleep(.2);snap(app,'validation-dialog')
        app.tap(890,225);app.to_chat()
        app.open_settings();app.tap(740,80);snap(app,'archives-empty');app.to_chat()
        app.close()
        with sqlite3.connect(p/'penchat.db') as db:
            presets=[{'id':'default','name':'默认预设','prompt_mode':'custom','prompt':'用简洁的中文解释问题。','model_id':'fixture-model','effort':'high'}]
            db.execute("insert or replace into app_settings(key,value_json,updated_at) values('chat_presets',?,1)",(json.dumps(presets),))
        app=Native(binary,p);app.open_settings()
        if args.legacy:drag(app,[210,160,100],x=300);app.tap(300,190)
        else:app.tap(300,230)
        assert app.diag()['view']=='presets'
        app.tap(300,166);assert app.diag()['view']=='preset_edit'
        snap(app,'preset-editor-top')
        app.tap(730,76);snap(app,'preset-prompt-options');app.tap(890,225)
        app.tap(300,134);snap(app,'preset-model-options');app.tap(890,225)
        app.tap(730,134);snap(app,'preset-effort-options');app.tap(890,225)
        drag(app,[220,170,100,60],x=700);time.sleep(.3);snap(app,'preset-editor-bottom')
        app.to_chat()
        folder=p/'DCIM';folder.mkdir(exist_ok=True)
        def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
        for i in range(4):
            w,h=240,150
            data=b''.join(b'\0'+b''.join(bytes([(x+i*40)%256,(y*2)%256,100]) for x in range(w)) for y in range(h))
            (folder/f'样例照片_{i}.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b''))
        app.tap(128,232);app.tap(200,82);snap(app,'album-populated')
    finally:app.close()
with tempfile.TemporaryDirectory(prefix='penchat-empty-audit-') as directory:
    app=Native(binary,pathlib.Path(directory),seed_model=False)
    try:
        snap(app,'welcome-unconfigured')
        app.open_settings();app.tap(180,80);snap(app,'providers-empty')
    finally:app.close()
print('Supplementary page screenshots:',out)
