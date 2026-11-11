#!/usr/bin/env python3
"""Real LVGL three-layout integration, without device access or model calls."""
import pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture

t.port=1
OUT=t.ROOT/'.work/layout-integration';OUT.mkdir(parents=True,exist_ok=True)
for w,h in [(568,1210),(960,266),(936,280),(640,260)]:
    with tempfile.TemporaryDirectory(prefix='penchat-three-layouts-') as directory:
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',pathlib.Path(directory),seed_model=False,
                   official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            capture(a,OUT/f'{w}-chat.png')
            a.open_settings();capture(a,OUT/f'{w}-settings.png');a.to_chat()
            a.tap(w//2,h-34);t.wait_for(lambda:a.diag()['editor']['active'])
            t.wait_for(lambda:bool(a.diag()['editor']['keys']))
            def e():return a.diag()['editor']
            def point(x,y):
                c=e()['coordinate_space'];return round(x*w/c['width']),round(y*h/c['height'])
            def action(name):
                if name=='official':return a.editor_action(name)
                b=e()['actions'][name];a.tap(*point((b[0]+b[2])//2,(b[1]+b[3])//2))
            def key(name):
                k=next(k for k in e()['keys'] if k['key']==name);a.tap(*point(k['x'],k['y']))
            space=e()['coordinate_space'];assert not space['scaled'];assert e()['compact_full']==(w==640)
            capture(a,OUT/f'{w}-keyboard.png')
            k=next(k for k in e()['keys'] if k['key']=='language');x,y=point(k['x'],k['y'])
            a.send('touch',x=x,y=y,pressed=True);time.sleep(.54)
            assert e()['layout_menu'],'500ms menu must open while still held'
            a.send('touch',x=x,y=y,pressed=False);time.sleep(.05)
            capture(a,OUT/f'{w}-layout-popup.png')
            action('layout:1');assert e()['layout']=='en26'
            key('a');key('l');assert e()['length']==2
            # Physical contact coordinates must be inverted exactly once in compact mode.
            qa=next(k for k in e()['keys'] if k['key']=='a');ql=next(k for k in e()['keys'] if k['key']=='l')
            xa,ya=point(qa['x'],qa['y']);xl,yl=point(ql['x'],ql['y'])
            a.send('touch',x=xa,y=ya,pressed=True,points=[{'id':1,'x':xa,'y':ya}]);time.sleep(.02)
            a.send('touch',x=xl,y=yl,pressed=True,points=[{'id':1,'x':xa,'y':ya},{'id':2,'x':xl,'y':yl}]);time.sleep(.02)
            a.send('touch',x=xl,y=yl,pressed=True,points=[{'id':2,'x':xl,'y':yl}]);time.sleep(.02)
            a.send('touch',x=xl,y=yl,pressed=False,points=[])
            t.wait_for(lambda:e()['length']==4)
            action('official');r=a.receive('keyboard.open');assert r['text']=='alal'
            a.send('keyboard.result',requestId=r['requestId'],confirmed=False)
            t.wait_for(lambda:not a.diag()['keyboard']);assert e()['length']==4
            action('official');r=a.receive('keyboard.open')
            text='多行输入 English words\n第二行内容，用于检查文字与工具栏。'*4
            a.send('keyboard.result',requestId=r['requestId'],confirmed=True,text=text)
            t.wait_for(lambda:not e()['active']);assert a.diag()['draft_length']==len(text)
            capture(a,OUT/f'{w}-long-composer.png')
            assert not a.read_messages(),'Editing must not create messages'
        finally:a.close()
print('THREE_LAYOUTS_GEOMETRY_POPUP_500MS_ROLLOVER_OFFICIAL_AND_DRAFT_OK')
