#!/usr/bin/env python3
"""Actual-process input, OCR, selection/clipboard and lifecycle regression."""
import pathlib,tempfile,time,signal,os
import test_lvgl_app as t
t.port=1
OUT=pathlib.Path(os.environ.get('PENCHAT_KEYBOARD_ARTIFACTS',t.ROOT/'.work/keyboard'));OUT.mkdir(parents=True,exist_ok=True)
def shot(a,name):
 p=a.directory/'lvgl-diagnostic.png'
 if p.exists():p.unlink()
 a.process.send_signal(signal.SIGUSR1);t.wait_for(p.exists);(OUT/(name+'.png')).write_bytes(p.read_bytes())
def state(a):return a.diag()['editor']
def key(a,value):
 k=next(k for k in state(a)['keys'] if k['key']==value);a.tap(k['x'],k['y'])
def layout(a,value):
 k=next(k for k in state(a)['keys'] if k['key']=='language')
 hold(a,k['x'],k['y']);t.wait_for(lambda:state(a)['layout_menu'])
 a.editor_action('layout:'+str({'zh26':0,'en26':1,'zh9':2}[value]))
 assert state(a)['layout']==value
def official(a,confirmed=False,text=''):
 a.editor_action('official');r=a.receive('keyboard.open');a.send('keyboard.result',requestId=r['requestId'],confirmed=confirmed,text=text);time.sleep(.1);return r
def export_text(a):return official(a)['text']
def hold(a,x,y):a.send('touch',x=x,y=y,pressed=True);time.sleep(.52);a.send('touch',x=x,y=y,pressed=False);time.sleep(.12)
def double(a,x,y):a.tap(x,y);a.tap(x,y)
def seed(a,text):
 official(a,True,text);a.tap(500,232)
 layout(a,'en26')
def scan(a,r,capture,text,**extra):
 a.send('ocr.result',requestId=r['requestId'],session=r['session'],mode=r['mode'],capture=capture,confirmed=True,text=text,**extra);time.sleep(.1)
