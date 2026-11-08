#!/usr/bin/env python3
"""Complete real editor input into the capsule without changing draft paragraphs."""
import argparse, pathlib, sqlite3, tempfile
parser = argparse.ArgumentParser()
parser.add_argument("--pixels", action="store_true", help="Also check screenshot glyph pixels (requires Pillow)")
options = parser.parse_args()
if options.pixels:
    from PIL import Image
import test_lvgl_app as t
from image_gestures import capture

t.port=1
out=t.ROOT/'.work/composer-draft';out.mkdir(parents=True,exist_ok=True)
for w,h in [(640,260),(960,266)]:
    with tempfile.TemporaryDirectory(prefix='penchat-composer-preview-') as directory:
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',pathlib.Path(directory),seed_model=False,
                   official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            def action(name):
                e=a.diag()['editor'];x1,y1,x2,y2=e['actions'][name];c=e['coordinate_space']
                a.tap(round((x1+x2)*w/(2*c['width'])),round((y1+y2)*h/(2*c['height'])))
            text='\n你好\n\n第二段 text\r\n'
            a.tap(w//2,h-34);t.wait_for(lambda:a.diag()['editor']['active'])
            t.wait_for(lambda:bool(a.diag()['editor']['actions']))
            action('official');request=a.receive('keyboard.open')
            a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=text)
            t.wait_for(lambda:not a.diag()['editor']['active'])
            d=a.diag();assert d['draft_length']==len(text),d['draft_length']
            image=out/f'{w}-capsule.png';capture(a,image)
            if options.pixels:
                pixels=Image.open(image).convert('RGB').crop((180,d['toolbar_y']+4,w-145,d['toolbar_y']+44))
                raw=pixels.tobytes()
                assert sum(1 for rgb in zip(raw[0::3],raw[1::3],raw[2::3]) if min(rgb)>220)>50,'Draft glyphs are invisible'
            a.tap(w//2,h-34);t.wait_for(lambda:a.diag()['editor']['active'])
            t.wait_for(lambda:bool(a.diag()['editor']['actions']))
            action('official');request=a.receive('keyboard.open')
            assert request['text']==text,'Presentation must not flatten the editable draft'
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
            t.wait_for(lambda:not a.diag()['keyboard']);action('cancel')
            with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
                assert db.execute('select count(*) from chat_messages').fetchone()[0]==0
        finally:a.close()
print('COMPOSER_COMPLETION_ORIGINAL_PARAGRAPHS_PRESERVED_NO_SEND_OK' + (' PIXELS_OK' if options.pixels else ''))
