#!/usr/bin/env python3
"""Bundled format docs, real sample imports and native responsive readers."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import signal
import sqlite3
import subprocess
import tempfile
import time
import zipfile

import test_lvgl_app as t
ROOT = t.ROOT
t.port = 1
BINARY = Path(os.environ.get('PENCHAT_TEST_BINARY', ROOT/'build/host/penchat-lvgl'))
DRIVER = Path(os.environ.get('PENCHAT_TRANSFER_TEST_BINARY', ROOT/'build/host/penchat-transfer-tests'))
OUT = ROOT/'.work/format-0.2.33'
OUT.mkdir(parents=True, exist_ok=True)
module = importlib.util.spec_from_file_location('format_generator', ROOT/'scripts/compile-transfer-format.py')
generator = importlib.util.module_from_spec(module)
module.loader.exec_module(generator)
expected = generator.resources()


def service():
    with tempfile.TemporaryDirectory(prefix='penchat-format-service-') as directory:
        base = Path(directory);data = base/'data';folder = base/'public'
        child = subprocess.Popen([str(DRIVER), str(data), str(folder)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        def call(op, args=None, error=False):
            child.stdin.write(json.dumps({'op': op, 'args': args or {}})+'\n');child.stdin.flush()
            result = json.loads(child.stdout.readline())
            assert (result['type']=='transfer_error') == error, result
            return result['data'].get('data', result['data'])
        try:
            call('init')
            with sqlite3.connect(data/'penchat.db') as db:
                db.execute('insert or replace into app_settings values(?,?,1)', ('private_fixture', json.dumps('NEVER_INCLUDE_USER_DATA')))
            before = hashlib.sha256((data/'penchat.db').read_bytes()).hexdigest()
            for topic, name in [('chats','聊天格式.md'),('presets','预设格式.md'),('markdown','Markdown导入.md')]:
                doc = call('format', {'topic': topic})
                assert doc['text'].encode() == expected[name] and doc['format_version']==1
            # Docs remain readable with unavailable public storage.
            folder.rename(base/'saved-public');folder.write_text('not a directory')
            assert call('format', {'topic':'chats'})['format_version']==1
            call('export_format', error=True)
            folder.unlink();(base/'saved-public').rename(folder)
            call('format', {'topic':'unknown'}, error=True)
            first = Path(call('export_format')['path']);second = Path(call('export_format')['path'])
            assert first != second and first.exists() and second.exists()
            assert first.parent == folder/'导出/格式规范'
            with zipfile.ZipFile(first) as z:
                assert set(z.namelist())==set(expected) and z.testzip() is None
                for name, value in expected.items():assert z.read(name)==value, name
                assert b'NEVER_INCLUDE_USER_DATA' not in b''.join(z.read(n) for n in z.namelist())
            assert hashlib.sha256((data/'penchat.db').read_bytes()).hexdigest()==before
            assert not (data/'transfer-tmp').exists()
            # Import the actual exported samples. No providers/models/API needed.
            inbox = folder/'导入/聊天';presets = folder/'导入/预设'
            (inbox/'sample.penchat.zip').write_bytes(expected['示例/示例聊天.penchat.zip'])
            preview = call('preview', {'kind':'chats','name':'sample.penchat.zip'})
            assert len(preview['items'])==1 and not preview['missing']
            assert call('import', {'indices':[0]})['added']==1
            preview = call('preview', {'kind':'chats','name':'sample.penchat.zip'})
            assert preview['items'][0]['duplicate']
            assert '重复' in call('import', {'indices':[0]}, error=True)['error']
            original_default = call('choices', {'kind':'presets'})['items'][0]
            (presets/'sample.penchat-presets.json').write_bytes(expected['示例/示例预设.penchat-presets.json'])
            preview = call('preview', {'kind':'presets','name':'sample.penchat-presets.json'})
            assert len(preview['items'])==1 and not preview.get('default_diff')
            assert call('import', {'indices':[0]})['added']==1
            assert call('choices', {'kind':'presets'})['items'][0] == original_default
            (inbox/'示例参考.md').write_bytes(expected['示例/示例参考.md'])
            (inbox/'图片').mkdir();(inbox/'图片/示例.png').write_bytes(expected['示例/图片/示例.png'])
            preview = call('preview', {'kind':'chats','name':'示例参考.md'})
            assert not preview['missing'];assert call('import', {'indices':[0]})['added']==1
            with sqlite3.connect(data/'penchat.db') as db:
                assert db.execute('select count(*) from providers').fetchone()[0]==0
                assert db.execute('select count(*) from chat_sessions').fetchone()[0]==2
                assert db.execute('select count(*) from chat_messages').fetchone()[0]==3
                assert db.execute('select count(*) from message_assets').fetchone()[0]==2
                stored=json.loads(db.execute("select value_json from app_settings where key='chat_presets'").fetchone()[0])
                assert '格式规范示例预设' in json.dumps(stored,ensure_ascii=False)
            # Published enum contract: the reader must not imply an importable
            # effort that the version-1 preset validator rejects.
            document=call('format', {'topic':'presets'})['text']
            for effort in ['', 'none', 'api_default', 'low', 'medium', 'high', 'xhigh', 'max', 'minimal']:
                sample=json.loads(expected['示例/示例预设.penchat-presets.json'])
                sample['items'][0]['effort']=effort
                sample['items'][0]['origin']['checksum']='enum-'+effort
                (presets/'enum.penchat-presets.json').write_text(json.dumps(sample))
                result=call('preview', {'kind':'presets','name':'enum.penchat-presets.json'}, error=effort=='minimal')
                if effort=='minimal':assert '推理强度' in result['error'] and '不接受minimal' in document
                elif effort:assert effort in document
            # Symlink target rejected; existing export left intact.
            output=folder/'导出/格式规范';output.rename(folder/'saved-specs')
            output.symlink_to(base,target_is_directory=True);call('export_format',error=True)
            output.unlink();(folder/'saved-specs').rename(output)
            if 'ASAN_OPTIONS' not in os.environ:
                source=base/'no-space.c';library=base/'no-space.so'
                source.write_text('#include <sys/statvfs.h>\n#include <string.h>\nint statvfs(const char*p,struct statvfs*s){(void)p;memset(s,0,sizeof(*s));s->f_frsize=4096;return 0;}\n')
                subprocess.run(['cc','-shared','-fPIC',str(source),'-o',str(library)],check=True)
                env=dict(os.environ,LD_PRELOAD=str(library))
                failed=subprocess.run([str(DRIVER),str(base/'space-data'),str(base/'space-public')],
                  input=json.dumps({'op':'export_format'})+'\n',capture_output=True,text=True,env=env,check=True,timeout=10)
                assert '空间不足' in json.loads(failed.stdout)['data']['error']
                assert not list((base/'space-data').rglob('*.part'))
        finally:
            child.stdin.close();assert child.wait(timeout=10)==0
    print('Format resources: exact docs/ZIP, unique files, unavailable storage, privacy, real sample imports, duplicate detection and symlink rejection passed.')


def ui(width,height):
    with tempfile.TemporaryDirectory(prefix='penchat-format-ui-') as directory:
        data=Path(directory)
        if width==936:
            schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
            with sqlite3.connect(data/'penchat.db') as db:
                db.executescript(schema)
                db.execute("insert into app_settings values('persistent_export_directory','false',1)")
        previous_preload=os.environ.get('LD_PRELOAD')
        delayed='ASAN_OPTIONS' not in os.environ
        if delayed:
            source=data/'slow-format.c';library=data/'slow-format.so'
            source.write_text('''#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
int open(const char *p,int flags,...){
 static int(*next)(const char*,int,...);if(!next)next=dlsym(RTLD_NEXT,"open");
 mode_t mode=0;if(flags&O_CREAT){va_list a;va_start(a,flags);mode=va_arg(a,int);va_end(a);}
 if(p && strstr(p,"/format.part") && (flags&O_CREAT))usleep(1000000);
 return next(p,flags,mode);
}
''')
            subprocess.run(['cc','-shared','-fPIC',str(source),'-ldl','-o',str(library)],check=True)
            os.environ['LD_PRELOAD']=str(library)+((':'+previous_preload) if previous_preload else '')
        try:
            app=t.Native(BINARY,data,seed_model=False,official_adapter=True,screen={'width':width,'height':height,'direction':0})
        finally:
            if previous_preload is None:os.environ.pop('LD_PRELOAD',None)
            else:os.environ['LD_PRELOAD']=previous_preload
        def ready(screen):t.wait_for(lambda:not app.diag()['transfer_busy'] and app.diag()['transfer_screen']==screen)
        def scroll(delta):
            x=width//2;y=min(height-30,220) if delta<0 else 75
            for i in range(8):
                app.send('touch',x=x,y=y+delta*i//7,pressed=True);time.sleep(.02)
            time.sleep(.12);app.send('touch',x=x,y=y+delta,pressed=False);time.sleep(.16)
        def click(name):
            for _ in range(30):
                d=app.diag();b=d['transfer_actions'][name]
                if b[1]>=0 and b[3]<height:
                    assert b[3]-b[1]+1>=44
                    app.tap((b[0]+b[2])//2,(b[1]+b[3])//2);return
                scroll(-120 if b[3]>=height else 120)
            raise AssertionError(('unreachable action',name,app.diag()))
        def back():
            if height > width:
                for i in range(10):
                    app.send('touch',x=5+i*(width//4)//9,y=height//2,pressed=True);time.sleep(.025)
                app.send('touch',x=5+width//4,y=height//2,pressed=False);time.sleep(.15)
            else:app.tap(35,28)
        def shot(name):
            path=data/'lvgl-diagnostic.png'
            if path.exists():path.unlink()
            app.process.send_signal(signal.SIGUSR1);t.wait_for(path.exists)
            (OUT/f'{width}-{height}-{name}.png').write_bytes(path.read_bytes())
        try:
            # Keep a draft without relying on screen-specific official editor coordinates.
            app.edit('keep-format-draft')
            draft=app.diag()['draft_length'];session=app.diag()['session']
            app.open_settings();app.setting('transfer');ready('home');shot('home')
            assert len(app.diag()['transfer_actions'])==6
            click('查看格式规范');ready('format_index');shot('index')
            for topic,label in [('chats','聊天包'),('presets','会话预设'),('markdown','普通 Markdown')]:
                click(label);ready('format_'+topic);shot(topic+'-top')
                assert app.diag()['transfer_data']['topic']==topic
                scroll(-120);assert app.diag()['scroll_y']>0;shot(topic+'-scroll')
                # Back returns to directory and re-entering restores the doc position.
                back();ready('format_index')
                click(label);ready('format_'+topic);assert app.diag()['scroll_y']>0
                if topic=='chats':
                    # Exercise a real horizontal table, not just its presence.
                    available=width-(0 if height>width else 86)-20
                    row=None
                    for _ in range(30):
                        row=next((r for r in app.diag()['table_rows'] if r['width']>available+20 and 50<r['y']<height-35),None)
                        if row:break
                        scroll(-100)
                    assert row,app.diag()['table_rows']
                    y=row['y']+12
                    left=50 if height>width else 140
                    for i in range(9):
                        app.send('touch',x=width-50-(width-50-left)*i//8,y=y,pressed=True);time.sleep(.025)
                    app.send('touch',x=left,y=y,pressed=False);time.sleep(.15)
                    assert app.diag()['transfer_screen']=='format_chats'
                    assert any(r['id']==row['id'] and r['scroll']>0 for r in app.diag()['table_rows'])
                    shot('table-horizontal')
                click('导出格式规范')
                if delayed and topic=='chats':
                    t.wait_for(lambda:app.diag()['transfer_busy']);shot('progress')
                    click('取消');ready('format_chats')
                    assert '取消' in app.diag()['transfer_error']
                    assert not list((data/'transfer-tmp').rglob('*.part'))
                    back();ready('format_index');click(label);ready('format_chats')
                    click('导出格式规范')
                ready('done');shot('done')
                assert app.diag()['transfer_data']['format_spec']
                path=Path(app.diag()['transfer_data']['path']);assert path.exists()
                click('返回格式规范');ready('format_'+topic)
                back();ready('format_index')
            back();ready('home')
            click('导出格式规范');ready('done');click('返回导入与导出');ready('home')
            assert app.diag()['draft_length']==draft and app.diag()['session']==session
        finally:app.close()
        if width==936:
            assert not (data/'transfer-files/导出').exists()
            assert (data/'transfer-files/导入/聊天').exists()
    print(f'Format UI {width}x{height}: six entries, three native readers, scroll restoration, ZIP export and retained conversation passed.')


if __name__=='__main__':
    service()
    for size in [(960,266),(936,280),(568,1210)]:ui(*size)
