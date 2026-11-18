"""Headless UI, atomic archive and transfer checks for the new checkpoint format."""
import http.server
import pathlib
import sqlite3
import tempfile
import threading

import test_context as context
from test_lvgl_app import Native, ROOT


def run():
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), context.Server)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        for protocol in ['responses', 'chat']:
            with tempfile.TemporaryDirectory() as temp:
                directory = pathlib.Path(temp)
                context.fixture(directory, server.server_port, rounds=40, budget=16384,
                                compression_budget=8192, api=protocol, image=True)
                context.Server.requests = []
                app = Native(ROOT / 'build/host/penchat-lvgl', directory, seed_model=False)
                try:
                    context.manual(app)
                    context.wait_idle(app)
                    assert not app.diag()['modal'], app.diag()
                    checkpoint = context.checkpoint(directory)
                    assert checkpoint['version'] == 2 and checkpoint['retained']
                    assert checkpoint['archive_id']
                    assert context.Server.requests
                    assert 'data:image/' in str(context.Server.requests)
                    with sqlite3.connect(directory / 'penchat.db') as database:
                        assert database.execute("select count(*) from chat_messages where session_id='s'").fetchone()[0] == 80
                        assert database.execute("select count(*) from chat_sessions where status='archived'").fetchone()[0] == 1
                    context.transfer_roundtrip(directory)
                    app.to_chat()
                    context.Server.requests = []
                    app.edit('FROZEN_PENDING_INPUT')
                    app.tap(908, 232)
                    context.wait_idle(app)
                    assert not app.diag()['modal'], app.diag()
                    assert 'FROZEN_PENDING_INPUT' in str(context.Server.requests[-1])
                    with sqlite3.connect(directory / 'penchat.db') as database:
                        assert database.execute("select count(*) from chat_messages where session_id='s' and output_text='FROZEN_PENDING_INPUT'").fetchone()[0] == 1
                    print(protocol, 'native UI, send preparation, full archive and v2 transfer passed')
                finally:
                    app.close()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            context.fixture(directory, server.server_port, rounds=1, budget=1024, compression_budget=16384)
            import json
            with sqlite3.connect(directory / 'penchat.db') as database:
                database.execute("update chat_messages set output_text='tiny'")
                metadata = json.loads(database.execute("select metadata_json from chat_sessions where id='s'").fetchone()[0])
                metadata['chat_preferences']['web_enabled'] = False
                database.execute("update chat_sessions set metadata_json=? where id='s'", (json.dumps(metadata),))
                database.execute("insert or replace into app_settings values('context_compression_percent','10',1)")
            context.Server.requests = []
            app = Native(ROOT / 'build/host/penchat-lvgl', directory, seed_model=False)
            try:
                app.edit('NO_GAIN_PENDING')
                app.tap(908, 232)
                context.wait_idle(app)
                assert not app.diag()['modal'], app.diag()
                assert not context.checkpoint(directory)
                assert 'NO_GAIN_PENDING' in str(context.Server.requests[-1])
                with sqlite3.connect(directory / 'penchat.db') as database:
                    assert database.execute("select count(*) from chat_sessions where status='archived'").fetchone()[0] == 0
                    assert database.execute("select count(*) from chat_messages where output_text='NO_GAIN_PENDING'").fetchone()[0] == 1
                print('no-benefit auto compression retained context and sent normally')
            finally:
                app.close()
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    run()
