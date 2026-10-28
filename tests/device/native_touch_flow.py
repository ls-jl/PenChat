"""Run only inside display_config's isolated, explicit-device acceptance."""
import subprocess,time

def run(c):
    state,tap,wait=c['state'],c['tap'],c['wait'];W,H=c['W'],c['H']
    s=state();assert s['touch_input']['source']=='native-evdev' and s['touch_input']['active']
    assert s['touch_input']['device']=='/dev/input/'+c['event'] and not s['touch_input']['exclusive']
    tool=c['OUT']/'touch-burst'
    subprocess.run([c['compiler'],'-std=c++11','-Os',str(c['ROOT']/'tests/device/touch_burst.cpp'),'-o',str(tool)],check=True)
    subprocess.run(['adb','-s',c['SERIAL'],'push',str(tool),'/tmp/penchat-touch-burst-test'],check=True,capture_output=True)
    c['shell']('chmod 755 /tmp/penchat-touch-burst-test')
    def cancel():
        b=state()['editor']['actions']['cancel'];tap((b[0]+b[2])//2,(b[1]+b[3])//2);wait(lambda:not state()['editor']['active'])
    def english():
        tap(W//2,H-34);wait(lambda:state()['editor']['active']);s=state()
        if s['editor']['layout']!='en26':
            k=next(k for k in s['editor']['keys'] if k['key']=='language');tap(k['x'],k['y'])
        assert state()['editor']['layout']=='en26'
    runs=[]
    try:
        for pair in [('c','m'),('a','l')]:
            english();keys={k['key']:k for k in state()['editor']['keys']};split=state()['editor']['keyboard_split']
            xy=[(3 if k=='a' else split-2 if k=='l' else keys[k]['x'],keys[k]['y']) for k in pair]
            args=' '.join(str(v) for x,y in xy for v in c['raw'](x,y))
            for ms in [80,20,5,1,0]:
                before=state()['editor']['length'];count=40
                response=c['shell']('/tmp/penchat-touch-burst-test /dev/input/'+c['event']+' '+args+f' {ms} {ms} {count} 2; echo status=$?')
                assert 'status=0' in response,response
                wait(lambda:state()['editor']['length']==before+count)
                assert state()['editor']['length']==before+count
                runs.append({'keys':list(pair),'slots':2,'down_ms':ms,'gap_ms':ms,'sent':count,'received':count})
                print('Native touch burst:',runs[-1],flush=True)
            c['capture']('native-touch-'+''.join(pair));cancel()
        # Background closes the fd; a touch while hidden cannot become a foreground click.
        english();p=c['pid']();c['shell']('miniapp_cli start 8080222437664451');wait(lambda:not state()['visible'])
        assert not state()['touch_input']['active']
        c['shell']('miniapp_cli start '+c['APP']['appid']);wait(lambda:state()['visible'] and state()['touch_input']['active'])
        assert c['pid']()==p and state()['editor']['length']==0
        cancel()
        # The root exit now uses the same native frames as contextual back.
        c['side_back']();wait(lambda:not c['pid']() or not state()['visible']);c['start_app']()
        assert state()['touch_input']['active']
        c['result']['native_touch']={'runs':runs,'background_closes_fd':True,'native_root_exit':True}
    finally:
        c['shell']('rm -f /tmp/penchat-touch-burst-test')
