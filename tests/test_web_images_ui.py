#!/usr/bin/env python3
"""Offline inline web-image layout regression for all three layout classes."""
import json, os, pathlib, sqlite3, struct, tempfile, zlib
import test_lvgl_app as t
from image_gestures import capture

BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', t.ROOT/'build/host/penchat-lvgl'))
OUT = t.ROOT/'.work/web-images-ui'

def png(path, width, height):
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))
    rows = b''.join(b'\0'+bytes([40+y*160//height, 150, 210])*width for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0))+chunk(b'IDAT', zlib.compress(rows))+chunk(b'IEND', b''))

def fixture(folder):
    schema = (t.ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
    (folder/'assets').mkdir()
    images = []
    for ref, width, height in [('img_1', 60, 90), ('img_2', 90, 60)]:
        path = folder/'assets'/(ref+'.img')
        png(path, width, height)
        images.append({'ref':ref, 'path':str(path), 'width':width, 'height':height, 'source_url':'https://example.org/'})
    with sqlite3.connect(folder/'penchat.db') as db:
        db.executescript(schema)
        db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('fixture','Inline images','active',1,1,1)")
        db.execute("insert into app_settings values('active_session_id','\"fixture\"',1)")
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,metadata_json,created_at,updated_at) values('reply','fixture',1,'assistant','complete','text',?,?,1,1)", ('![Portrait](penchat-image:img_1)\n\n![Landscape](penchat-image:img_2)', json.dumps({'web_images':images})))

def run(width, height):
    with tempfile.TemporaryDirectory(prefix='penchat-web-images-') as directory:
        folder = pathlib.Path(directory)
        fixture(folder)
        app = t.Native(BINARY, folder, seed_model=False, screen={'width':width,'height':height,'direction':0})
        try:
            t.wait_for(lambda:len(app.diag()['chat_images'])==2)
            t.wait_for(lambda:all(p['height']>1 and p['width']>1 for p in app.diag()['chat_images']))
            images = sorted(app.diag()['chat_images'], key=lambda p:p['y'])
            expected = min(240, max(100, height//3))
            assert [p['height'] for p in images]==[expected, expected], images
            assert abs(images[0]['width']/expected-2/3)<.02, images
            assert abs(images[1]['width']/expected-1.5)<.02, images
            assert all(p['radius']==12 for p in images)
            for _ in range(12):
                first = min(app.diag()['chat_images'], key=lambda p:p['y'])
                if 4<=first['y'] and first['y']+first['height']<height-8:break
                app.settings_swipe(65 if first['y']<4 else -65)
            capture(app, OUT/f'{width}x{height}.png')
            first = min(app.diag()['chat_images'], key=lambda p:p['y'])
            app.tap(first['x']+first['width']//2, first['y']+first['height']//2)
            t.wait_for(lambda:app.diag()['image_viewer'].get('loaded',False))
            assert app.diag()['image_viewer']['frame']==[60,90]
        finally:
            app.close()

if __name__=='__main__':
    t.port = 1
    OUT.mkdir(parents=True, exist_ok=True)
    for dimensions in [(960,266),(640,260),(568,1210)]:
        run(*dimensions)
    print('Inline web images: three layouts, aspect ratio and original-image viewer passed')
