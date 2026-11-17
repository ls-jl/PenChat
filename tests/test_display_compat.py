#!/usr/bin/env python3
"""Actual non-headless app + isolated DRM fixture: upper-overlay lifecycle/IME."""
import json,os,pathlib,socket,sqlite3,subprocess,tempfile,threading,time
import test_lvgl_app as ui
ROOT=ui.ROOT;OUT=ROOT/'.work/x6-compat';OUT.mkdir(parents=True,exist_ok=True)
DRM=pathlib.Path(os.environ.get('LIBDRM_DIR','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK/external/libdrm'))
lib=OUT/'libfixture-drm.so'
subprocess.run(['g++','-std=c++14','-shared','-fPIC','-DDRM_PROBE_LIBRARY','-I'+str(ROOT/'native/camera/src'),'-I'+str(DRM),'-I'+str(DRM/'include/drm'),str(ROOT/'tests/native/DrmProbeTest.cpp'),'-ldl','-o',str(lib)],check=True)
for width,height in [(960,266),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-compat-ui-') as directory:
  folder=pathlib.Path(directory);schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
  with sqlite3.connect(folder/'penchat.db') as db:
   db.executescript(schema);db.execute("insert into app_settings(key,value_json,updated_at) values('onboarding',?,1)",(json.dumps({'version':1,'complete':True,'step':6}),))
  config=folder/'screen.json';config.write_text(json.dumps({'screen':{'width':width,'height':height,'direction':0}}))
  parent,child=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET);parent.settimeout(15)
  log=(folder/'runtime.log').open('wb');env=os.environ.copy();env.update(LD_PRELOAD=str(lib),PENCHAT_FIXTURE_UPPER='1',PENCHAT_TEST_NATIVE_OCR_KEYS='1' if height>width else '0')
  process=subprocess.Popen([str(ROOT/'build/host/penchat-lvgl'),'--screen-config',str(config),'--bridge-fd',str(child.fileno()),'--data-dir',str(folder),'--workspace',str(ROOT),'--font',str(ROOT/'.deps/chinese.ttf'),'--tex-resources',str(ROOT/'.deps/microtex/res')],env=env,pass_fds=[child.fileno()],stdout=log,stderr=log);child.close()
  a=ui.Native.__new__(ui.Native);a.parent=parent;a.directory=folder;a.process=process;a.log=log;a.pending=[];a.generation=17;a.official_adapter=False;a.ocr_cancel_ack=True;a.ocr_prepare_adapter=True
  stop=threading.Event();state={'focus':False,'active':False,'panel':False,'awake':True}
  def heartbeat():
   seq=0
   while not stop.wait(.08):
    seq+=1
    try:
     a.send('screen.power',sequence=seq,available=True,known=True,awake=state['awake'])
     a.send('system.overlay.status',window_valid=True,panel_supported=True,panel_visible=state['panel'])
     a.send('window.focus',focused=state['focus'],sequence=seq)
     a.send('input.focus',active=state['active'],sequence=seq)
    except OSError:return
  original_send=a.send
  a.send=lambda kind,**kwargs:original_send(kind,**({'source':'native-evdev',**kwargs} if kind=='touch' else kwargs))
  a.send('init',ocrCapabilities={});thread=threading.Thread(target=heartbeat,daemon=True);thread.start()
  try:
   time.sleep(1.9);d=a.diag();assert process.poll() is None and not d['display_visible']
   assert d['display_info']['mode']=='upper_overlay' and d['display_info']['format']!=0
   state['focus']=True;a.receive('ready');state['active']=True;ui.wait_for(lambda:a.diag()['display_visible'])
   ui.wait_for(lambda:a.diag()['input_focused'])
   b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);ui.wait_for(lambda:a.diag()['editor']['active'])
   ui.wait_for(lambda:{'n','i'}.issubset({k['key'] for k in a.diag()['editor']['keys']}))
   for char in ['n','i']:
    k=next(k for k in a.diag()['editor']['keys'] if k['key']==char);a.tap(k['x'],k['y'])
   ui.wait_for(lambda:not a.diag()['editor']['pending']);before=a.diag()['editor']
   state.update(focus=False,active=False,panel=True);ui.wait_for(lambda:not a.diag()['display_visible'])
   assert a.diag()['editor']['session']==before['session'] and a.diag()['editor']['preedit_length']==2
   state.update(focus=True,active=True,panel=False);ui.wait_for(lambda:a.diag()['display_visible'] and a.diag()['input_focused'],timeout=15)
   assert a.diag()['editor']['session']==before['session'] and a.diag()['editor']['preedit_length']==2
   # Merged dev power gating must still dominate an upper-overlay display lease.
   state['awake']=False;ui.wait_for(lambda:not a.diag()['display_visible'] and not a.diag()['input_focused'])
   assert a.diag()['editor']['session']==before['session'] and a.diag()['editor']['preedit_length']==2
   state['awake']=True;ui.wait_for(lambda:a.diag()['display_visible'] and a.diag()['input_focused'],timeout=15)
   assert a.diag()['editor']['session']==before['session'] and a.diag()['editor']['preedit_length']==2
   a.editor_action('official');request=a.receive('keyboard.open');assert request['inline'] is False
   assert not a.diag()['display_visible']
   a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
   ui.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['display_visible'],timeout=15)
   assert a.diag()['editor']['session']==before['session'] and a.diag()['editor']['preedit_length']==2
   a.editor_action('official');request=a.receive('keyboard.open');assert request['inline'] is False
   a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='fixture confirmed')
   ui.wait_for(lambda:not a.diag()['keyboard'] and a.diag()['display_visible'],timeout=15)
   d=a.diag();assert d['draft_length']==len('fixture confirmed') and not d['generating'] and d['summary']['sessions']==0
   (OUT/f'compat-{width}-runtime.log').write_bytes((folder/'runtime.log').read_bytes())
  finally:
   stop.set();thread.join();a.close()
 print('Actual upper-overlay startup, panel retention and fullscreen official editing passed:',width,height,flush=True)
