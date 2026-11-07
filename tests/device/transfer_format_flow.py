"""Real mapped touches and native files; parent owns private/public isolation."""
import importlib.util
import json
import shlex
import subprocess
import sys
import time
import types
import zipfile


def run(env):
    d=types.SimpleNamespace(**env);W,H=d.W,d.H
    sys.path.insert(0,str(d.ROOT/"tests"))
    from settings_ui import activate
    def click(name):
        for _ in range(40):
            s=d.state();b=s['transfer_actions'][name]
            if s['page']['scrolling']:time.sleep(.15);continue
            if b[1]>=0 and b[3]<H:
                assert b[3]-b[1]+1>=44
                d.tap((b[0]+b[2])//2,(b[1]+b[3])//2);return
            d.settings_swipe(-110 if b[3]>=H else 110)
        raise AssertionError(('unreachable',name))
    def ready(screen):d.wait(lambda:not d.state()['transfer_busy'] and d.state()['transfer_screen']==screen)
    def back():
        if H>W:d.side_back()
        else:d.tap(35,28)
    session=d.state()['session'];draft=d.state()['draft_length'];native=d.pid()
    d.tap(35,28);d.wait(lambda:d.state()['navigation_open'])
    d.tap(W*2//3-70,H-30);d.wait(lambda:d.state()['view']=='settings')
    activate(d.state,d.tap,d.settings_swipe,'transfer');ready('home');d.capture('format-home')
    assert len(d.state()['transfer_actions'])==6
    click('查看格式规范');ready('format_index');d.capture('format-index')
    for topic,name in [('chats','聊天包'),('presets','会话预设'),('markdown','普通 Markdown')]:
        click(name);ready('format_'+topic);d.capture('format-'+topic)
        d.settings_swipe(-100);d.wait(lambda:not d.state()['page']['scrolling'])
        previous=d.state()['scroll_y'];assert previous>0
        back();ready('format_index');click(name);ready('format_'+topic)
        assert d.state()['scroll_y']==previous
        if topic=='chats':
            available=W-(0 if H>W else 86)-20
            for _ in range(35):
                row=next((r for r in d.state()['table_rows'] if r['width']>available+20 and 50<r['y']<H-35),None)
                if row:break
                d.settings_swipe(-100)
            assert row
            d.drag(W-50,row['y']+12,50 if H>W else 140,row['y']+12)
            d.wait(lambda:any(r['id']==row['id'] and r['scroll']>0 for r in d.state()['table_rows']))
            d.capture('format-horizontal-table')
        back();ready('format_index')
    click('导出说明与示例 ZIP');ready('done');d.capture('format-done')
    result=d.state()['transfer_data'];assert result['format_spec'] and result['count']==8
    archive=d.OUT/'format-export.zip'
    subprocess.run(['adb','-s',d.SERIAL,'pull',result['path'],str(archive)],check=True,capture_output=True)
    spec=importlib.util.spec_from_file_location('format_generator',d.ROOT/'scripts/compile-transfer-format.py')
    generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
    expected=generator.resources()
    with zipfile.ZipFile(archive) as z:
        assert set(z.namelist())==set(expected) and z.testzip() is None
        for name,content in expected.items():assert z.read(name)==content,name
    click('返回格式规范');ready('format_index');back();ready('home')
    files=[('示例/示例聊天.penchat.zip','聊天','example.penchat.zip'),
           ('示例/示例预设.penchat-presets.json','预设','example.penchat-presets.json'),
           ('示例/示例参考.md','聊天','example.md'),('示例/图片/示例.png','聊天','图片/示例.png')]
    for index,(name,kind,target) in enumerate(files):
        local=d.OUT/('format-fixture-'+str(index));local.write_bytes(expected[name])
        remote='/userdisk/Favorite/PenChat/导入/'+kind+'/'+target
        d.shell('mkdir -p '+shlex.quote(remote.rsplit('/',1)[0]))
        subprocess.run(['adb','-s',d.SERIAL,'push',str(local),remote],check=True,capture_output=True)
    for kind,name in [('聊天','example.penchat.zip'),('预设','example.penchat-presets.json'),('聊天','example.md')]:
        click(kind+'导入');ready('import_chats' if kind=='聊天' else 'import_presets')
        click(name);ready('preview');d.capture('format-preview-'+('presets' if kind=='预设' else 'markdown' if name.endswith('.md') else 'chats'))
        assert '查看默认预设差异' not in d.state()['transfer_actions']
        action=next(n for n in d.state()['transfer_actions'] if n.startswith('导入所选'))
        click(action);ready('done');assert d.state()['transfer_data']['added']==1
        click('返回导入与导出');ready('home')
    assert d.state()['session']==session and d.state()['draft_length']==draft
    assert d.pid()==native
    d.result['transfer_format']={'readers':3,'native_zip_exact_resources':True,'imported_chat_preset_markdown':True,'horizontal_table':True,'reading_position_restored':True}
