#!/usr/bin/env python3
"""Short touch expiry keeps settled reading selection, never a held contact."""
import pathlib,tempfile,time
import test_lvgl_app as t
from test_input_heartbeat_editor import Heartbeat
from test_text_selection import fixture,ro,press

t.port=1
BINARY=t.ROOT/'build/host/penchat-lvgl'

def run(width,height):
  with tempfile.TemporaryDirectory(prefix='penchat-heartbeat-selection-') as folder:
    p=pathlib.Path(folder);fixture(p)
    a=t.Native(BINARY,p,official_adapter=False,screen={'width':width,'height':height})
    beat=Heartbeat(a)
    try:
      beat.start();a.attachment_action('session_info');t.wait_for(lambda:a.diag()['view']=='session_info' and not a.diag()['session_info_busy'])
      def select():
        region=next(r for r in ro(a)['regions'] if r['key']=='page:session_info')
        b=next(b for b in region['parts'] if b[1]>=a.diag()['safe_insets'][1]+38 and b[3]<height-10)
        a.long_press(b[0]+5,(b[1]+b[3])//2,.7);t.wait_for(lambda:ro(a)['selected'] and ro(a)['menu'])
      def expire():
        beat.pause();time.sleep(.38);t.wait_for(lambda:not a.diag()['input_focused'])
        assert a.diag()['visible'] and a.diag()['window_focused']
      def resume():
        if beat.thread is None:beat.start()
        else:beat.pulse()
        t.wait_for(lambda:a.diag()['input_focused'])
      select();before=ro(a);selected=(before['active'],before['start'],before['end'])
      expire();after=ro(a)
      assert after['selected'] and (after['active'],after['start'],after['end'])==selected,after
      # Stale contacts cannot copy while the lease is closed, even with the
      # settled selection still visible. A fresh contact after renewal can.
      b=after['menu_bounds'];point=(b[0]+(b[2]-b[0])*3//4,(b[1]+b[3])//2)
      a.tap(*point);assert ro(a)['clipboard_count']==0
      resume();a.tap(*point);t.wait_for(lambda:ro(a)['clipboard_count']==1)
      # A true focus handoff clears selection; renewed heartbeats cannot revive it.
      a.send('window.focus',sequence=1,focused=False);t.wait_for(lambda:not a.diag()['window_focused'] and not ro(a)['selected'])
      a.send('window.focus',sequence=2,focused=True);resume();assert not ro(a)['selected']
      select();grip=ro(a)['handle_grips'][-1];press(a,*grip);expire()
      assert not ro(a)['selected'],'An in-flight handle drag must be cancelled'
      press(a,*grip,False);resume();assert not ro(a)['selected'] and ro(a)['clipboard_count']==1
      select();b=ro(a)['menu_bounds'];point=(b[0]+(b[2]-b[0])*3//4,(b[1]+b[3])//2)
      press(a,*point);expire();assert not ro(a)['selected']
      resume();press(a,*point,False);time.sleep(.1)
      assert ro(a)['clipboard_count']==1,'An old menu release must never copy after renewal'
      select();a.send('visibility',visible=False);t.wait_for(lambda:not ro(a)['selected'])
      a.send('visibility',visible=True);resume();assert not ro(a)['selected']
      select();beat.pause();time.sleep(1.6);t.wait_for(lambda:not ro(a)['selected'])
      print('READ_SELECTION_SHORT_LEASE_RETAINS_SETTLED_ONLY',width,height,flush=True)
    finally:beat.pause();a.close()

if __name__=='__main__':
  for size in ((960,266),(640,260),(568,1210)):run(*size)
