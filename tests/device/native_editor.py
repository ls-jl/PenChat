#!/usr/bin/env python3
"""ORANGE native editor acceptance using an unsaved Provider form, never user data."""
import json, time, subprocess
import acceptance as d

def state(): return d.state()['editor']
def key(value):
    item=next(k for k in state()['keys'] if k['key']==value)
    d.tap(item['x'],item['y'])
def clear():
    d.tap(730,236);d.tap(815,168);key('delete')
    assert state()['length']==0 and state()['preedit_length']==0

def main():
    s=d.state()
    assert s['view']=='chat' and not s['editor']['active'] and not s['keyboard'] and not s['generating'], 'Finish existing input before testing'
    host=d.adb('pidof miniapp')
    results={'version':d.APP['version'],'host_pid':host,'query_ms':[]}
    try:
        d.open_settings();d.tap(180,80);d.tap(790,30);d.tap(250,60)
        assert state()['active'] and not state()['multiline']
        clear();d.tap(730,100)
        key('shift');key('A');key('b');key('c')
        assert state()['length']==3 and state()['shift']==0
        d.capture('editor-english')
        key('mode');key('1');d.capture('editor-numbers')
        key('mode');key('[');d.capture('editor-symbols')
        key('mode');clear();d.tap(730,100)
        for char in 'nihao':key(char)
        d.wait(lambda:state()['candidate_count']>0)
        results['query_ms'].append(state()['query_ms'])
        d.capture('editor-pinyin');d.tap(930,28);d.capture('editor-expanded')
        d.tap(687,150)
        d.wait(lambda:state()['length']==1 and state()['preedit_length']==3)
        d.capture('editor-partial')
        d.wait(lambda:state()['candidate_count']>0)
        d.tap(870,100);d.wait(lambda:state()['preedit_length']==0)
        assert state()['active'] and state()['length']==2
        d.tap(870,100);d.wait(lambda:not state()['active'])
        d.tap(250,60);assert state()['length']==2
        for char in 'ni':key(char)
        original=state();d.tap(860,236);d.wait(lambda:d.state()['keyboard'])
        assert 'allocated by = penchat-lvgl' not in d.adb('cat /sys/kernel/debug/dri/0/state')
        d.adb('miniapp_cli capture /tmp/penchat-official-editor.png')
        subprocess.run(['adb','-s',d.SERIAL,'pull','/tmp/penchat-official-editor.png',str(d.OUT/'editor-official.png')],check=True,capture_output=True)
        d.tap(35,35);d.wait(lambda:not d.state()['keyboard'])
        assert state()['active'] and state()['length']==2 and state()['preedit_length']==2 and state()['cursor']==original['cursor']
        d.capture('editor-official-restored')
        native=d.pid();session=state()['session']
        d.adb('miniapp_cli start 8080222437664451');d.wait(lambda:not d.state()['visible'])
        d.adb('miniapp_cli start '+d.APP_ID);d.wait(lambda:d.state()['visible'])
        assert d.pid()==native and state()['session']==session and state()['preedit_length']==2
        d.tap(860,236);d.wait(lambda:d.state()['keyboard']);d.tap(896,226)
        d.wait(lambda:not d.state()['keyboard']);assert not state()['active']
        d.tap(250,60);assert state()['length']==4
        d.tap(730,236);d.capture('editor-tools');d.tap(730,236)
        d.tap(630,100)
        results.update(partial_composition=True,done_two_steps=True,official_cancel=True,official_confirm=True,background_resume=True)
        assert d.adb('pidof miniapp')==host
    finally:
        if d.pid():
            s=d.state()
            if s['keyboard']:d.tap(35,35);d.wait(lambda:not d.state()['keyboard'])
            if state()['active']:d.tap(630,100)
            # Discard the unsaved Provider. Never press its Save action.
            if d.state()['view']=='provider':d.tap(40,32)
            d.to_chat()
    (d.OUT/'editor-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(results,ensure_ascii=False))
if __name__=='__main__':main()
