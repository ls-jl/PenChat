"""Physical-touch catalog acceptance, using the shared data isolation harness."""
import json, shutil, sqlite3, sys, time


def prepare(root, folder):
    sys.path.insert(0, str(root/'tests'))
    from test_reasoning_ui import seed
    if folder.exists(): shutil.rmtree(folder)
    folder.mkdir(); seed(folder, 1)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute("update model_configs set name='gpt-5',display_name='gpt-5' where id='a'")
    return folder/'penchat.db'


def run(ctx):
    read, tap, wait, capture = (ctx[k] for k in ('state','tap','wait','capture'))
    width, height = ctx['W'], ctx['H']
    def click(b): tap((b[0]+b[2])//2, (b[1]+b[3])//2)
    def stroke(x, y, ex, ey):
        # --drag-hold intentionally holds 450ms BEFORE moving. That is a real
        # long press, not a swipe. Use the helper's ordinary 150ms press here.
        sx,sy=ctx['raw'](x,y);tx,ty=ctx['raw'](ex,ey)
        ctx['shell']('PENCHAT_TOUCH_DEVICE=/dev/input/'+ctx['event']+
                     ' /tmp/penchat-display-tap '+f'{sx} {sy} {tx} {ty} 25'+' 2>/dev/null')
        time.sleep(.25)
        wait(lambda:not read()['page']['scrolling'])
    def swipe(delta):
        y=height-50 if delta<0 else 75
        stroke(width//2,y,width//2,min(height-10,max(10,y+delta)))
    def locate(key):
        for _ in range(35):
            s=read(); b=s['settings']['rows'][key]['bounds']; v=s['page']['viewport']
            if s['page']['scrolling']:
                time.sleep(.12);continue
            if b[1]>=v[1] and b[3]<=v[3]:
                time.sleep(.12);settled=read()
                if not settled['page']['scrolling'] and settled['settings']['rows'][key]['bounds']==b:return b
                continue
            swipe(max(-110,min(110,height//2-(b[1]+b[3])//2)))
        raise AssertionError(key)
    def action(name):
        for _ in range(35):
            s=read(); b=s['page']['actions'][name]; v=s['page']['viewport']
            if b[1]>=v[1] and b[3]<=v[3]: click(b);return
            swipe(110 if b[1]<v[1] else -110)
        raise AssertionError(name)
    def number(text):
        wait(lambda:read()['editor']['active'])
        # This test never invokes the official editor or injects bridge results.
        length=read()['editor']['length']
        for _ in range(length):ctx['key']('delete')
        if read()['editor']['mode']==0:ctx['key']('mode')
        for c in text:ctx['key'](c)
        click(read()['editor']['actions']['done']);wait(lambda:not read()['editor']['active'])
    tap(40,32);tap(width*2//3-70,height-30);assert read()['view']=='settings'
    click(locate('compression_threshold'));number('60');capture('catalog-threshold')
    click(locate('models'));wait(lambda:read()['view']=='provider')
    tap(width-90,125);wait(lambda:read()['view']=='model')
    assert read()['model_catalog']['match']['rule']=='openai:gpt-5:gpt-5'
    for key in ('context_budget','reasoning.levels','input_modalities'):
        b=locate('model_'+key);tap(b[2]+31,(b[1]+b[3])//2)
    s=read();assert s['model_parameters']['context']==400000
    assert s['model_parameters']['reasoning']['levels']['minimal']=='minimal'
    assert s['model_parameters']['vision'] is True;capture('catalog-filled-model')
    click(locate('model_context_budget'));number('22000');action('保存')
    wait(lambda:read()['view']=='provider');tap(width-90,125);wait(lambda:read()['view']=='model')
    assert read()['model_parameters']['context']==22000
    capture('catalog-manual-override');tap(40,32);wait(lambda:read()['view']=='provider')
    tap(width-90,76);wait(lambda:bool(read()['provider_protocol']))
    click(locate('model_adapter'));wait(lambda:read()['modal']);capture('catalog-adapter-list')
    # Select automatic. Lower entries are separately covered by real host UI
    # and protocol fixtures; scrolling here verifies the 44px rows on the board.
    stroke(width//2,height-90,width//2,70);capture('catalog-adapter-scroll')
    tap(width-70,height-44);wait(lambda:not read()['modal']);action('保存')
    wait(lambda:not read()['provider_protocol'])
    tap(40,32);wait(lambda:read()['view']=='settings')
    capture('catalog-settings-restored')
