#!/usr/bin/env python3
"""Actual native editor: pointer cancellation, composition protocol and handoff."""
import pathlib, tempfile, time, signal, os
import test_lvgl_app as t
t.port=1
OUT=pathlib.Path(os.environ.get('PENCHAT_KEYBOARD_ARTIFACTS',t.ROOT/'.work/keyboard'));OUT.mkdir(parents=True,exist_ok=True)
def shot(app,name):
 p=app.directory/'lvgl-diagnostic.png'
 if p.exists():p.unlink()
 app.process.send_signal(signal.SIGUSR1);t.wait_for(p.exists)
 (OUT/(name+'.png')).write_bytes(p.read_bytes())
def state(a):return a.diag()['editor']
def key(a,value):
 k=next(k for k in state(a)['keys'] if k['key']==value);a.tap(k['x'],k['y'])
def latest(a,text):
 while True:
  req=a.receive('ime.request')
  if req['text']==text:return req
  a.send('ime.result',session=req['session'],sequence=req['sequence'],revision=req['revision'],ok=True,candidates=['旧'])
def reply(a,r,**v):
 a.send('ime.result',session=r['session'],sequence=r['sequence'],revision=r['revision'],ok=True,**v);time.sleep(.08)
with tempfile.TemporaryDirectory(prefix='penchat-keyboard-') as temp:
 a=t.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl')),temp,official_adapter=False)
 try:
  a.tap(500,232);assert state(a)['active'] and state(a)['multiline']
  # Enter commits the original letters, even before a candidate reply arrives.
  key(a,'n');raw_request=a.receive('ime.request');key(a,'i');key(a,'enter')
  assert state(a)['active'] and state(a)['length']==2 and state(a)['preedit_length']==0
  reply(a,raw_request,candidates=['你']);assert state(a)['candidate_count']==0
  a.tap(860,236);official=a.receive('keyboard.open');assert official['text']=='ni'
  a.send('keyboard.result',requestId=official['requestId'],confirmed=False,text='');time.sleep(.1)
  a.tap(630,100);a.tap(500,232)
  key(a,'n');r=a.receive('ime.request');key(a,'i')
  reply(a,r,candidates=['旧']);assert state(a)['candidate_count']==0
  r=latest(a,'ni');reply(a,r,candidates=['你','呢','泥','拟','逆','倪','妮','腻'])
  shot(a,'composition');a.tap(930,28);shot(a,'candidates');a.tap(930,28)
  key(a,'space');r=a.receive('ime.request');assert r['operation']=='choose'
  reply(a,r,committed='你',remaining='');assert state(a)['active'] and state(a)['length']==1
  a.tap(870,100);assert not state(a)['active'];assert a.diag()['draft_length']==1
  a.tap(500,232);a.tap(730,100);shot(a,'english')
  key(a,'shift');assert state(a)['shift']==1
  key(a,'A');assert state(a)['shift']==0
  key(a,'shift');key(a,'shift');assert state(a)['shift']==2
  key(a,'B');assert state(a)['shift']==2
  key(a,'shift');key(a,'c')
  # Narrow key gutters and small edge jitter must still deliver exactly once.
  before=state(a)['length']
  a.tap(59,38)  # the former dead gap between q and w
  assert state(a)['length']==before+1
  a.send('touch',x=55,y=38,pressed=True);time.sleep(.03)
  a.send('touch',x=62,y=38,pressed=True);time.sleep(.03)
  a.send('touch',x=62,y=38,pressed=False);time.sleep(.1)
  assert state(a)['length']==before+2
  # Back-to-back events in one bridge batch must not lose or duplicate taps.
  before=state(a)['length']
  for _ in range(10):
    a.send('touch',x=31,y=38,pressed=True)
    a.send('touch',x=31,y=38,pressed=False)
  t.wait_for(lambda:state(a)['length']==before+10)
  # Slide away and return must not type a key.
  k=next(k for k in state(a)['keys'] if k['key']=='q');before=state(a)['length']
  a.send('touch',x=k['x'],y=k['y'],pressed=True);time.sleep(.06)
  a.send('touch',x=700,y=90,pressed=True);time.sleep(.06)
  a.send('touch',x=k['x'],y=k['y'],pressed=False);time.sleep(.1)
  assert state(a)['length']==before
  a.tap(38,238);shot(a,'numbers');key(a,'1');a.tap(38,238);shot(a,'symbols');key(a,'[')
  before=state(a)['length']
  a.tap(730,236);shot(a,'edit-menu');a.tap(815,168);key(a,'delete')
  assert state(a)['length']==0
  a.tap(730,236);a.tap(905,168);assert state(a)['length']==before
  # Official cancel preserves native text, cursor and original pinyin.
  a.tap(38,238);a.tap(730,100);key(a,'n');key(a,'i')
  before=state(a);a.tap(860,236);request=a.receive('keyboard.open')
  assert request['multiline'] and 'ni' in request['text'];shot(a,'official-handoff')
  a.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='lost');time.sleep(.12)
  after=state(a);assert after['active'] and after['cursor']==before['cursor'] and after['preedit_length']==2
  # Official confirmation completes the transaction exactly once.
  a.tap(860,236);request=a.receive('keyboard.open')
  long='中文😀 English\n'+('长文本保持可滚动。\n'*18)
  a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=long);time.sleep(.12)
  assert not state(a)['active'] and a.diag()['draft_length']==len(long)
  a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='duplicate');time.sleep(.05)
  assert a.diag()['draft_length']==len(long)
  a.tap(500,232);shot(a,'long-text')
  # Background cancels held deletion and rejects stale engine replies.
  k=next(k for k in state(a)['keys'] if k['key']=='delete')
  a.send('touch',x=k['x'],y=k['y'],pressed=True);time.sleep(.7)
  a.send('visibility',visible=False);time.sleep(.15);n=state(a)['length'];time.sleep(.35);assert state(a)['length']==n
  a.send('visibility',visible=True);time.sleep(.1);a.tap(630,100);assert a.diag()['draft_length']==len(long)
  assert not state(a)['active']
  a.open_settings();a.tap(180,80);a.tap(790,30);a.tap(250,120)
  assert state(a)['secret'] and not state(a)['chinese'] and not state(a)['multiline']
  key(a,'a');shot(a,'secret')
  a.tap(860,236);request=a.receive('keyboard.open')
  assert request['text']=='a' and request['inputType']=='EnUSOnly' and request['multiline'] is False
  a.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='discard');time.sleep(.1)
  assert state(a)['active'] and state(a)['length']==1
  a.tap(630,100)
 finally:a.close()
print('native keyboard composition, layout, handoff and lifecycle checks passed')
