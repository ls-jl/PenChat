"""Logical multi-pointer fixtures shared by the crop and original-image tests."""
import time

def mapped(a,x,y,crop=False):
    if not crop:return round(x),round(y)
    s=a.diag();left,top,_,_=s['crop_viewport_bounds'];x+=left;y+=top
    cx,cy=s['width']/2,s['height']/2;dx,dy=x-cx,y-cy
    angle=s['camera_angle']
    return tuple(round(v) for v in ((cx+dy,cy-dx) if angle==90 else (cx-dx,cy-dy) if angle==180 else (cx-dy,cy+dx) if angle==270 else (x,y)))

def action(a,key):
    b=a.diag()['crop_actions'][key];s=a.diag();cx,cy=s['width']/2,s['height']/2
    x,y=(b[0]+b[2])/2,(b[1]+b[3])/2;dx,dy=x-cx,y-cy;angle=s['camera_angle']
    x,y=(cx+dy,cy-dx) if angle==90 else (cx-dx,cy-dy) if angle==180 else (cx-dy,cy+dx) if angle==270 else (x,y)
    a.tap(round(x),round(y))

def send(a,points,crop=False,cancelled=False,gesture=None):
    data=[]
    for id,x,y in points:
        x,y=mapped(a,x,y,crop);data.append({'id':id,'x':x,'y':y})
    x,y=(data[0]['x'],data[0]['y']) if data else (0,0)
    a.send('touch',x=x,y=y,pressed=bool(data),cancelled=cancelled,points=data,gesture=gesture if gesture is not None else getattr(a,'image_serial',1))
    time.sleep(.035)

def start(a):a.image_serial=getattr(a,'image_serial',0)+1

def pinch(a,x,y,distance=80,scale=2,crop=False,release=True,move=(0,0)):
    start(a)
    send(a,[(11,x-distance/2,y)],crop)
    send(a,[(11,x-distance/2,y),(22,x+distance/2,y)],crop)
    for step in range(1,9):
        t=step/8;half=distance/2*(1+(scale-1)*t);mx,my=move[0]*t,move[1]*t
        # Reverse the array order too: identifiers, not array slots, own fingers.
        send(a,[(22,x+mx+half,y+my),(11,x+mx-half,y+my)],crop)
    if release:
        send(a,[(11,x+move[0]-distance*scale/2,y+move[1])],crop)
        send(a,[],crop);time.sleep(.2)

def drag(a,x,y,dx,dy,crop=False):
    start(a);send(a,[(11,x,y)],crop)
    for step in range(1,9):send(a,[(11,x+dx*step/8,y+dy*step/8)],crop)
    send(a,[],crop)

def capture(a,target):
    import os,pathlib,signal
    image=a.directory/'lvgl-diagnostic.png'
    if image.exists():image.unlink()
    a.process.send_signal(signal.SIGUSR1)
    deadline=time.monotonic()+max(10,float(os.environ.get('PENCHAT_TEST_TIMEOUT','10')))
    while time.monotonic()<deadline:
        data=image.read_bytes() if image.exists() else b''
        if data.endswith(b'\x00\x00\x00\x00IEND\xaeB\x60\x82'):
            pathlib.Path(target).write_bytes(data);return
        if a.process.poll() is not None:raise AssertionError('Native exited while capturing image')
        time.sleep(.03)
    raise AssertionError('Native image capture timed out')
