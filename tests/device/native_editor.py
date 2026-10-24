#!/usr/bin/env python3
"""ORANGE editor acceptance with isolated private/public directories and fake input."""
import json,time,subprocess,shlex,os
from chat_acceptance import close_app,push
import acceptance as d

def state():return d.state()['editor']
def key(value):
    item=next(k for k in state()['keys'] if k['key']==value);d.tap(item['x'],item['y'])
def hold(x,y):
    d.adb('/tmp/penchat-ui-tap --hold '+str(y+113)+' '+str(959-x)+' 550 2>/dev/null');time.sleep(.1)
def double(x,y):
    command='/tmp/penchat-ui-tap '+str(y+113)+' '+str(959-x)+' 2>/dev/null'
    d.adb(command+' && '+command);time.sleep(.08)
def drag_caret(x,y,ex,ey,screenshot=None):
    command='/tmp/penchat-ui-tap --drag-hold '+str(y+113)+' '+str(959-x)+' '+str(ey+113)+' '+str(959-ex)+' 550 1500'
    process=subprocess.Popen(['adb','-s',d.SERIAL,'shell',command],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        d.wait(lambda:state()['magnifier'] and abs(d.state()['touch']['x']-ex)<4)
        assert state()['cursor_drag']
        if screenshot:d.capture(screenshot)
    finally:assert process.wait(timeout=5)==0
    d.wait(lambda:not state()['magnifier'])
def select_all():
    if state()['preedit_length']:key('enter')
    if not state()['length']:return
    double(622,93);d.wait(lambda:state()['selected']);hold(622,93)
    d.wait(lambda:state()['menu']);d.tap(632,154);d.wait(lambda:state()['selection_end']==state()['length'])
def clear():
    select_all()
    if state()['length']:key('delete')
    assert state()['length']==0 and not state()['preedit_length']
def exercise():
    s=d.state();assert not s['editor']['active'] and not s['keyboard'] and not s['generating'] and not s.get('compressing',False) and not s['draft_length'],'Finish existing input before testing'
    host=d.adb('pidof miniapp');results={'version':d.APP['version'],'host_pid':host}
    try:
        d.open_settings();d.setting("models");d.tap(790,30);d.tap(250,60)
        d.wait(lambda:state()['active'] and state()['top_panel_blocked']);d.wait(lambda:state()['ocr_active'])
        # Swipe from the physical top edge; it must remain in this native editor.
        d.adb('/tmp/penchat-ui-tap 114 659 313 659 25 2>/dev/null')
        assert state()['active'] and state()['top_panel_blocked'] and d.state()['visible']
        results['top_panel_policy']=True
        clear()
        if state()['chinese']:key('language')
        key('shift');key('A');key('b');key('c');assert state()['length']==3;d.capture('editor-english')
        drag_caret(624,93,300,96,'editor-caret-magnifier')
        assert state()['cursor']==0 and state()['length']==3 and state()['text_scroll']==0
        drag_caret(624,93,930,260)
        assert state()['length']==3 and state()['text_scroll']==0
        results['caret_cross_keyboard_and_empty_bottom']=True
        key('mode');key('1');d.capture('editor-numbers');key('symbols');key('[');d.capture('editor-symbols')
        key('mode');key('mode');assert state()['mode']==1;key('mode');clear()
        for c in 'alpha':key(c)
        select_all();d.capture('editor-selection');hold(622,93);d.wait(lambda:state()['menu']);d.tap(722,154)
        assert state()['clipboard_count']==1
        key('x');assert state()['length']==1
        hold(938,185);d.wait(lambda:state()['undo_menu']);d.capture('editor-undo');x1,y1,x2,y2=state()['actions']['undo'];d.tap((x1+x2)//2,(y1+y2)//2);assert state()['selected']
        d.tap(930,236);d.wait(lambda:state()['clipboard_open']);d.capture('editor-clipboard');d.tap(700,98);assert state()['length']==5
        clear();key('language')
        for c in 'nihao':key(c)
        d.wait(lambda:state()['candidate_count']>0);results['query_ms']=state()['query_ms'];d.capture('editor-pinyin')
        d.tap(930,28);d.capture('editor-expanded');d.tap(930,28)
        key('space');d.wait(lambda:state()['length']>0)
        while state()['preedit_length']:
            d.wait(lambda:state()['candidate_count']>0);key('space')
        original=state();d.official_editor();d.wait(lambda:d.state()['keyboard'])
        d.adb('miniapp_cli capture /tmp/penchat-official-editor.png')
        subprocess.run(['adb','-s',d.SERIAL,'pull','/tmp/penchat-official-editor.png',str(d.OUT/'editor-official.png')],check=True,capture_output=True)
        d.tap(35,35);d.wait(lambda:not d.state()['keyboard']);d.wait(lambda:state()['top_panel_blocked'] and state()['ocr_active'])
        assert state()['length']==original['length'] and state()['cursor']==original['cursor']
        native=d.pid();session=state()['session'];d.adb('miniapp_cli start 8080222437664451');d.wait(lambda:not d.state()['visible'])
        assert not state()['ocr_active']
        d.adb('miniapp_cli start '+d.APP_ID);d.wait(lambda:d.state()['visible'] and state()['top_panel_blocked'] and state()['ocr_active'])
        assert d.pid()==native and state()['session']==session
        x1,y1,x2,y2=state()['actions']['ocr-mode'];d.tap((x1+x2)//2,(y1+y2)//2)
        d.wait(lambda:state()['ocr_multi']);d.capture('editor-multi-ocr')
        d.tap(630,236);d.wait(lambda:not state()['active']);assert not state()['ocr_active']
        results.update(clipboard=True,selection=True,undo=True,mode_reset=True,official_cancel=True,background_resume=True,ocr_registration_and_release=True)
        assert d.adb('pidof miniapp')==host
    finally:
        if d.pid():
            s=d.state()
            if s['keyboard']:d.tap(35,35);d.wait(lambda:not d.state()['keyboard'])
            if state()['active']:d.tap(630,236)
            if d.state()['view']=='provider':d.tap(40,32)
            d.to_chat()
    (d.OUT/'editor-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n');print(json.dumps(results,ensure_ascii=False))
def hashes(path):
    return sorted(d.adb('find '+shlex.quote(path)+' -type f -exec sha256sum {} \\;').splitlines())
def main():
    assert 'ORANGE' in d.adb('cat /proc/device-tree/model')
    if d.pid():
        current=d.state()
        assert current['view']=='chat' and not current['title_busy'] and not current['editor']['active'] and not current['keyboard'] and not current['generating'] and not current.get('compressing',False) and not current['draft_length'] and not current['storage']['unsaved'],'Finish existing input before testing'
        close_app()
    sdk=os.environ.get('RK_SDK_ROOT','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK')
    gcc=os.environ.get('JSAPI_TOOLCHAIN_ROOT',sdk+'/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu')+'/bin/aarch64-linux-gnu-g++'
    helper=d.ROOT/'.work/penchat-ui-tap'
    subprocess.run([gcc,'-std=c++11','-Os',str(d.ROOT/'tests/device/tap.cpp'),'-o',str(helper)],check=True)
    push(helper,'/tmp/penchat-ui-tap');d.adb('chmod 755 /tmp/penchat-ui-tap')
    saved=d.DATA+'.keyboard-acceptance-saved'
    public='/userdisk/Favorite/PenChat';public_saved=public+'.keyboard-acceptance-saved'
    assert d.adb('test ! -e '+saved+' && test ! -e '+public_saved+' && echo ready')=='ready','Recover an earlier interrupted acceptance first'
    original=hashes(d.DATA);has_public=d.adb('test -d '+public+' && echo yes')=='yes'
    original_public=hashes(public) if has_public else [];host=d.adb('pidof miniapp')
    isolated=False;isolated_public=False;cycles=[]
    try:
        d.adb('mv '+d.DATA+' '+saved+' && mkdir '+d.DATA);isolated=True
        if has_public:d.adb('mv '+public+' '+public_saved)
        isolated_public=True
        d.adb('miniapp_cli start '+d.APP_ID);d.wait(d.pid);d.wait(lambda:d.state()['visible']);time.sleep(.6)
        exercise()
        d.open_settings();d.setting("models");d.tap(790,30)
        for index in range(20):
            d.tap(250,60);d.wait(lambda:state()['top_panel_blocked'] and state()['ocr_active'] and d.state()['scan_released'])
            native=d.pid();cycles.append({'cycle':index+1,'fds':int(d.adb('ls /proc/'+native+'/fd | wc -l')),'rss_kib':int(d.adb("awk '/VmRSS/{print $2}' /proc/"+native+'/status'))})
            d.tap(630,236);d.wait(lambda:not state()['active'] and not d.state()['ocr'])
        assert max(c['fds'] for c in cycles)-min(c['fds'] for c in cycles)<=2,cycles
    finally:
        if d.pid():close_app()
        if isolated:d.adb('rm -rf '+d.DATA+' && mv '+saved+' '+d.DATA)
        if isolated_public:
            d.adb('rm -rf '+public)
            if has_public:d.adb('mv '+public_saved+' '+public)
        assert hashes(d.DATA)==original,'Original private files changed'
        if has_public:assert hashes(public)==original_public,'Original public files changed'
        assert d.adb('pidof miniapp')==host,'System host changed'
    (d.OUT/'editor-cycles.json').write_text(json.dumps({'cycles':cycles,'private_files_restored':True,'public_files_restored':True,'host_unchanged':True},indent=2)+'\n')
    d.adb('miniapp_cli start '+d.APP_ID);d.wait(d.pid);d.wait(lambda:d.state()['visible']);time.sleep(.6)
if __name__=='__main__':main()
