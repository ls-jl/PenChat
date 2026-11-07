#!/usr/bin/env python3
"""Rapid physical-touch packets with a real dictionary delayed off the UI thread."""
import json,os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture
t.port=1
ROOT=t.ROOT
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
OUT=ROOT/'.work/key-lag';OUT.mkdir(parents=True,exist_ok=True)
def state(a):return a.diag()['editor']
def keys(a):return {k['key']:(k['x'],k['y']) for k in state(a)['keys']}
def burst(a,text,positions,interval=.002):
    for key in text:
        x,y=positions[key]
        a.send('touch',x=x,y=y,pressed=True)
        a.send('touch',x=x,y=y,pressed=False)
        if interval:time.sleep(interval)
def official(a):
    a.editor_action('official');r=a.receive('keyboard.open')
    a.send('keyboard.result',requestId=r['requestId'],confirmed=False,text='')
    t.wait_for(lambda:state(a)['active'] and not a.diag()['keyboard'])
    return r['text']
def layout(a,name):
    if state(a)['layout']==name:return
    # Direct test-only input action still goes through the native UI event queue.
    if name=='en26':
        p=keys(a);burst(a,['language'],p,0)
        t.wait_for(lambda:state(a)['layout']=='en26')
    else:raise AssertionError(name)
def scenario(width,height):
    old=os.environ.get('PENCHAT_TEST_PINYIN_DELAY_MS');os.environ['PENCHAT_TEST_PINYIN_DELAY_MS']='300'
    try:
        with tempfile.TemporaryDirectory(prefix='penchat-fast-keys-') as directory:
            a=t.Native(BINARY,pathlib.Path(directory),official_adapter=False,screen={'width':width,'height':height})
            try:
                s=a.diag();a.tap(250 if height>width else 500,s['toolbar_y']+30 if height>width else 232)
                t.wait_for(lambda:bool(state(a)['keys']));p=keys(a)
                # Spaces arriving before dictionary results must not disappear;
                # later words must stay in order rather than append to the first.
                before=state(a);chinese_baseline=before["build_count"];began=time.monotonic()
                sequence=list('nihao')+['space']+list('zhongguo')+['space']+list('shijie')+['space']
                burst(a,sequence,p)
                queued=state(a);assert queued['deferred_keys']>0 and queued['select_pending'],queued
                capture(a,OUT/f'pending-{width}.png')
                t.wait_for(lambda:state(a)['length']==6 and not state(a)['preedit_length'] and not state(a)['deferred_keys'],timeout=12)
                chinese=state(a);assert official(a)=='你好中国世界'
                assert chinese['build_count']-before['build_count']<=8,(before['build_count'],chinese['build_count'])
                # A query completion while a key is held must not replace keys.
                burst(a,list('ni'),p)
                x,y=p['a'];a.send('touch',x=x,y=y,pressed=True)
                held=state(a);time.sleep(.7);assert state(a)['build_count']==held['build_count']
                a.send('touch',x=x,y=y,pressed=False)
                t.wait_for(lambda:state(a)['preedit_length']==3)
                burst(a,['enter'],p);t.wait_for(lambda:state(a)['preedit_length']==0)
                assert official(a)=='你好中国世界nia'
                layout(a,'en26');p=keys(a);before=state(a)
                text='qwertyuiopasdfghjklzxcvbnm'*8
                began_en=time.monotonic();burst(a,list(text),p,0)
                t.wait_for(lambda:state(a)['length']==9+len(text));elapsed_en=time.monotonic()-began_en
                english=state(a);assert official(a)=='你好中国世界nia'+text
                assert english['build_count']==before['build_count'],(before,english)
                assert english['refresh_count']>before['refresh_count']
                # No raw text is written to metrics/diagnostics.
                result={'size':[width,height],'chinese_keys':len(sequence),'chinese_builds':chinese['build_count']-chinese_baseline,
                        'english_keys':len(text),'english_ms':round(elapsed_en*1000,1),'english_builds':english['build_count']-before['build_count'],
                        'english_refreshes':english['refresh_count']-before['refresh_count'],'injected_query_delay_ms':300}
                # Held delete crosses from composition into committed text without
                # postponing the visible text update until finger release.
                burst(a,['language'],p);t.wait_for(lambda:state(a)['layout']=='zh26');p=keys(a)
                burst(a,list('ni'),p);x,y=p['delete']
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.95)
                held=state(a);assert not held['preedit_length'] and held['length']<9+len(text)
                assert held['painted_length']==held['length']
                a.send('touch',x=x,y=y,pressed=False)
                print(json.dumps(result),flush=True);return result
            finally:a.close()
    finally:
        if old is None:os.environ.pop('PENCHAT_TEST_PINYIN_DELAY_MS',None)
        else:os.environ['PENCHAT_TEST_PINYIN_DELAY_MS']=old
def queued_lifecycle():
    old=os.environ.get('PENCHAT_TEST_PINYIN_DELAY_MS');os.environ['PENCHAT_TEST_PINYIN_DELAY_MS']='300'
    try:
        with tempfile.TemporaryDirectory(prefix='penchat-queued-keys-') as directory:
            a=t.Native(BINARY,pathlib.Path(directory),official_adapter=False)
            try:
                a.tap(500,232);p=keys(a)
                burst(a,list('nihao')+['space']+list('zhongguox')+['delete','space'],p)
                t.wait_for(lambda:state(a)['deferred_keys']>0)
                a.send('visibility',visible=False);time.sleep(.1);a.send('visibility',visible=True)
                t.wait_for(lambda:state(a)['length']==4 and not state(a)['preedit_length'] and not state(a)['deferred_keys'],timeout=12)
                assert official(a)=='你好中国'
                burst(a,list('shijie')+['space'],p)
                t.wait_for(lambda:state(a)['select_pending'])
                a.editor_action('cancel');t.wait_for(lambda:not state(a)['active'])
                assert a.diag()['draft_length']==0
                a.tap(500,232);assert state(a)['length']==0 and not state(a)['deferred_keys'] and not state(a)['select_pending']
            finally:a.close()
    finally:
        if old is None:os.environ.pop('PENCHAT_TEST_PINYIN_DELAY_MS',None)
        else:os.environ['PENCHAT_TEST_PINYIN_DELAY_MS']=old
    print('Queued word selection: delete/order, background resume and cancel isolation passed.',flush=True)

if __name__=='__main__':
    results=[scenario(960,266),scenario(568,1210)];queued_lifecycle()
    (OUT/'throughput.json').write_text(json.dumps(results,indent=2)+'\n')
