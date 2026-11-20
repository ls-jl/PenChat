#!/usr/bin/env python3
"""Real LVGL rollback warning, cancellation, handoff identity and draft retention."""
import json,os,pathlib,signal,sqlite3,tempfile,time
import test_lvgl_app as t
from test_update import Node,future_version
ROOT=t.ROOT;OUT=ROOT/'.work/version-rollback';OUT.mkdir(exist_ok=True);(OUT/'tmp').mkdir(exist_ok=True);t.port=1

def action(app,name):
    for _ in range(45):
        state=app.diag();bounds=state['update']['actions'].get(name)
        if not bounds or state['page']['scrolling']:time.sleep(.08);continue
        viewport=state['page']['viewport']
        if name=='rollback_continue' or bounds[1]>=viewport[1] and bounds[3]<=viewport[3]:
            assert 0<=bounds[0]<bounds[2]<state['width'] and 0<=bounds[1]<bounds[3]<state['height'],(name,bounds)
            app.tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2);return bounds
        app.settings_swipe(100 if bounds[1]<viewport[1] else -100);time.sleep(.12)
    raise AssertionError((name,app.diag()))

def shot(app,name):
    image=app.directory/'lvgl-diagnostic.png';image.unlink(missing_ok=True)
    app.process.send_signal(signal.SIGUSR1);t.wait_for(image.exists)
    (OUT/(name+'.png')).write_bytes(image.read_bytes())

def run():
    node=Node();node.release(future_version(),note=b'## Changes\n\nFixture only.');results=[]
    previous=os.environ.get('PENCHAT_TEST_UPDATE_CONFIG')
    try:
        for width,height in [(960,266),(640,260),(568,1210)]:
            for scale in [100,150]:
                for theme in ['dark','light']:
                    with tempfile.TemporaryDirectory(prefix='fixture-',dir=OUT/'tmp') as folder:
                        folder=pathlib.Path(folder)
                        config=folder/'updater.json';config.write_text(json.dumps(dict(root=f'http://localhost:{node.port}/penchat-rk/',nodes=['127.0.0.1'],auto=False)))
                        os.environ['PENCHAT_TEST_UPDATE_CONFIG']=str(config)
                        schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
                        draft='回退页面返回后保留的草稿'
                        with sqlite3.connect(folder/'penchat.db') as db:
                            db.executescript(schema)
                            db.execute("insert into app_settings values('theme_mode',?,1)",(json.dumps(theme),))
                            db.execute("insert into chat_drafts values('new',0,1,?)",(draft,))
                        app=t.Native(ROOT/'build/host/penchat-lvgl',folder,seed_model=False,screen={'width':width,'height':height},ui_scale_percent=scale)
                        try:
                            assert app.diag()['draft_length']==len(draft)
                            app.open_settings();app.setting('update');t.wait_for(lambda:app.diag()['update']['status']=='available')
                            name=f'{width}-{scale}-{theme}'
                            action(app,'rollback');t.wait_for(lambda:app.diag()['modal']);shot(app,name+'-warning')
                            assert not app.diag()['update']['rollback_pending']
                            if width==960 and scale==100:
                                b=app.diag()['update']['actions']['rollback_continue']
                                app.tap((b[0]+b[2])//2+120,(b[1]+b[3])//2)
                                t.wait_for(lambda:not app.diag()['modal'])
                                assert not app.diag()['update']['rollback_pending']
                                assert not any(p.get('type')=='rescue.open' for p in app.pending)
                                action(app,'rollback');t.wait_for(lambda:app.diag()['modal'])
                            action(app,'rollback_continue');event=app.receive('rescue.open')
                            assert event['operation'] and app.diag()['update']['rollback_pending']
                            assert app.process.poll() is None
                            app.send('rescue.open.result',operation='obsolete',ok=False)
                            assert app.diag()['update']['rollback_pending'] and not app.diag()['modal']
                            app.send('rescue.open.result',operation=event['operation'],ok=False)
                            t.wait_for(lambda:app.diag()['modal'] and not app.diag()['update']['rollback_pending'])
                            app.send('update.open');t.wait_for(lambda:app.diag()['view']=='update' and not app.diag()['modal'])
                            assert app.diag()['draft_length']==len(draft);shot(app,name+'-update')
                            results.append(dict(width=width,height=height,scale=scale,theme=theme,passed=True))
                        finally:app.close()
                        with sqlite3.connect(folder/'penchat.db') as db:
                            assert db.execute("select text from chat_drafts where draft_key='new'").fetchone()[0]==draft
                            assert db.execute('select count(*) from chat_sessions').fetchone()[0]==0
        (OUT/'ui-results.json').write_text(json.dumps(results,indent=2)+'\n')
    finally:
        node.close()
        if previous is None:os.environ.pop('PENCHAT_TEST_UPDATE_CONFIG',None)
        else:os.environ['PENCHAT_TEST_UPDATE_CONFIG']=previous
    print('ROLLBACK_WARNING_CANCEL_IDENTITY_RETURN_AND_DRAFT_MATRIX_OK',len(results))
if __name__=='__main__':run()
