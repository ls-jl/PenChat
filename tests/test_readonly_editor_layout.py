#!/usr/bin/env python3
"""Rounded read-only message selection keeps first text below its action row."""
import json,os,pathlib,tempfile
import test_lvgl_app as t
from test_message_edit import fixture,menu,action,shot

BINARY=t.ROOT/'build/host/penchat-lvgl'
t.port=1

def run(theme,scale,width=568,height=1210):
  with tempfile.TemporaryDirectory(prefix='penchat-readonly-layout-') as folder:
    p=pathlib.Path(folder);fixture(p,1)
    import sqlite3
    with sqlite3.connect(p/'penchat.db') as db:
      db.execute('insert or replace into app_settings values(?,?,1)',('theme_mode',json.dumps(theme)))
    a=t.Native(BINARY,p,official_adapter=False,screen={'width':width,'height':height,'direction':0},ui_scale_percent=scale)
    try:
      menu(a);action(a,'select');t.wait_for(lambda:a.diag()['editor']['selection_only'])
      state=a.diag();e=state['editor'];text=e['text_bounds']
      assert not e['keys'] and not e['ocr_active']
      c=e['coordinate_space']
      def editor_button(key):
        current=a.diag()['editor'];b=current['actions'][key]
        a.tap(round((b[0]+b[2])*width/(2*c['width'])),round((b[1]+b[3])*height/(2*c['height'])))
      header_bottom=max(e['actions'][key][3] for key in ('cancel','all','copy'))
      assert text[1]>header_bottom,(text,e['actions'])
      assert text[3]<c['height']
      if height>width:assert state['screen_safety']['rounded'] and text[3]<height-state['safe_insets'][3]+1
      x=text[0]+max(8,round(12*scale/100));y=text[1]+e['line_height']//2+max(3,round(6*scale/100))
      x,y=round(x*width/c['width']),round(y*height/c['height'])
      a.tap(x,y);a.tap(x,y);t.wait_for(lambda:a.diag()['editor']['selected'])
      e=a.diag()['editor'];assert 0<e['selection_end']-e['selection_start']<e['length']
      if height>width:assert e['selection_handles'][0]['shape'][1]>header_bottom
      shot(a,f'readonly-{width}-{theme}-{scale}')
      editor_button('copy');t.wait_for(lambda:a.diag()['editor']['clipboard_count']>0)
      editor_button('cancel');t.wait_for(lambda:not a.diag()['editor']['active'])
    finally:a.close()
  print('Read-only header and first-line copy:',width,height,theme,scale,'passed',flush=True)

if __name__=='__main__':
  previous=os.environ.get('PENCHAT_TEST_Y07_ROUNDED')
  os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
  try:
    for theme in ('dark','light'):
      for scale in (75,100,125,150):run(theme,scale)
    os.environ['PENCHAT_TEST_Y07_ROUNDED']='0'
    for width,height in ((960,266),(640,260)):
      for theme in ('dark','light'):
        for scale in (75,100,150):run(theme,scale,width,height)
  finally:
    if previous is None:os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
    else:os.environ['PENCHAT_TEST_Y07_ROUNDED']=previous
