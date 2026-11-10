#!/usr/bin/env python3
"""Focused headless LVGL assistant menu/selection; no device or API requests."""
import pathlib
import sqlite3
import tempfile
import test_lvgl_app as ui
from test_message_edit import fixture, menu, action, shot, rows

BINARY=ui.ROOT/'build/host/penchat-lvgl'

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-assistant-menu-') as tmp:
        folder=pathlib.Path(tmp);fixture(folder,1)
        # Keep one short exchange visible across all layout classes.
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute("DELETE FROM message_parts WHERE message_id NOT IN ('u1','a1')")
            db.execute('DELETE FROM message_assets')
            db.execute("DELETE FROM chat_messages WHERE id NOT IN ('u1','a1')")
        before=rows(folder)
        app=ui.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            menu(app,'u1')
            assert set(app.diag()['message_interactions']['actions'])=={'copy','select','edit'}
            action(app,'copy')
            menu(app,'a1')
            actions=app.diag()['message_interactions']['actions']
            assert set(actions)=={'copy','select'},actions
            assert all(b[3]-b[1]+1>=44 for b in actions.values())
            assert len({b[1] for b in actions.values()})==2
            shot(app,str(w)+'-assistant-menu')
            action(app,'copy')
            assert app.diag()['editor']['clipboard_count']==2
            menu(app,'a1');action(app,'select')
            ui.wait_for(lambda:app.diag()['editor']['selection_only'])
            assert app.diag()['editor']['length']==len('Prefix answer')
            assert not app.diag()['editor']['keys']
            shot(app,str(w)+'-assistant-select')
            assert rows(folder)==before
        finally:app.close()
        print('Assistant menu and selection:',w,h,'passed')

if __name__=='__main__':
    for size in [(960,266),(640,360),(568,1210)]:run(*size)
