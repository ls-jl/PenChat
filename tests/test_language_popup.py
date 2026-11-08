#!/usr/bin/env python3
"""The language menu appears while held and never consumes its opener as a choice."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/language-popup';OUT.mkdir(parents=True,exist_ok=True)
for w,h in [(960,266),(568,1210)]:
 for native in [False,True]:
  with tempfile.TemporaryDirectory(prefix='penchat-language-popup-') as folder:
   a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h})
   try:
    def e():return a.diag()['editor']
    def frame(x,y,down=True,cancel=False):
     args={'x':x,'y':y,'pressed':down,'cancelled':cancel}
     if native:args['points']=[{'id':42,'x':x,'y':y}] if down else []
     a.send('touch',**args);time.sleep(.025)
    def globe():return next(k for k in e()['keys'] if k['key']=='language')
    b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    t.wait_for(lambda:e()['active'] and bool(e()['keys']))
    for index in [1,2,0]:
     k=globe();before=e()['layout'];frame(k['x'],k['y']);time.sleep(.55)
     assert not e()['layout_menu']
     t.wait_for(lambda:e()['layout_menu'] and e()['layout_hold'])
     assert e()['layout']==before
     v=e()['layout_popup'];assert v and v[3]<k['bounds'][1] and v[0]<k['x']<v[2]
     for action in ['layout:0','layout:1','layout:2']:
      bounds=e()['actions'][action];assert bounds[3]-bounds[1]+1>=44
     capture(a,OUT/f'{w}-{native}-{before}-held.png')
     # Moving onto an option then releasing the opening contact is not a tap.
     b=e()['actions']['layout:'+str(index)];frame((b[0]+b[2])//2,(b[1]+b[3])//2)
     frame((b[0]+b[2])//2,(b[1]+b[3])//2,False)
     assert e()['layout_menu'] and not e()['layout_hold'] and e()['layout']==before
     a.editor_action('layout:'+str(index));t.wait_for(lambda:not e()['layout_menu'])
     assert e()['layout']==['zh26','en26','zh9'][index]
    # Short taps still cycle; cancelled/slid contacts never open the menu.
    k=globe();before=e()['layout'];a.tap(k['x'],k['y']);assert e()['layout']!=before
    k=globe();before=e()['layout'];frame(k['x'],k['y']);frame(w-2,20);time.sleep(.8)
    assert not e()['layout_menu'];frame(w-2,20,False,True);assert e()['layout']==before
    k=globe();frame(k['x'],k['y']);t.wait_for(lambda:e()['layout_menu'])
    a.send('visibility',visible=False);assert not e()['layout_hold']
    a.send('visibility',visible=True);t.wait_for(lambda:e()['active'])
    frame(k['x'],k['y'],False,True)
    assert e()['layout']==before and e()['length']==0
    print(f'{w}x{h}, native contacts={native}: 750ms held popup, anchored list, swallowed release, selection, short tap, slide/cancel and lifecycle passed.',flush=True)
   finally:a.close()