with tempfile.TemporaryDirectory(prefix='penchat-keyboard-') as temp:
 a=t.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl')),temp,official_adapter=False)
 try:
  a.tap(500,232);assert state(a)['active'] and state(a)['multiline']
  policy=a.receive('editor.state');a.send('editor.policy',session=policy['session'],sequence=policy['sequence'],ok=True,blocked=True)
  t.wait_for(lambda:state(a)['top_panel_blocked']);assert a.receive('ocr.start')['mode']=='single'
  key(a,'n');key(a,'i');key(a,'enter')
  assert state(a)['length']==2 and state(a)['preedit_length']==0
  time.sleep(.15);assert state(a)['candidate_count']==0
  assert export_text(a)=='ni'
  a.tap(630,236);a.tap(500,232)
  for c in 'nihao':key(a,c)
  t.wait_for(lambda:state(a)['candidate_count']>0 and not state(a)['pending']);shot(a,'composition')
  a.tap(930,28);shot(a,'candidates');a.tap(640,100)
  t.wait_for(lambda:state(a)['preedit_length']==0 and state(a)['length']==2)
  a.tap(880,236);assert not state(a)['active'] and a.diag()['draft_length']==2
  a.tap(500,232);key(a,'language');assert not state(a)['chinese'];shot(a,'english')
  key(a,'shift');key(a,'A');assert state(a)['shift']==0
  key(a,'shift');key(a,'shift');assert state(a)['shift']==2
  key(a,'B');assert state(a)['shift']==2;key(a,'shift');key(a,'c')
  before=state(a)['length'];a.tap(59,38);assert state(a)['length']==before+1
  a.send('touch',x=55,y=38,pressed=True);a.send('touch',x=62,y=38,pressed=True);a.send('touch',x=62,y=38,pressed=False)
  t.wait_for(lambda:state(a)['length']==before+2)
  before=state(a)['length']
  for _ in range(10):a.send('touch',x=31,y=38,pressed=True);a.send('touch',x=31,y=38,pressed=False)
  t.wait_for(lambda:state(a)['length']==before+10)
  k=next(k for k in state(a)['keys'] if k['key']=='q');before=state(a)['length']
  a.send('touch',x=k['x'],y=k['y'],pressed=True);a.send('touch',x=700,y=90,pressed=True);a.send('touch',x=k['x'],y=k['y'],pressed=False);time.sleep(.1)
  assert state(a)['length']==before
  key(a,'mode');assert state(a)['mode']==1;shot(a,'numbers');key(a,'1');key(a,'symbols');assert state(a)['mode']==2;shot(a,'symbols');key(a,'[')
  key(a,'mode');assert state(a)['mode']==0;key(a,'mode');assert state(a)['mode']==1;key(a,'symbols');key(a,'symbols');assert state(a)['mode']==1;key(a,'mode')
  seed(a,'');key(a,',');key(a,'.');assert export_text(a)==',.'
  key(a,'language');key(a,',');key(a,'.');shot(a,'chinese-punctuation')
  key(a,'mode')
  for c in '?!;:()':key(a,c)
  key(a,'symbols');key(a,'[');key(a,']')
  assert export_text(a)==',.，。？！；：（）【】'
  key(a,'mode')
  seed(a,'alpha beta')
  a.send('touch',x=640,y=96,pressed=True);time.sleep(.5)
  a.send('touch',x=700,y=96,pressed=True)
  t.wait_for(lambda:state(a)['magnifier']);shot(a,'caret-magnifier')
  a.send('touch',x=300,y=96,pressed=True);time.sleep(.12)
  assert state(a)['cursor_drag'] and state(a)['magnifier'] and state(a)['cursor']==0
  a.send('touch',x=300,y=260,pressed=True);time.sleep(.3)
  assert state(a)['text_scroll']==0 and state(a)['length']==10
  a.send('touch',x=300,y=260,pressed=False);time.sleep(.1)
  assert not state(a)['magnifier'] and state(a)['length']==10
  a.send('touch',x=640,y=96,pressed=True);time.sleep(.5);a.send('touch',x=720,y=96,pressed=True)
  t.wait_for(lambda:state(a)['magnifier'])
  a.send('touch',x=720,y=96,pressed=False,cancelled=True);time.sleep(.1)
  assert not state(a)['magnifier'] and not state(a)['cursor_drag']
  seed(a,'m'*17);current=state(a);x=current['caret'][0]-4;y=current['caret'][1]-16
  a.send('touch',x=x,y=y,pressed=True);time.sleep(.5);a.send('touch',x=x+16,y=y,pressed=True)
  t.wait_for(lambda:state(a)['magnifier']);current=state(a)
  assert current['magnifier_bounds'][2]<a.diag()['width']
  assert abs((current['magnifier_caret'][0]+current['magnifier_caret'][2]+1)//2-current['caret'][0])<=1
  shot(a,'caret-magnifier-right');a.send('touch',x=x+16,y=y,pressed=False);time.sleep(.1)
  seed(a,'alpha\nbeta\ngamma\ndelta\nepsilon')
  current=state(a);double(a,current['text_bounds'][0]+20,min(current['text_bounds'][3]-6,current['caret'][1]-16))
  current=state(a);assert current['selected']
  handles=current['selection_handles'];assert any(h['bounds'][3]>current['text_bounds'][3] for h in handles)
  for h in handles:assert abs(h['x']-h['anchor'][0])<=1 and abs(h['y']-h['anchor'][1])<=1
  shot(a,'selection-bottom-overflow');end=next(h for h in handles if h['endpoint']==1);before=current['selection_end']
  a.send('touch',x=end['x'],y=end['y']+8,pressed=True);a.send('touch',x=end['x']-24,y=end['y']+8,pressed=True);a.send('touch',x=end['x']-24,y=end['y']+8,pressed=False);time.sleep(.1)
  assert state(a)['active'] and state(a)['selected'] and state(a)['selection_end']<before
  seed(a,'alpha beta 中文 👩‍💻')
  double(a,624,93);assert state(a)['selected'] and state(a)['selection_end']==5;shot(a,'selection')
  handles=state(a)['selection_handles'];assert len(handles)==2
  assert any(h['bounds'][0]<state(a)['text_bounds'][0] for h in handles)
  for h in handles:assert abs(h['x']-h['anchor'][0])<=1 and abs(h['y']-h['anchor'][1])<=1
  end=handles[1];a.send('touch',x=end['x'],y=end['y'],pressed=True);time.sleep(.06);a.send('touch',x=end['x']+35,y=end['y'],pressed=True);a.send('touch',x=end['x']+35,y=end['y'],pressed=False);time.sleep(.1)
  assert state(a)['selection_end']>5
  double(a,624,93);hold(a,624,93);assert state(a)['menu'];shot(a,'selection-menu');before=state(a)['length'];a.editor_action('copy');assert state(a)['length']==before
  assert state(a)['clipboard_count']==1
  key(a,'X' if state(a)['shift'] else 'x');assert not state(a)['selected']
  before=state(a);hold(a,940,185);popup=state(a)
  assert popup['undo_menu'] and popup['text_bounds']==before['text_bounds'] and popup['text_scroll']==before['text_scroll']
  x1,y1,x2,y2=popup['undo_popup'];assert (x2-x1+1,y2-y1+1)==(188,52) and 'paste' in popup['actions']
  shot(a,'undo');a.editor_action('undo');assert state(a)['selected']
  a.tap(930,236);assert state(a)['clipboard_open'];shot(a,'clipboard');a.tap(700,98);assert not state(a)['clipboard_open']
  # A nonselected caret can paste directly, and the inserted text remains undoable.
  seed(a,'');before=state(a);hold(a,940,185);popup=state(a)
  assert not popup['selected'] and 'paste' in popup['actions'] and popup['text_bounds']==before['text_bounds']
  shot(a,'undo-paste');a.editor_action('paste');assert export_text(a)=='alpha' and not state(a)['undo_menu']
  hold(a,940,185);a.editor_action('undo');assert state(a)['length']==0
  # OCR IDs, capture de-duplication and multi-line sequence, with text never in diagnostics.
  seed(a,'');ocr=a.receive('ocr.start')
  while ocr['requestId']!=state(a)['ocr_request']:ocr=a.receive('ocr.start')
  scan(a,ocr,1,'单行');scan(a,ocr,1,'重复');assert state(a)['length']==2
  a.editor_action('ocr-mode');multi=a.receive('ocr.start')
  while multi['requestId']!=state(a)['ocr_request']:multi=a.receive('ocr.start')
  scan(a,ocr,2,'旧结果');assert state(a)['length']==2
  scan(a,multi,1,'第一行');scan(a,multi,2,'第二行');assert export_text(a)=='单行第一行\n第二行'
  shot(a,'ocr-multi')
  # OCR arriving during text drag waits until release.
  current=a.receive('ocr.start')
  while current['requestId']!=state(a)['ocr_request']:current=a.receive('ocr.start')
  before=state(a)['length'];a.send('touch',x=640,y=96,pressed=True);scan(a,current,1,'延后');scan(a,current,1,'重复');assert state(a)['length']==before
  a.send('touch',x=640,y=96,pressed=False);t.wait_for(lambda:state(a)['length']==before+3)
  # Cancelled official handoff preserves composition; confirm completes exactly once.
  key(a,'language') if not state(a)['chinese'] else None
  key(a,'n');key(a,'i');before=state(a);r=official(a);assert 'ni' in r['text']
  assert state(a)['preedit_length']==before['preedit_length'] and state(a)['cursor']==before['cursor']
  long='中文😀 English\n'+('长文本保持可滚动。\n'*18);r=official(a,True,long)
  assert not state(a)['active'] and a.diag()['draft_length']==len(long)
  a.send('keyboard.result',requestId=r['requestId'],confirmed=True,text='duplicate');assert a.diag()['draft_length']==len(long)
  a.tap(500,232);shot(a,'long-text')
  a.send('touch',x=640,y=96,pressed=True);time.sleep(.5);a.send('touch',x=700,y=0,pressed=True)
  t.wait_for(lambda:state(a)['magnifier'] and state(a)['text_scroll']==0)
  a.send('touch',x=300,y=265,pressed=True)
  t.wait_for(lambda:state(a)['cursor']==state(a)['length'])
  end_scroll=state(a)['text_scroll'];time.sleep(.25);assert state(a)['text_scroll']==end_scroll
  shot(a,'caret-magnifier-long')
  a.send('visibility',visible=False);time.sleep(.1)
  assert not state(a)['magnifier'] and not state(a)['cursor_drag']
  a.send('visibility',visible=True);time.sleep(.1)
  # A scroll between two nearby taps must break the double-tap sequence.
  for x,y,pressed in [(624,93,True),(624,93,False),(624,93,True),(624,155,True),(624,155,False),(624,93,True),(624,93,False)]:
   a.send('touch',x=x,y=y,pressed=pressed);time.sleep(.015)
  assert not state(a)['selected']
  scroll=state(a)['text_scroll'];official(a);assert state(a)['text_scroll']==scroll
  k=next(k for k in state(a)['keys'] if k['key']=='delete')
  a.send('touch',x=k['x'],y=k['y'],pressed=True);time.sleep(.7);a.send('visibility',visible=False);time.sleep(.1)
  n=state(a)['length'];time.sleep(.3);assert state(a)['length']==n and not state(a)['ocr_active']
  a.send('visibility',visible=True);time.sleep(.1);a.tap(630,236);assert a.diag()['draft_length']==len(long)
  a.open_settings();a.setting("models");a.tap(790,30);a.tap(250,120)
  assert state(a)['secret'] and not state(a)['chinese'] and not state(a)['multiline'];key(a,'a');shot(a,'secret')
  r=official(a);assert r['text']=='a' and r['inputType']=='EnUSOnly' and not r['multiline'];a.tap(630,236)
 finally:a.close()
print('Native keyboard modes, selection, clipboard, OCR, official handoff and lifecycle passed')
