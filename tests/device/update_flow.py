"""Fixed production source/manual UI, using the harness's isolated data."""
import json,time

def run(g):
 state,tap,drag,W,H=[g[k] for k in ('state','tap','drag','W','H')];wait=g['wait'];capture=g['capture']
 g['open_settings']()
 from pathlib import Path
 import sys
 sys.path.insert(0,str(g['ROOT']/'tests'))
 from settings_ui import activate
 def scroll(delta):
  g['settings_swipe'](delta)
 activate(state,tap,scroll,'update');wait(lambda:state()['view']=='update')
 wait(lambda:state()['update']['status'] in ('available','current','error'),timeout=45)
 capture('update-check');s=state();assert s['update']['current']==g['APP']['version']
 assert not s['generating'] and not s['storage']['unsaved'];assert 'url' not in s['update']
 # Actual updates are tested under a separate AppID. Never install a CDN
 # release into the user's app from the isolated-data screen fixture.
 g['navigate_back_or_menu']();wait(lambda:state()['view']=='settings');assert state()['scroll_y']>0
 capture('update-settings-return')
 g['result']['update']={'version':g['APP']['version'],'status':s['update']['status'],'manual_check':True,'settings_scroll_restored':True,'production_source':True}
