"""Physical two-slot touches through native evdev, isolated images and ORANGE captures."""
import pathlib,shlex,subprocess,time,types

def run(env):
    d=types.SimpleNamespace(**env);W,H=d.W,d.H
    def photo(x,y,angle):
        return (round(y),round(H-x)) if angle==90 else (round(W-x),round(H-y)) if angle==180 else (round(W-y),round(x)) if angle==270 else (round(x),round(y))
    def local(x,y,crop=False):
        if not crop:return round(x),round(y)
        s=d.state();a=s['camera_angle'];b=s['crop_viewport_bounds']
        cx,cy=W/2,H/2;dx,dy=x+b[0]-cx,y+b[1]-cy
        return tuple(round(v) for v in ((cx+dy,cy-dx) if a==90 else (cx-dx,cy-dy) if a==180 else (cx-dy,cy+dx) if a==270 else (x+b[0],y+b[1])))
    def pinch(x,y,distance=80,scale=2,crop=False):
        p=[local(x-distance/2,y,crop),local(x+distance/2,y,crop),local(x-distance*scale/2,y,crop),local(x+distance*scale/2,y,crop)]
        args=[]
        for px,py in p:args.extend(d.raw(px,py))
        out=d.shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+d.event+' /tmp/penchat-display-tap --pinch '+' '.join(map(str,args))+' 2>&1')
        time.sleep(.25);return out
    def drag(x,y,dx,dy,crop=False,hold=False):
        p=local(x,y,crop);q=local(x+dx,y+dy,crop);args=d.raw(*p)+d.raw(*q)
        command='PENCHAT_TOUCH_DEVICE=/dev/input/'+d.event+' /tmp/penchat-display-tap '
        command+=('--drag-hold ' if hold else '')+' '.join(map(str,args))+(' 450 200' if hold else ' 20')
        d.shell(command+' 2>/dev/null');time.sleep(.15)
    def crop_action(key):
        s=d.state();b=s['crop_actions'][key];cx,cy=W/2,H/2;x,y=(b[0]+b[2])/2,(b[1]+b[3])/2;dx,dy=x-cx,y-cy;a=s['camera_angle']
        p=(cx+dy,cy-dx) if a==90 else (cx-dx,cy-dy) if a==180 else (cx-dy,cy+dx) if a==270 else (x,y)
        d.tap(*map(round,p))
    def double():
        x,y=d.raw(W//2,H//2)
        d.shell('for i in 1 2; do PENCHAT_TOUCH_DEVICE=/dev/input/'+d.event+' /tmp/penchat-display-tap '+str(x)+' '+str(y)+' 2>/dev/null; done')
    def jpeg_size(data):
        i=2
        while i<len(data):
            if data[i]!=255:i+=1;continue
            marker=data[i+1];i+=2
            if marker in (0xd8,0xd9):continue
            length=int.from_bytes(data[i:i+2],'big')
            if marker in (0xc0,0xc1,0xc2):return [int.from_bytes(data[i+5:i+7],'big'),int.from_bytes(data[i+3:i+5],'big')]
            i+=length
        raise AssertionError('No JPEG dimensions')
    # First check the actual native two-finger reader on both boards.
    for _ in range(10):
        before=d.state();item=next((i for i in before['chat_images'] if i['y']>=0 and i['y']+i['height']<=H),None)
        if item:break
        d.settings_swipe(100)
    assert item
    d.tap(item['x']+item['width']//2,item['y']+item['height']//2)
    d.wait(lambda:d.state()['image_viewer'].get('loaded'));d.capture('gestures-viewer-fit')
    pinch(W/2,H/2,100,3)
    s=d.state();assert s['image_viewer']['zoom']>2.8,('Native touch did not deliver pinch',s['image_viewer'],s['touch'])
    assert s['image_viewer']['touch_peak']==2 and s['image_viewer']['pinch_updates']>=2
    pan=s['image_viewer']['pan'];drag(W/2,H/2,-40,-25)
    assert d.state()['image_viewer']['pan']!=pan;d.capture('gestures-viewer-pinch-pan')
    pinch(W/2,H/2,100,.2);d.wait(lambda:d.state()['image_viewer']['zoom']==1)
    d.capture('gestures-viewer-auto-fit');double();assert d.state()['image_viewer']['zoom']==2
    double();assert d.state()['image_viewer']['zoom']==1
    d.tap(W-38,30);d.wait(lambda:not d.state()['image_viewer']['open'])
    after=d.state();assert after['session']==before['session'] and after['scroll_y']==before['scroll_y']
    d.result['real_two_finger_viewer']=True
    if d.SERIAL!='MEB0400008900794':return
    original_report=d.shell('cat /sys/kernel/gsensor/report_active')
    captured=[]
    for angle in (0,90,180,270):
        s=d.state()
        if s['to_bottom_visible']:d.tap(s['to_bottom_x']+22,s['to_bottom_y']+22)
        d.wait(lambda:d.state()['toolbar_y']==H-58)
        d.tap(128,H-34);s=d.state();b=s['attachment_actions']['camera'];d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
        d.wait(lambda:d.state()['view']=='camera')
        for _ in range(4):
            current=d.state()['camera_angle']
            if current==angle:break
            pw,ph=(H,W) if current%180 else (W,H)
            d.tap(*photo(pw/2 if current%180 else pw-72,ph-26 if current%180 else ph-44,current))
            d.wait(lambda:d.state()['camera_angle']==(current+90)%360)
        d.wait(lambda:d.state()['camera_ready'],timeout=12)
        pw,ph=(H,W) if angle%180 else (W,H)
        d.tap(*photo(pw/2 if angle%180 else pw-72,ph-90 if angle%180 else ph/2,angle))
        d.wait(lambda:d.state()['view']=='crop' and d.state()['crop_loaded'],timeout=12)
        s=d.state();iw,ih=s['crop_source'];assert s['crop_source_rect']==[0,0,iw,ih]
        assert d.shell('cat /sys/kernel/gsensor/report_active')==original_report
        d.capture('gestures-crop-fit-'+str(angle));x,y,w,h=s['crop_selection'];pan=s['crop_pan']
        drag(x+2,y+2,30,24,True);s=d.state();assert s['crop_pan']==pan
        frame=s['crop_selection'];x,y,w,h=frame
        pinch(x+w/2,y+h/2,40,2,True);s=d.state()
        assert s['crop_selection']==frame and s['crop_zoom']>1.8 and s['image_touch']['peak']==2
        pan=s['crop_pan'];drag(x+w/2,y+h/2,-20,-14,True,hold=angle==0)
        after=d.state();d.capture('gestures-crop-pan-probe-'+str(angle))
        assert after['crop_pan']!=pan and after['crop_selection']==frame,(angle,pan,after['crop_pan'],frame,after['crop_selection'],after['image_touch'],after['touch'],after['view'],after['visible'],after['keyboard'])
        d.capture('gestures-crop-pinch-pan-'+str(angle));crop_action('reset')
        assert d.state()['crop_source_rect']==[0,0,iw,ih] and d.state()['crop_zoom']==1
        s=d.state();x,y,w,h=s['crop_selection'];drag(x+2,y+2,30,24,True)
        expected=d.state()['crop_source_rect'];old=set(d.shell('find '+d.DATA+'/assets -type f').splitlines())
        crop_action('confirm');d.wait(lambda:d.state()['view']=='chat' and not d.state()['storage']['unsaved'])
        new=set(d.shell('find '+d.DATA+'/assets -type f').splitlines())-old
        jpg=next(p for p in new if p.lower().endswith('.jpg'))
        target=d.OUT/('gestures-crop-output-'+str(angle)+'.jpg')
        subprocess.run(['adb','-s',d.SERIAL,'pull',jpg,str(target)],check=True,capture_output=True)
        assert jpeg_size(target.read_bytes())==expected[2:]
        assert not d.state()['generating'];captured.append({'angle':angle,'source':[iw,ih],'output':expected[2:]})
    d.result['real_two_finger_crop_four_poses']=captured
