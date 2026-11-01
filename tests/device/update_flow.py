"""Fixed production source/manual UI, using the harness's isolated data."""
import json,time

def run(g):
 state,tap,drag,W,H=[g[k] for k in ('state','tap','drag','W','H')];wait=g['wait'];capture=g['capture']
 tap(40,32);wait(lambda:state()['navigation_open']);tap(int(W*2/3)-140,H-30);wait(lambda:state()['view']=='settings')
 from pathlib import Path
 import sys
 sys.path.insert(0,str(g['ROOT']/'tests'))
 from settings_ui import activate
 def scroll(delta):
  viewport=state()['settings']['viewport'];x=viewport[0]+4;y=H-40 if delta<0 else 40
  drag(x,y,x,min(H-12,max(12,y+delta)))
 activate(state,tap,scroll,'update');wait(lambda:state()['view']=='update')
 wait(lambda:state()['update']['status'] in ('available','current','error'),timeout=45)
 capture('update-check');s=state();assert s['update']['current']==g['APP']['version']
 assert not s['generating'] and not s['storage']['unsaved'];assert 'url' not in s['update']
 # Actual updates are tested under a separate AppID. Never install a CDN
 # release into the user's app from the isolated-data screen fixture.
 tap(40,32);wait(lambda:state()['view']=='settings');assert state()['scroll_y']>0
 capture('update-settings-return')
 g['result']['update']={'version':g['APP']['version'],'status':s['update']['status'],'manual_check':True,'settings_scroll_restored':True,'production_source':True}
