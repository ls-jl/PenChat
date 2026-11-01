"""Bounded physical-key capture; isolated user data, never uploads a report."""
import json,subprocess,time

def run_inner(c):
 read,wait,tap,drag,shell=[c[k] for k in ('state','wait','tap','drag','shell')];W,H=c['W'],c['H'];capture=c['capture'];data=c['DATA']
 def swipe(delta):
  viewport=read()['page']['viewport'];x=viewport[0]+4;y=H-40 if delta<0 else 40
  drag(x,y,x,min(H-12,max(12,y+delta)))
 def go(key):
  for _ in range(35):
   d=read();b=d['feedback']['actions'][key]
   if d['page']['scrolling']:time.sleep(.1);continue
   if b[1]>=0 and b[3]<H:tap((b[0]+b[2])//2,(b[1]+b[3])//2);time.sleep(.15);return
   swipe(120 if b[1]<0 else -120);time.sleep(.15)
  raise AssertionError(key)
 def draft():return json.loads(shell('cat '+data+'/.feedback/draft.json'))
 def step(name):return draft().get('steps',{}).get(name,{})
 def open_feedback():
  if read()['view']=='chat':
   tap(40,32);wait(lambda:read()['navigation_open']);tap(W*2//3-130,H-30);wait(lambda:read()['view']=='settings')
  if read()['view']=='settings':
   import sys
   sys.path.insert(0,str(c['ROOT']/'tests'));from settings_ui import activate
   activate(read,tap,swipe,'feedback');wait(lambda:read()['view']=='feedback')
 open_feedback();before=read()['scroll_y'];capture('feedback-form')
 tap(W-55,152);wait(lambda:draft().get('adapt'));assert not read()['feedback']['collecting'];go('adapt');capture('feedback-adapt')
 native=c['pid']()
 go('start_scan');wait(lambda:step('scan').get('status')=='listening');assert step('scan')['blocking']
 actual=[d for d in step('scan')['devices'] if not d.get('name','').startswith('PenChat-')]
 scan_path=next(d['path'] for d in actual if 134 in d.get('key_codes',[]))
 home_path=next(d['path'] for d in actual if 102 in d.get('key_codes',[]))
 assert shell(c['key_source']+' --probe '+scan_path)=='busy'
 assert shell(c['key_source']+' --probe '+home_path)=='busy'
 shell(c['key_source']+' --scan '+c['key_socket'])
 wait(lambda:step('scan').get('status')=='complete');assert not step('scan')['blocking'];assert c['pid']()==native and read()['visible'] and read()['view']=='feedback'
 assert [e['value'] for e in step('scan')['events'] if e['code']==134]==[1,0];capture('feedback-scan-blocked')
 go('start_home');wait(lambda:step('home').get('status')=='listening');assert step('home')['blocking']
 assert shell(c['key_source']+' --probe '+home_path)=='busy'
 shell(c['key_source']+' --home '+c['key_socket'])
 wait(lambda:step('home').get('status')=='complete');assert not step('home')['blocking'];assert c['pid']()==native and read()['visible'] and read()['view']=='feedback'
 assert [e['value'] for e in step('home')['events'] if e['code']==102]==[1,0];capture('feedback-home-blocked')
 # Verify the real production 30-second limit, without a host-only test clock.
 go('start_scan');wait(lambda:step('scan').get('status')=='listening');started=time.monotonic();last_touch=started
 while time.monotonic()-started<35 and step('scan').get('status') in ('listening','pressed'):
  if time.monotonic()-last_touch>5:tap(W-3,H-3);last_touch=time.monotonic() # Ordinary wake activity, no setting changes.
  time.sleep(.15)
 s=step('scan');assert s['status']=='timeout' and not s['blocking'],s
 assert 29900<=s['elapsed_ms']<=31000,s # Includes evdev/RCU close latency after the 30s polling deadline.
 wait(lambda:read()['modal']);capture('feedback-timeout');tap(W-60,H-40);wait(lambda:not read()['modal'])
 go('start_scan');wait(lambda:step('scan').get('status')=='listening');go('stop_keys');wait(lambda:not read()['feedback']['collecting'])
 assert shell(c['key_source']+' --probe '+home_path)=='free'
 assert shell(c['key_source']+' --probe '+scan_path)=='free'
 assert not step('scan')['blocking'];assert shell('ls -l /proc/'+native+'/fd').count(' -> /dev/input/')<=1
 # Home must regain its actual function after the capture ends.
 assert shell(c['key_source']+' --physical '+home_path)=='sent';time.sleep(.4)
 shell('miniapp_cli capture /tmp/penchat-feedback-home.png');subprocess.run(['adb','-s',c['SERIAL'],'pull','/tmp/penchat-feedback-home.png',str(c['OUT']/'feedback-home-restored.png')],check=True,capture_output=True)
 c['start_app']();wait(lambda:read()['visible']);time.sleep(.4);open_feedback()
 if read()['feedback']['screen']=='form':go('adapt')
 capture('feedback-resumed-review');go('review');go('preview');wait(lambda:read()['feedback']['screen']=='preview');capture('feedback-preview')
 assert not list(filter(None,shell('find '+data+'/.feedback/reports -type f').splitlines()))
 shell('rm -f /tmp/penchat-feedback-home.png');tap(40,32);tap(40,32);wait(lambda:read()['view']=='settings')
 c['result']['feedback']={'exclusive_capture':True,'scan_suppressed':True,'home_suppressed':True,'early_release':True,'timeout_ms':s['elapsed_ms'],'timeout_popup':True,'cancel_released':True,'home_restored':True,'no_upload':True}

def run(c):
 import os,select
 source=c['OUT']/'feedback-key-source';remote='/tmp/penchat-feedback-key-source';sock='/tmp/penchat-feedback-source-'+str(os.getpid())+'.sock'
 subprocess.run([c['compiler'],'-std=c++11','-Os','-static-libstdc++','-static-libgcc',str(c['ROOT']/'tests/device/feedback_key_source.cpp'),'-o',str(source)],check=True)
 subprocess.run(['adb','-s',c['SERIAL'],'push',str(source),remote],check=True,capture_output=True)
 c['shell']('chmod 755 '+remote)
 log=(c['OUT']/'feedback-source-error.txt').open('w')
 proc=subprocess.Popen(['adb','-s',c['SERIAL'],'shell','-t',remote+' --server '+sock],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True)
 try:
  assert select.select([proc.stdout],[],[],5)[0],'Key source did not start'
  assert json.loads(proc.stdout.readline())['ready']
  time.sleep(.2);c['key_source']=remote;c['key_socket']=sock;run_inner(c)
 finally:
  c['shell'](remote+' --quit '+sock+'; rm -f '+remote+' '+sock)
  try:proc.wait(timeout=4)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=4)
  proc.stdin.close();proc.stdout.close();log.close()
