#!/usr/bin/env python3
"""Portable files, hostile archives, rollback and actual LVGL entrypoints."""
import json,os,pathlib,shutil,signal,sqlite3,stat,subprocess,tempfile,time,warnings,zipfile
from test_lvgl_app import Native,ROOT,wait_for
from test_chat_style import seed
import test_lvgl_app as transport
transport.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
driver=pathlib.Path(os.environ.get('PENCHAT_TRANSFER_TEST_BINARY',ROOT/'build/host/penchat-transfer-tests'))
fixture=json.loads(subprocess.check_output([str(driver)],text=True).splitlines()[-1])
package=pathlib.Path(fixture['package'])
with zipfile.ZipFile(package) as z:
    payload={n:z.read(n) for n in z.namelist()}
    joined=b'\n'.join(payload.values())
    assert b'KEY_DO_NOT_EXPORT' not in joined and b'OPAQUE_DO_NOT_EXPORT' not in joined
    assert b'user:pass' not in joined and b'?api_key' not in joined
    manifest=json.loads(payload['manifest.json'])
    assert manifest['version']==1 and manifest['app_version']==json.loads((ROOT/'package.json').read_text())['version']
    assert any(n.endswith('.md') for n in payload) and any(n.startswith('assets/') for n in payload)
with tempfile.TemporaryDirectory(prefix='penchat-transfer-safety-') as directory:
    base=pathlib.Path(directory);data=base/'data';folder=base/'Favorite/PenChat'
    p=subprocess.Popen([str(driver),str(data),str(folder)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    def call(op,args={},error=False):
        p.stdin.write(json.dumps({'op':op,'args':args})+'\n');p.stdin.flush()
        v=json.loads(p.stdout.readline());assert (v['type']=='transfer_error')==error,v
        return v['data'].get('data',v['data'])
    try:
        call('init');inbox=folder/'导入/聊天'
        presets=folder/'导入/预设'
        (presets/'version.penchat-presets.json').write_text(json.dumps({'format':'penchat','version':99,'kind':'presets','items':[]}))
        call('preview',{'kind':'presets','name':'version.penchat-presets.json'},True)
        nested={}
        for _ in range(100):nested={'nested':nested}
        (presets/'deep.penchat-presets.json').write_text(json.dumps(nested))
        call('preview',{'kind':'presets','name':'deep.penchat-presets.json'},True)
        # Isolate a real statvfs failure result in this test child only.
        if 'ASAN_OPTIONS' not in os.environ:
            source=base/'space.c';library=base/'space.so'
            source.write_text('#include <sys/statvfs.h>\n#include <string.h>\nint statvfs(const char *p,struct statvfs *s){(void)p;memset(s,0,sizeof(*s));s->f_frsize=4096;return 0;}\n')
            subprocess.run(['cc','-shared','-fPIC',str(source),'-o',str(library)],check=True)
            space_data=base/'full-data';space_root=base/'full-root'
            child=subprocess.Popen([str(driver),str(space_data),str(space_root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,LD_PRELOAD=str(library)))
            child.stdin.write(json.dumps({'op':'init'})+'\n');child.stdin.flush();assert json.loads(child.stdout.readline())['type']=='transfer_result'
            (space_root/'导入/聊天/file.md').write_text('storage test')
            child.stdin.write(json.dumps({'op':'preview','args':{'kind':'chats','name':'file.md'}})+'\n');child.stdin.flush()
            failure=json.loads(child.stdout.readline());assert failure['type']=='transfer_error' and '空间不足' in failure['data']['error']
            child.stdin.close();assert child.wait(timeout=10)==0

        def count():
            with sqlite3.connect(data/'penchat.db') as db:return db.execute('select count(*) from chat_sessions').fetchone()[0]
        def archive(name,entries):
            with zipfile.ZipFile(inbox/name,'w',zipfile.ZIP_DEFLATED) as z:
                for k,v in entries.items():z.writestr(k,v)
        cases={
            'traversal.penchat.zip':{'../outside.txt':b'no'},
            'absolute.penchat.zip':{'/tmp/outside.txt':b'no'},
            'undeclared.penchat.zip':dict(payload,**{'extra.txt':b'no'}),
            'corrupt.penchat.zip':dict(payload,**{manifest['items'][0]['file']:b'{}'}),
        }
        for name,entries in cases.items():archive(name,entries);call('preview',{'kind':'chats','name':name},True);assert count()==0
        info=zipfile.ZipInfo('assets/link');info.create_system=3;info.external_attr=(stat.S_IFLNK|0o777)<<16
        with zipfile.ZipFile(inbox/'symlink.penchat.zip','w') as z:z.writestr(info,'/etc/passwd')
        call('preview',{'kind':'chats','name':'symlink.penchat.zip'},True)
        with warnings.catch_warnings():
            warnings.simplefilter('ignore')
            with zipfile.ZipFile(inbox/'duplicate.penchat.zip','w') as z:z.writestr('manifest.json','{}');z.writestr('manifest.json','{}')
        call('preview',{'kind':'chats','name':'duplicate.penchat.zip'},True)
        # Source symlinks are excluded from the file browser.
        (inbox/'link.md').symlink_to('/etc/passwd')
        assert not any(x['name']=='link.md' for x in call('list',{'kind':'chats'})['items'])
        # Missing attachments require a separate explicit opt-in.
        asset=next(n for n in payload if n.startswith('assets/'))
        archive('missing.penchat.zip',{k:v for k,v in payload.items() if k!=asset})
        preview=call('preview',{'kind':'chats','name':'missing.penchat.zip'});assert preview['missing']
        call('import',{'indices':[0]},True);assert count()==0
        assert call('import',{'indices':[0],'allow_missing':True})['added']==1
        # Absolute/parent-relative Markdown images become readable placeholders.
        (inbox/'unsafe.md').write_text('# Reference\n![x](/etc/passwd)\n![y](../private.png)\n![remote](https://example.test/photo.png)')
        call('preview',{'kind':'chats','name':'unsafe.md'});call('import',{'indices':[0],'allow_missing':True})
        with sqlite3.connect(data/'penchat.db') as db:
            text=db.execute("select output_text from chat_messages where metadata_json like '%imported_reference%'").fetchone()[0]
            assert '![' not in text and '/etc/passwd' not in text and '[remote](https://' in text
        # A failed business transaction leaves neither records nor copied assets.
        archive('valid.penchat.zip',payload)
        call('preview',{'kind':'chats','name':'valid.penchat.zip'})
        before=set((data/'assets').glob('*'));n=count()
        with sqlite3.connect(data/'penchat.db') as db:db.execute("CREATE TRIGGER fail_import BEFORE INSERT ON chat_messages BEGIN SELECT RAISE(ABORT,'fixture failure'); END")
        call('import',{'indices':[0],'copy':True},True)
        assert count()==n and set((data/'assets').glob('*'))==before
        with sqlite3.connect(data/'penchat.db') as db:db.execute('DROP TRIGGER fail_import')
        assert call('import',{'indices':[0],'copy':True})['added']==1
        # Kill after asset preparation while the actual SQLite writer is locked.
        call('preview',{'kind':'chats','name':'valid.penchat.zip'})
        before=set((data/'assets').glob('*'));n=count()
        blocker=sqlite3.connect(data/'penchat.db',isolation_level=None)
        blocker.execute('BEGIN IMMEDIATE')
        p.stdin.write(json.dumps({'op':'import','args':{'indices':[0],'copy':True}})+'\n');p.stdin.flush()
        deadline=time.monotonic()+2
        while time.monotonic()<deadline and set((data/'assets').glob('*'))==before:time.sleep(.005)
        assert set((data/'assets').glob('*'))!=before, 'Import never reached asset preparation'
        p.kill();p.wait(timeout=5);blocker.execute('ROLLBACK');blocker.close()
        p=subprocess.Popen([str(driver),str(data),str(folder)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        call('init');assert count()==n
        deadline=time.monotonic()+5
        while time.monotonic()<deadline and set((data/'assets').glob('*'))!=before:time.sleep(.01)
        assert set((data/'assets').glob('*'))==before
        assert not (base/'outside.txt').exists()
    finally:p.stdin.close();assert p.wait(timeout=10)==0
# The actual editor, navigation and imported message rendering stay intact.
with tempfile.TemporaryDirectory(prefix='penchat-transfer-ui-') as directory:
    data=pathlib.Path(directory);seed(data,'Existing **Markdown** and $x^2$')
    with sqlite3.connect(data/'penchat.db') as db:
        db.execute("insert into providers(id,name,type,base_url,api_key,created_at,updated_at) values('local','本地服务','openai-compatible','https://unmatched.test/v1','fake-local-key',1,1)")
        db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,is_default,created_at,updated_at) values('local-a','local','local-model','本地模型','chat',1,1,1)")
    app=Native(binary,data)
    gallery=ROOT/'.work/transfer-ui';gallery.mkdir(parents=True,exist_ok=True)
    def shot(name):
        time.sleep(.25);app.process.send_signal(signal.SIGUSR1);time.sleep(.25)
        shutil.copy2(data/'lvgl-diagnostic.png',gallery/(name+'.png'))
    def ready(screen):wait_for(lambda:not app.diag()['transfer_busy'] and app.diag()['transfer_screen']==screen)
    def home():
        while app.diag()['transfer_screen']!='home':app.tap(40,32);time.sleep(.05)
    try:
        app.edit('保留的草稿');app.open_settings();app.setting("transfer")
        assert app.diag()['view']=='transfer';shot('home')
        app.tap(300,108);ready('export_chats');wait_for(lambda:bool(app.diag()['transfer_data'].get('items')));shot('chat-selection')
        app.tap(300,184);app.tap(300,72);ready('done');shot('export-done')
        output=pathlib.Path(app.diag()['transfer_data']['path']);inbox=data/'transfer-files/导入/聊天';shutil.copy2(output,inbox/'chat.penchat.zip')
        home();app.tap(740,108);ready('import_chats');wait_for(lambda:len(app.diag()['transfer_data'].get('items',[]))==1);shot('file-list')
        app.tap(300,128);ready('preview');shot('chat-preview')
        app.tap(300,140);ready('done');assert app.diag()['transfer_data']['added']==1
        home();app.tap(300,168);ready('export_presets');wait_for(lambda:len(app.diag()['transfer_data'].get('items',[]))==1);shot('preset-selection')
        app.tap(740,72);ready('done');presetfile=pathlib.Path(app.diag()['transfer_data']['path']);shutil.copy2(presetfile,data/'transfer-files/导入/预设/default.penchat-presets.json')
        home();app.tap(740,168);ready('import_presets');wait_for(lambda:len(app.diag()['transfer_data'].get('items',[]))==1)
        app.tap(300,128);ready('preview');shot('preset-preview')
        app.tap(300,140);wait_for(lambda:bool(app.diag()['modal']));shot('default-confirmation-error');app.tap(890,222);wait_for(lambda:not app.diag()['modal'])
        home();shutil.copy2(package,data/'transfer-files/导入/聊天/models.penchat.zip')
        app.tap(740,108);ready('import_chats');wait_for(lambda:len(app.diag()['transfer_data'].get('items',[]))==2)
        app.tap(300,184);ready('preview');shot('model-pending')
        app.tap(300,200);ready('mapping');shot('model-mapping')
        app.tap(300,134);ready('preview');shot('model-mapped')
        home();app.tap(40,32);app.to_chat();assert app.diag()['draft_length']==len('保留的草稿')
        app.open_navigation();row=app.diag()['history_rows'][0];app.long_press(row['x'],row['y']);shot('history-export')
        app.history_action("export");ready('export_chats');assert app.diag()['view']=='transfer'
    finally:app.close()
shutil.rmtree(fixture['root'])
print('Transfer: chat/preset roundtrip, credentials exclusion, Markdown, hostile archives, rollback, directory and LVGL flows passed.')

# Persistence is opt-out, survives restart, and never cleans on background/IME.
with tempfile.TemporaryDirectory(prefix='penchat-transfer-policy-') as directory:
    data=pathlib.Path(directory)
    app=Native(binary,data)
    root=data/'transfer-files'
    wait_for(lambda:(root/'导出/聊天').is_dir())
    exported=root/'导出/聊天/keep.txt';incoming=root/'导入/聊天/keep.md'
    exported.write_text('exported');incoming.write_text('incoming')
    app.send('visibility',visible=False);time.sleep(.1)
    assert exported.exists()
    app.send('visibility',visible=True);app.close()
    assert exported.exists() and incoming.exists()
    app=Native(binary,data)
    try:
        app.open_settings()
        app.setting('persistent_exports')
        with sqlite3.connect(data/'penchat.db') as db:
            assert json.loads(db.execute("select value_json from app_settings where key='persistent_export_directory'").fetchone()[0]) is False
        shot_dir=ROOT/'.work/transfer-ui';time.sleep(.25);app.process.send_signal(signal.SIGUSR1);time.sleep(.25)
        shutil.copy2(data/'lvgl-diagnostic.png',shot_dir/'persistence-off.png')
        app.send('visibility',visible=False);time.sleep(.1)
        assert exported.exists() and incoming.exists()
        app.send('visibility',visible=True)
    finally:app.close()
    assert not (root/'导出').exists() and incoming.exists()
    app=Native(binary,data)
    try:
        wait_for(lambda:(root/'导出/聊天').is_dir())
        with sqlite3.connect(data/'penchat.db') as db:
            assert json.loads(db.execute("select value_json from app_settings where key='persistent_export_directory'").fetchone()[0]) is False
        (root/'导出/聊天/again.txt').write_text('again')
    finally:app.close()
    assert not (root/'导出').exists() and incoming.exists()
print('Export persistence: default on, UI toggle, background retention, clean exit, recreation and import-file preservation passed.')
