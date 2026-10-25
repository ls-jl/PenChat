#!/usr/bin/env python3
"""Real LVGL field filling, per-model preservation, adapters and global threshold."""
import json, os, pathlib, sqlite3, tempfile, time
from test_lvgl_app import Native, ROOT, wait_for
from test_reasoning_ui import seed
from test_anthropic_ui import locate, click, row, enter, shot

BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', ROOT/'build/host/penchat-lvgl'))

def fill(app, key):
    bounds = locate(app, 'model_' + key)
    app.tap(bounds[2] + 31, (bounds[1] + bounds[3]) // 2)

def run(width, height):
    with tempfile.TemporaryDirectory(prefix='penchat-model-catalog-') as directory:
        folder = pathlib.Path(directory); seed(folder, 1)
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute("update model_configs set name='gpt-5',display_name='gpt-5' where id='a'")
            original = json.loads(db.execute("select capabilities_json from model_configs where id='a'").fetchone()[0])
        app = Native(BINARY, folder, screen={'width':width,'height':height,'direction':0})
        try:
            app.open_settings(); app.setting('compression_threshold'); enter(app, '60')
            assert app.diag()['view'] == 'settings'
            with sqlite3.connect(folder/'penchat.db') as db:
                assert json.loads(db.execute("select value_json from app_settings where key='context_compression_percent'").fetchone()[0]) == 60
            shot(app, f'{width}-catalog-threshold')
            app.setting('models'); app.tap(width-90,125)
            assert app.diag()['view']=='model'
            assert app.diag()['model_catalog']['match']['rule']=='openai:gpt-5:gpt-5'
            assert app.diag()['model_parameters']['reasoning']==original['reasoning']
            shot(app, f'{width}-catalog-old-settings')
            fill(app,'context_budget')
            assert app.diag()['model_parameters']['context']==400000
            fill(app,'reasoning.levels')
            assert 'minimal' in app.diag()['model_parameters']['reasoning']['levels']
            fill(app,'input_modalities')
            assert app.diag()['model_parameters']['vision'] is True
            shot(app, f'{width}-catalog-fill-fields')
            row(app,'model_context_budget'); enter(app, '22000')
            app.page_action('保存')
            with sqlite3.connect(folder/'penchat.db') as db:
                saved=json.loads(db.execute("select capabilities_json from model_configs where id='a'").fetchone()[0])
                assert saved['context_budget']==22000 and saved['vision'] is True
                assert saved['reasoning']['levels']['minimal']=='minimal'
                assert 'context_budget' in saved['catalog']['overrides']
                assert db.execute("select count(*) from model_configs where id='a'").fetchone()[0]==1
            app.tap(width-90,76); row(app,'model_adapter')
            shot(app, f'{width}-catalog-adapters')
            # The list scrolls independently inside the dialog; first choice
            # remains automatic rather than treating a proxy as an official host.
            app.tap(300,64)
            assert app.diag()['view']=='provider'
            app.page_action('保存')
        finally: app.close()
        app=Native(BINARY,folder,screen={'width':width,'height':height,'direction':0})
        try:
            app.open_settings(); app.setting('models'); app.tap(width-90,125)
            assert app.diag()['model_parameters']['context']==22000
        finally: app.close()
        if width == 936:
            # Disabling a Provider adapter projects its usable runtime levels;
            # editing context must not persist that projection over raw choices.
            original_reasoning = dict(mode='effort',levels={'none':'disabled','high':'high'},
                                      adapter='deepseek',wire='thinking_effort',disable=True,summary=False)
            with sqlite3.connect(folder/'penchat.db') as db:
                caps=json.loads(db.execute("select capabilities_json from model_configs where id='a'").fetchone()[0])
                caps['reasoning']=original_reasoning
                db.execute("update model_configs set capabilities_json=?,name='deepseek-flash' where id='a'",(json.dumps(caps),))
                db.execute("update providers set metadata_json=?",(json.dumps(dict(api='chat',model_adapter='standard')),))
            app=Native(BINARY,folder,screen={'width':width,'height':height,'direction':0})
            try:
                app.open_settings();app.setting('models');app.tap(width-90,125)
                assert app.diag()['model_parameters']['reasoning']==original_reasoning
                row(app,'model_context_budget');enter(app,'23000');app.page_action('保存')
                with sqlite3.connect(folder/'penchat.db') as db:
                    result=json.loads(db.execute("select capabilities_json from model_configs where id='a'").fetchone()[0])
                    assert result['reasoning']==original_reasoning and result['context_budget']==23000
            finally:app.close()

if __name__=='__main__':
    for size in [(960,266),(936,280)]: run(*size)
    print('Model catalog actual-LVGL fill, override, threshold, adapter and restart checks passed')
