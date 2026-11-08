"""Explicitly authorized portrait hardware, no user data or external API requests."""
import pathlib,shlex,subprocess,time,types

def run(env):
 d=types.SimpleNamespace(**env);assert d.SERIAL=='7G12000010905426' and d.H>d.W
 W,H=d.W,d.H
 def control(key):
  b=d.state()['editor']['actions'][key];d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
 def key(value):
  k=next(k for k in d.state()['editor']['keys'] if k['key']==value);d.tap(k['x'],k['y'])
 def attachment(key):
  d.tap(40,H-34);d.wait(lambda:d.state()['attachments']);b=d.state()['attachment_actions'][key]
  d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
 def pinch(x,y,scale=2,crop=False):
  if crop:
   b=d.state()['crop_viewport_bounds'];x+=b[0];y+=b[1]
  points=[(x-40,y),(x+40,y),(x-40*scale,y),(x+40*scale,y)];args=[]
  for px,py in points:args.extend(d.raw(round(px),round(py)))
  out=d.shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+d.event+' /tmp/penchat-display-tap --pinch '+' '.join(map(str,args))+' 2>&1')
  assert 'ranges=' in out;time.sleep(.25)
 # Real type-B frames enter the same evdev path used by a physical hand.
 photo=next(i for i in d.state()['chat_images'] if i['y']>=0 and i['y']+i['height']<H)
 d.tap(photo['x']+photo['width']//2,photo['y']+photo['height']//2);d.wait(lambda:d.state()['image_viewer']['loaded']);d.capture('portrait-viewer-fit')
 pinch(W/2,H/2,3);assert d.state()['image_viewer']['touch_peak']==2 and d.state()['image_viewer']['zoom']>2.8;d.capture('portrait-viewer-pinch')
 d.tap(W-38,30);d.wait(lambda:not d.state()['image_viewer']['open'])
 before=len(d.shell('ls -1 /proc/'+d.pid()+'/fd').splitlines())
 for cycle in range(20):
  d.tap(W//2,H-34);d.wait(lambda:d.state()['editor']['active']);d.wait(lambda:d.state()['scan_released']);d.wait(lambda:d.state()['editor']['top_panel_blocked'])
  if cycle==0:
   d.wait(lambda:d.state()['editor']['ocr_active']);d.capture('portrait-keyboard-26')
   for c in 'nihao':key(c)
   d.wait(lambda:d.state()['editor']['candidate_count']>0 and not d.state()['editor']['pending']);d.capture('portrait-candidates')
   b=d.state()['editor']['actions']['expand'];d.tap((b[0]+b[2])//2,(b[1]+b[3])//2);d.capture('portrait-candidates-expanded')
   b=d.state()['editor']['actions']['expand'];d.tap((b[0]+b[2])//2,(b[1]+b[3])//2);key('space')
   key('language');d.capture('portrait-keyboard-English');key('mode');d.capture('portrait-keyboard-number');key('symbols');d.capture('portrait-keyboard-symbol');key('mode')
   k=next(k for k in d.state()['editor']['keys'] if k['key']=='language');rx,ry=d.raw(k['x'],k['y']);d.shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+d.event+' /tmp/penchat-display-tap --hold '+str(rx)+' '+str(ry)+' 850')
   d.wait(lambda:d.state()['editor']['layout_menu']);b=d.state()['editor']['actions']['layout:2'];d.tap((b[0]+b[2])//2,(b[1]+b[3])//2);d.capture('portrait-keyboard-9')
  control('cancel');d.wait(lambda:not d.state()['editor']['active'])
 after=len(d.shell('ls -1 /proc/'+d.pid()+'/fd').splitlines());assert after<=before+12,(before,after)
 d.result['portrait_editor']={'cycles':20,'fd_before':before,'fd_after':after,'ocr_registered':True,'official_physical_confirmation':'pending manual verification','native_type_b_pinch':True}
 # Real rear sensor: low-resolution preview and full-size JPEG, no upload.
 photos=[];rss=[]
 for cycle in range(20):
  attachment('camera');d.wait(lambda:d.state()['view']=='camera');d.wait(lambda:d.state()['camera_ready'],timeout=20)
  rss.append(int(next(l.split()[1] for l in d.shell('cat /proc/'+d.pid()+'/status').splitlines() if l.startswith('VmHWM:'))))
  if cycle==0:
   d.capture('portrait-camera');b=d.state()['camera_controls']['shutter'];d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   d.wait(lambda:d.state()['view']=='crop' and d.state()['crop_loaded'],timeout=30);s=d.state();assert sorted(s['crop_source'])==[2448,3264],s['crop_source']
   d.capture('portrait-HD-crop');x,y,w,h=s['crop_selection'];pinch(x+w/2,y+h/2,2,True);assert d.state()['crop_zoom']>1.8;d.capture('portrait-HD-crop-pinch')
   remote=d.shell('find '+d.DATA+'/camera-tmp -type f').splitlines();assert len(remote)==1
   subprocess.run(['adb','-s',d.SERIAL,'pull',remote[0],str(d.OUT/'portrait-HD-original.jpg')],check=True,capture_output=True)
   photos.append(s['crop_source']);b=d.state()['crop_actions']['cancel'];d.tap((b[0]+b[2])//2,(b[1]+b[3])//2);d.wait(lambda:d.state()['view']=='camera')
  d.tap(40,32);d.wait(lambda:d.state()['view']=='chat');d.wait(lambda:not d.state()['camera_ready'])
 d.result['portrait_camera']={'cycles':20,'photos':photos,'peak_rss_kib':max(rss),'four_pose_optical_calibration':'pending human target','sharpness':'pending human printed target'}
 d.capture('portrait-returned-chat')
 # No synthetic scan is sent: transport/lifecycle only, not paper-quality acceptance.
 resident=d.wait(lambda:(s if (s:=d.state())['scanner'].get('type')=='ready' else None),timeout=30)['scanner']['worker_pid']
 scans=[]
 for cycle in range(20):
  previous=d.state().get('scan_result',{}).get('scanId')
  d.shell('/tmp/penchat-display-key /dev/input/by-path/led_control 134 1 500 100')
  s=d.wait(lambda:(s if (s:=d.state()).get('scan_result',{}).get('type')=='finished' and s['scan_result'].get('scanId')!=previous else None),timeout=90)
  scan=s['scan_result'];counts=scan['counts'];assert counts['received']>=5 and counts['spilled']==0 and counts['system_reserve_bytes']>=32*1024*1024,scan
  assert d.shell('pidof penchat-scanner')==str(resident)
  assert counts['discarded_backlog']==0 and counts['dropped_raw']==counts['recovered_frames']
  scans.append({'received':counts['received'],'usable':scan['usable'],'status':scan['status']})
  if cycle in (0,19):d.capture('portrait-scan-result-'+str(cycle))
  # Empty results accept a new pen press immediately. Nonempty results are discarded.
  if scan['usable'] or cycle==19:
   d.tap(W//2,H-36);d.wait(lambda:d.state()['view']=='chat')
   d.wait(lambda:d.state()['scanner'].get('type')=='ready',timeout=20)
 d.result['portrait_scanner']={'cycles':scans,'resident_pid':resident,'paper_quality_validated':False,'calibration':'independent neutral; awaiting physical paper'}
 d.side_back();d.wait(lambda:not d.pid(),timeout=20);d.start_app()
