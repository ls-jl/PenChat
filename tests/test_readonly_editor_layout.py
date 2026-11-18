#!/usr/bin/env python3
"""Rounded read-only message selection keeps first text below its action row."""
import json,os,pathlib,tempfile
import test_lvgl_app as t
from test_message_edit import fixture,menu,action,button,shot

BINARY=t.ROOT/'build/host/penchat-lvgl'
t.port=1

def run(theme,scale):
  with tempfile.TemporaryDirectory(prefix='penchat-readonly-layout-') as folder:
    p=pathlib.Path(folder);fixture(p,1)
    import sqlite3
    with sqlite3.connect(p/'penchat.db') as db:
      db.execute('insert or replace into app_settings values(?,?,1)',('theme_mode',json.dumps(theme)))
    a=t.Native(BINARY,p,official_adapter=False,screen={'width':568,'height':1210,'direction':0},ui_scale_percent=scale)
    try:
      menu(a);action(a,'select');t.wait_for(lambda:a.diag()['editor']['selection_only'])
      state=a.diag();e=state['editor'];text=e['text_bounds']
      assert state['screen_safety']['rounded'] and not e['keys'] and not e['ocr_active']
      header_bottom=max(e['actions'][key][3] for key in ('cancel','all','copy'))
      assert text[1]>header_bottom,(text,e['actions'])
      assert text[3]<state['height']-state['safe_insets'][3]+1
      x=text[0]+max(8,round(12*scale/100));y=text[1]+e['line_height']//2+max(3,round(6*scale/100))
      a.tap(x,y);a.tap(x,y);t.wait_for(lambda:a.diag()['editor']['selected'])
      e=a.diag()['editor'];assert 0<e['selection_end']-e['selection_start']<e['length']
      assert e['selection_handles'][0]['shape'][1]>header_bottom
      shot(a,f'readonly-rounded-{theme}-{scale}')
      button(a,a.diag()['editor']['actions']['copy']);t.wait_for(lambda:a.diag()['editor']['clipboard_count']>0)
      button(a,a.diag()['editor']['actions']['cancel']);t.wait_for(lambda:not a.diag()['editor']['active'])
    finally:a.close()
  print('Read-only rounded header and first-line copy:',theme,scale,'passed',flush=True)

if __name__=='__main__':
  previous=os.environ.get('PENCHAT_TEST_Y07_ROUNDED')
  os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
  try:
    for theme in ('dark','light'):
      for scale in (75,100,125,150):run(theme,scale)
  finally:
    if previous is None:os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
    else:os.environ['PENCHAT_TEST_Y07_ROUNDED']=previous
