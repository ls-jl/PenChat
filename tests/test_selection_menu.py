#!/usr/bin/env python3
"""Actual LVGL menu placement follows visible highlights, not release coordinates."""
import pathlib
import json
import sqlite3
import tempfile
import time
import test_lvgl_app as ui
from test_text_selection import BINARY, fixture, ro, editor, press, bounds_tap, screenshot
ui.port=1


def check(bounds,menu,width,height,scaled=False,shapes=()):
    assert len(bounds)==4 and len(menu)==4,(bounds,menu)
    side=max(24,min(32,width//24)) if height>width else 8
    top=side if height>width else 4
    bottom=height-(side+4 if height>width else 10)
    w=menu[2]-menu[0]+1;h=menu[3]-menu[1]+1
    centered=max(side,min(width-side-w,bounds[0]+int(((bounds[2]-bounds[0]+1)-w)/2)))
    clearance=max([0]+[max(0,g[3]-bounds[3]) for g in shapes if g[0]<centered+w and g[2]+1>centered])
    if bounds[1]-8-h>=top:
        kind='above';y=bounds[1]-8-h
        x=max(side,min(width-side-w,bounds[0]+int(((bounds[2]-bounds[0]+1)-w)/2)))
    elif bounds[3]+1+clearance+8+h<=bottom:
        kind='below';y=bounds[3]+clearance+9
        x=max(side,min(width-side-w,bounds[0]+int(((bounds[2]-bounds[0]+1)-w)/2)))
    else:kind='lower-left';x=side;y=bottom-h
    tolerance=2 if scaled else 0
    assert abs(menu[0]-x)<=tolerance and abs(menu[1]-y)<=tolerance,(kind,bounds,menu,(x,y))
    assert menu[0]>=side-tolerance and menu[2]<width-side+tolerance and menu[1]>=top-tolerance and menu[3]<bottom+tolerance
    return kind


def run(width,height,scene,percent=100):
    with tempfile.TemporaryDirectory(prefix='penchat-selection-menu-') as tmp:
        folder=pathlib.Path(tmp);fixture(folder)
        if percent!=100:
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute('INSERT OR REPLACE INTO app_settings VALUES(?,?,1)',('reading_font_percent',json.dumps(percent)))
        if scene=='above':
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute("UPDATE chat_messages SET output_text=? WHERE id='u'",('Question\nword',))
        if scene!='above':
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute("DELETE FROM message_parts WHERE message_id='u'")
                db.execute("DELETE FROM chat_messages WHERE id='u'")
                if scene=='full':
                    text='\n\n'.join('Alpha paragraph '+str(i) for i in range(80))
                    db.execute("UPDATE chat_messages SET output_text=? WHERE id='a'",(text,))
        app=ui.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height,'direction':0})
        try:
            if scene=='full':
                press(app,width//2,40);press(app,width//2,115);press(app,width//2,115,False);time.sleep(.2)
            r=next(r for r in ro(app)['regions'] if r['key']=='a')
            visible=[p for p in r['parts'] if p[1]>=0 and p[3]>p[1] and p[3]<app.diag()['reader_height']-8]
            b=visible[len(visible)//2] if scene=='full' else visible[0]
            app.long_press(b[0]+14,b[1]+14,.52);assert ro(app)['menu'],ro(app)
            if scene=='full':
                m=ro(app)['menu_bounds'];app.tap(m[0]+45,m[1]+25)
            s=ro(app);kind=check(s['selection_bounds'],s['menu_bounds'],width,height,shapes=s['handle_shapes'])
            if scene!='full':assert kind==scene,(scene,kind,s)
            elif height<width and percent==100:assert kind=='lower-left',(kind,s)
            screenshot(app,str(width)+'-menu-'+scene+('' if percent==100 else '-'+str(percent)))
            # Hit coordinates come from the newly positioned widget.
            m=s['menu_bounds'];app.tap(m[0]+140,m[1]+25)
            assert ro(app)['clipboard_bytes']>0 and ro(app)['selected']
            print('Selection menu:',width,height,scene,kind,'passed',flush=True)
        finally:app.close()


def empty_editor(width,height):
    with tempfile.TemporaryDirectory(prefix='penchat-editor-menu-') as tmp:
        app=ui.Native(BINARY,pathlib.Path(tmp),official_adapter=False,screen={'width':width,'height':height,'direction':0})
        try:
            bounds_tap(app,app.diag()['composer_bounds']);ui.wait_for(lambda:editor(app)['active'])
            e=editor(app);space=e['coordinate_space'];b=e['text_bounds'];before=e['build_count']
            def native(x,y):
                if not space['scaled']:return x,y
                return x*round(256*space['native_width']/space['width'])/256,y*round(256*space['native_height']/space['height'])/256
            x,y=native(b[0]+20,b[1]+17)
            app.long_press(round(x),round(y),.52);e=editor(app)
            assert e['menu'] and all(k in e['actions'] for k in ['all','copy','paste','cut','undo']),e
            assert e['build_count']==before,(before,e['build_count'])
            s=e['selection_bounds'];m=e['selection_popup']
            if space['scaled']:
                x1,y1=native(s[0],s[1]);x2,y2=native(s[2]+1,s[3]+1);s=[round(x1),round(y1),round(x2)-1,round(y2)-1]
                x1,y1=native(m[0],m[1]);x2,y2=native(m[2]+1,m[3]+1);m=[round(x1),round(y1),round(x2)-1,round(y2)-1]
            check(s,m,width,height,space['scaled'])
            screenshot(app,str(width)+'-menu-empty-editor')
            print('Empty editor menu and unchanged keyboard:',width,height,'passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    for size in [(960,266),(640,360),(568,1210)]:
        for scene in ['above','below','full']:run(*size,scene)
        empty_editor(*size)
    run(960,266,'full',75)
    run(960,266,'full',150)
    run(568,1210,'full',150)
