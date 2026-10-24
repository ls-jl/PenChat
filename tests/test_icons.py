#!/usr/bin/env python3
"""Verify the actual LVGL gear pixels, including dimmed small-size rendering."""
import os,pathlib,signal,subprocess,tempfile,time
import test_lvgl_app as t
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/icons';OUT.mkdir(parents=True,exist_ok=True)
for w,h in ((960,266),(936,280)):
    with tempfile.TemporaryDirectory(prefix='penchat-icon-pixels-') as folder:
        p=pathlib.Path(folder);a=t.Native(BINARY,p,seed_model=False,screen={'width':w,'height':h,'direction':0})
        try:
            a.send('icons.test');t.wait_for(lambda:a.diag()['view']=='icons');time.sleep(.1);a.process.send_signal(signal.SIGUSR1)
            shot=p/'lvgl-diagnostic.png';t.wait_for(shot.exists)
            target=OUT/(str(w)+'-gear.png');target.write_bytes(shot.read_bytes())
            for x,y,size,dim in ((554,19,26,0),(557,88,20,1)):
                subprocess.run([str(BINARY.parent/'penchat-icon-pixel-tests'),str(target),str(x),str(y),str(size),str(dim)],check=True)
        finally:a.close()
print('Gear icon: actual rendered normal/small/disabled pixels are symmetric and centered at both device sizes.')
