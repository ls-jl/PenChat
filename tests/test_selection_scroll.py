#!/usr/bin/env python3
"""Selection survives real reader scrolling, margins, cancellation and editor scroll."""
import pathlib
import sqlite3
import tempfile
import time
import test_lvgl_app as ui
from test_text_selection import BINARY, fixture, ro, press, screenshot


def swipe(app,x,y,dy):
    press(app,x,y)
    for step in range(1,7):
        press(app,x,y+dy*step//6);time.sleep(.025)
    press(app,x,y+dy,False);time.sleep(.15)


def run(width,height):
    with tempfile.TemporaryDirectory(prefix='penchat-selection-scroll-') as tmp:
        folder=pathlib.Path(tmp);fixture(folder)
        with sqlite3.connect(folder/'penchat.db') as db:
            text='\n\n'.join('Alpha bravo paragraph '+str(n) for n in range(60))
            db.execute("UPDATE chat_messages SET output_text=? WHERE id='a'",(text,))
            db.execute("UPDATE message_parts SET text=? WHERE message_id='a'",(text,))
        app=ui.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height,'direction':0})
        try:
            region=next(r for r in ro(app)['regions'] if r['key']=='a')
            parts=[p for p in region['parts'] if p[1]>25 and p[3]<app.diag()['reader_height']-25 and p[3]>p[1]]
            p=parts[len(parts)//2];x=p[0]+14;y=(p[1]+p[3])//2
            app.long_press(x,y,.52);assert ro(app)['selected']
            saved=(ro(app)['active'],ro(app)['start'],ro(app)['end'])
            before=app.diag()['scroll_y']
            swipe(app,min(width-30,x+230),y,45)
            assert (ro(app)['active'],ro(app)['start'],ro(app)['end'])==saved,ro(app)
            assert app.diag()['scroll_y']<before,(before,app.diag()['scroll_y'])
            assert not ro(app)['menu']
            # The right margin is outside registered text and selection handles.
            margin=width-4
            swipe(app,margin,min(110,app.diag()['reader_height']//2),40)
            assert (ro(app)['active'],ro(app)['start'],ro(app)['end'])==saved,ro(app)
            screenshot(app,str(width)+'-selection-after-scroll')
            # A cancelled margin drag does not clear the previous range.
            press(app,margin,100);press(app,margin,125);press(app,margin,125,False,cancelled=True)
            assert ro(app)['selected'] and not ro(app)['menu']
            app.tap(margin,100);assert not ro(app)['selected']
            print('Reader scrolling keeps selection:',width,height,'passed',flush=True)
        finally:app.close()


if __name__=='__main__':
    for size in [(960,266),(640,360),(568,1210)]:run(*size)
