#!/usr/bin/env python3
"""Fixed crop frame, original-pixel output and rotation-aware multi-pointer editing."""
import os,pathlib,sqlite3,struct,tempfile,zlib
from test_lvgl_app import Native,ROOT,wait_for
from test_chat_style import snapshot
from image_gestures import action,drag,pinch,send,start
import test_lvgl_app as transport
transport.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
def png(path,w,h):
    def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
    # Real black content on both edges must be retained, not heuristically trimmed.
    data=b''.join(b'\0'+b''.join(bytes([0,0,0]) if x<20 or x>=w-20 else bytes([220 if (x//20+y//20)%2 else 30,x*255//w,y*255//h]) for x in range(w)) for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b''))

def begin(a,p,angle=0,w=800,h=600):
    folder=p/'camera-tmp';folder.mkdir(exist_ok=True);photo=folder/'source.png';png(photo,w,h)
    a.send('camera.test',photo={'path':str(photo),'width':w,'height':h,'orientation':angle})
    wait_for(lambda:a.diag()['view']=='crop' and a.diag()['crop_loaded'])
    return photo

def run(w,h,angle):
    with tempfile.TemporaryDirectory(prefix='penchat-gesture-crop-') as directory:
        p=pathlib.Path(directory);a=Native(binary,p,screen={'width':w,'height':h})
        try:
            iw,ih=(600,800) if angle%180 else (800,600)
            begin(a,p,angle,iw,ih);s=a.diag();frame=s['crop_selection'];x,y,fw,fh=frame
            assert s['crop_source_rect']==[0,0,iw,ih]
            assert set(s['crop_actions'])=={'cancel','reset','album','confirm'}
            assert s['crop_viewport'][0]==(h if angle%180 else w)-16
            assert abs(fw/fh-iw/ih)<.001
            snapshot(a,f'photo-fit-{w}-{angle}')
            pan=s['crop_pan'];drag(a,x+2,y+2,45,35,True)
            s=a.diag();assert s['crop_pan']==pan
            assert s['crop_selection'][0]>x+40 and s['crop_selection'][1]>y+30
            frame=s['crop_selection'];x,y,fw,fh=frame
            drag(a,x+fw/2,y+fh/2,20,16,True)
            s=a.diag();assert s['crop_selection']==frame and s['crop_pan']!=pan
            pinch(a,x+fw/2,y+fh/2,distance=min(40,fw/2),scale=2,crop=True)
            s=a.diag();assert s['crop_zoom']>1.9 and s['crop_selection']==frame
            assert s['image_touch']['peak']==2 and s['image_touch']['pinch_updates']>=8
            before=s['crop_pan'];drag(a,x+fw/2,y+fh/2,-25,-18,True)
            s=a.diag();assert s['crop_selection']==frame and s['crop_pan']!=before
            before=s['crop_pan'];drag(a,x+fw-2,y+fh/2,-18,0,True)
            s=a.diag();assert s['crop_pan']==before and s['crop_selection'][2]<fw
            snapshot(a,f'photo-pinch-pan-resize-{w}-{angle}')
            expected=s['crop_source_rect'];action(a,'confirm');wait_for(lambda:a.diag()['view']=='chat')
            with sqlite3.connect(p/'penchat.db') as db:actual=db.execute('select width,height from message_assets').fetchone()
            assert actual==tuple(expected[2:]),(actual,expected)
            begin(a,p,angle,iw,ih);s=a.diag();x,y,fw,fh=s['crop_selection']
            drag(a,x+2,y+2,35,25,True);pinch(a,x+fw/2,y+fh/2,distance=40,scale=2,crop=True)
            action(a,'reset');s=a.diag()
            assert s['crop_zoom']==1 and s['crop_source_rect']==[0,0,iw,ih]
            snapshot(a,f'photo-reset-{w}-{angle}')
            # Three fingers and lifecycle interruption cannot confirm or modify later controls.
            start(a);send(a,[(1,x+10,y+10),(2,x+50,y+10),(3,x+80,y+10)],True)
            send(a,[],True);assert a.diag()['view']=='crop'
            a.send('visibility',visible=False);a.send('visibility',visible=True)
            assert a.diag()['crop_source_rect']==[0,0,iw,ih]
        finally:a.close()

if __name__=='__main__':
    for w,h in [(960,266),(936,280)]:
        for angle in (0,90,180,270):run(w,h,angle)
    print('Photo gestures: two screens/four poses, fixed frame, edge resize, pinch IDs, pan, reset, black content and original-pixel crop output passed.')
