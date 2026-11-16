#!/usr/bin/env python3
"""Actual LVGL entry timing and geometry; baseline uses the same release-to-frame observer."""
import argparse,json,pathlib,tempfile,time,statistics,math
import test_lvgl_app as ui
ui.port=1
p=argparse.ArgumentParser();p.add_argument('--binary',type=pathlib.Path,default=ui.ROOT/'build/host/penchat-lvgl');p.add_argument('--output',type=pathlib.Path,default=ui.ROOT/'.work/resource-residency/keyboard-timing.json');args=p.parse_args()
results=[]
for w,h in [(960,266),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-motion-timing-') as folder:
  a=ui.Native(args.binary,folder,official_adapter=False,screen={'width':w,'height':h})
  samples=[]
  try:
   for attempt in range(6):
    ui.wait_for(lambda:not a.diag()['surface_motion']);s=a.diag();b=s['composer_text_bounds'];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
    a.send('touch',x=x,y=y,pressed=True);time.sleep(.03);start=time.monotonic();a.send('touch',x=x,y=y,pressed=False)
    e=ui.wait_for(lambda:(v if v['editor']['keys'] and v['render_timing']['frames']>s['render_timing']['frames'] else None) if (v:=a.diag()) else None)
    elapsed=(time.monotonic()-start)*1000;samples.append(round(elapsed,2))
    if 'timing' in e['editor']:
     t=e['editor']['timing'];assert t['duration_ms']==(180 if h>w else 120),t
     assert t['animation']>=t['requested']
    ui.wait_for(lambda:not a.diag()['surface_motion'])
    if 'timing' in e['editor']:
     end=a.diag()['editor']['timing'];assert end['entry_finished']-end['animation']>=end['duration_ms']-5,end
    a.editor_action('done');ui.wait_for(lambda:not a.diag()['surface_motion'])
    if 'timing' in e['editor']:
     end=a.diag()['editor']['timing'];assert end['exit_finished']-end['exit_started']>=(180 if h>w else 120)-5,end
   results.append({'geometry':[w,h],'release_to_rendered_editor_ms':samples,'median_ms':statistics.median(samples),'p95_ms':sorted(samples)[math.ceil(.95*len(samples))-1],'max_ms':max(samples),'note':'observer includes bridge/diagnostic round trip; not a hardware frame-latency claim'})
  finally:a.close()
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results),flush=True)
