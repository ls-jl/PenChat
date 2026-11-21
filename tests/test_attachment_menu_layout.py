#!/usr/bin/env python3
"""Plus menus use real capsule edges, natural content and available screen space."""
import pathlib,tempfile,time,sqlite3,json
import test_lvgl_app as t
from provider_ui import capture
OUT=t.ROOT/'.work/attachment-menu-layout';OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def tap_bounds(a,b):a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def check(a,w,h,name):
    before=a.diag();active=before['editor']['active'];preedit=before['editor']['preedit_length'];draft=before['draft_length']
    tap_bounds(a,before['attachment_button'])
    t.wait_for(lambda:a.diag()['attachments'])
    time.sleep(.18) # Measure the resting viewport after its 150ms entry.
    d=a.diag();menu=d['attachment_viewport'];composer=d['composer_bounds'];count=len(d['attachment_actions'])
    assert menu[0]==composer[0],(menu,composer)
    assert 0<=menu[0]<menu[2]<w and 0<=menu[1]<menu[3]<h,menu
    if active and d['editor']['docked']:
        editor=d['editor'];height=editor['coordinate_space']['height']
        panel=(editor['panel_top']*h+height-1)//height
        top=max(d['safe_insets'][1],8)
        assert menu[3]<panel and d['attachment_above_keyboard'],(menu,panel)
        assert menu[3]-menu[1]+1==min(count*44+8,max(1,panel-top-8))
    else:
        assert menu[3]>=composer[3]+10,(menu,composer)
        assert menu[3]-menu[1]+1==min(count*44+8,h-8),d['attachment_actions']
    assert d['editor']['active']==active and d['editor']['preedit_length']==preedit and d['draft_length']==draft
    capture(a,OUT,f'{w}-{name}')
    # Reveal the final option by scrolling only this menu; never scroll the chat.
    before_scroll=d['scroll_y']
    for _ in range(20):
        d=a.diag();b=d['attachment_actions']['session_info'];v=d['attachment_viewport']
        if b[1]>=v[1] and b[3]<=v[3]:break
        x=(v[0]+v[2])//2;start=v[3]-20;end=max(v[1]+16,start-120)
        for i in range(7):a.send('touch',x=x,y=start+(end-start)*i//6,pressed=True);time.sleep(.025)
        a.send('touch',x=x,y=end,pressed=False);time.sleep(.12)
    else:raise AssertionError('Last attachment entry is unreachable')
    assert a.diag()['scroll_y']==before_scroll
    a.tap(w-2,2);t.wait_for(lambda:not a.diag()['attachments'])
    assert a.diag()['editor']['active']==active and a.diag()['draft_length']==draft

for w,h in [(960,266),(640,260),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-plus-layout-') as folder:
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            check(a,w,h,'collapsed')
            b=a.diag()['composer_text_bounds'];tap_bounds(a,b);t.wait_for(lambda:a.diag()['editor']['active'])
            if h>w:
                # Composition must remain parked in the same live keyboard.
                for char in ['n','i']:
                    k=next(k for k in a.diag()['editor']['keys'] if k['key']==char);a.tap(k['x'],k['y'])
                check(a,w,h,'docked-preedit')
            else:a.editor_action('done');t.wait_for(lambda:not a.diag()['editor']['active'])
            if not a.diag()['editor']['active']:
                tap_bounds(a,a.diag()['composer_text_bounds']);t.wait_for(lambda:a.diag()['editor']['active'])
            a.editor_action('official');r=a.receive('keyboard.open')
            a.send('keyboard.result',requestId=r['requestId'],confirmed=True,text=('多行草稿内容用于检查输入框高度与菜单对齐。\n'*8))
            t.wait_for(lambda:not a.diag()['editor']['active'])
            check(a,w,h,'long-draft')
        finally:a.close()
print('THREE_LAYOUT_PLUS_ANCHORS_NATURAL_SIZE_LOCAL_SCROLL_AND_DOCKED_PREEDIT_OK')
