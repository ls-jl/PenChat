#!/usr/bin/env python3
import math,os,pathlib,sqlite3,struct,tempfile,time,zlib
from test_lvgl_app import Native,ROOT,wait_for
from test_chat_style import snapshot
import test_lvgl_app as transport
transport.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
def point(x,y,angle):
    return (y,266-x) if angle==90 else (960-x,266-y) if angle==180 else (960-y,x) if angle==270 else (x,y)
def png(path,w,h):
    def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
    data=b''.join(b'\0'+b''.join(bytes([220 if (x//20+y//20)%2 else 30,(x*255//w),y*255//h]) for x in range(w)) for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b''))
for angle in [0,90,180,270]:
    with tempfile.TemporaryDirectory(prefix='penchat-editor-') as directory:
        p=pathlib.Path(directory);app=Native(binary,p)
        def tap(x,y):app.tap(*point(x,y,angle))
        def drag(x,y,dx,dy):
            px,py=point(x,y,angle);app.send('touch',x=px,y=py,pressed=True);time.sleep(.08)
            for step in range(1,13):
                px,py=point(x+dx*step/12,y+dy*step/12,angle)
                app.send('touch',x=round(px),y=round(py),pressed=True);time.sleep(.025)
            app.send('touch',x=round(px),y=round(py),pressed=False);time.sleep(.08)
        try:
            folder=p/'camera-tmp';folder.mkdir(exist_ok=True);photo=folder/'source.png'
            portrait=angle%180;w,h=(600,800) if portrait else (800,600);png(photo,w,h)
            app.send('camera.test',photo={'path':str(photo),'width':w,'height':h,'orientation':angle})
            wait_for(lambda:app.diag()['view']=='crop');time.sleep(.3)
            s=app.diag();pw,ph=s['crop_preview'];vw,vh=s['crop_viewport'];pan=s['crop_pan']
            vx,vy=(44,68) if portrait else (98,44)
            dx,dy=(pw-70,ph-65)
            drag(vx+pan[0]+3,vy+pan[1]+3,dx,dy)
            rect=app.diag()['crop_rect'];assert abs(rect[0]-dx)<=1 and abs(rect[1]-dy)<=1,(angle,rect,dx,dy)
            if portrait:select=(222,738);mode=(133,788);plus=(101,738);confirm=(70,934)
            else:select=(922,64);mode=(846,114);plus=(820,64);confirm=(794,230)
            snapshot(app,'photo-before-'+str(angle))
            tap(*select);s=app.diag();assert s['crop_zoom']>2,s
            assert s['crop_rect']==rect
            snapshot(app,'photo-zoom-'+str(angle))
            assert app.diag()['crop_pan_mode']
            before_pan=app.diag()['crop_pan']
            drag(vx+vw/2,vy+vh/2,-20,-16)
            assert app.diag()['crop_rect']==rect
            assert app.diag()['crop_pan'] != before_pan
            tap(*mode);assert not app.diag()['crop_pan_mode']
            tap(*select);tap(*mode);s=app.diag();zoom=s['crop_zoom'];pan=s['crop_pan']
            drag(vx+pan[0]+(rect[0]+rect[2])*zoom-2,vy+pan[1]+(rect[1]+rect[3]/2)*zoom,-22,0)
            final=app.diag()['crop_rect'];assert final[2]<rect[2] and final[0]==rect[0],(angle,final,rect)
            tap(*confirm);wait_for(lambda:app.diag()['view']=='chat')
            with sqlite3.connect(p/'penchat.db') as db:actual=db.execute('select width,height from message_assets').fetchone()
            expected=(math.ceil((final[0]+final[2])/pw*w)-math.floor(final[0]/pw*w),math.ceil((final[1]+final[3])/ph*h)-math.floor(final[1]/ph*h))
            assert actual==expected,(angle,actual,expected)
        finally:app.close()
print('Photo editor: four orientations, inverse touch mapping, selection magnification, pan/resize separation, original-pixel crop output passed.')
