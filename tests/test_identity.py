#!/usr/bin/env python3
"""A fresh installation has no configuration or data from outside its directory."""
import json, pathlib, tempfile, sqlite3, os, signal, time, shutil, subprocess
from test_lvgl_app import Native, ROOT
with tempfile.TemporaryDirectory(prefix='penchat-clean-') as directory:
    app=Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl')),pathlib.Path(directory),seed_model=False)
    try:
        assert app.diag()['summary']['sessions']==0
        with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
            assert db.execute('select count(*) from providers').fetchone()[0]==0
            assert db.execute('select count(*) from model_configs').fetchone()[0]==0
            assert db.execute('select count(*) from chat_messages').fetchone()[0]==0
        app.process.send_signal(signal.SIGUSR1);time.sleep(.2)
        output=ROOT/'.work/identity';output.mkdir(parents=True,exist_ok=True)
        (output/'welcome.png').write_bytes((pathlib.Path(directory)/'lvgl-diagnostic.png').read_bytes())
        app.open_settings();app.process.send_signal(signal.SIGUSR1);time.sleep(.2)
        (output/'settings.png').write_bytes((pathlib.Path(directory)/'lvgl-diagnostic.png').read_bytes())
        from settings_ui import locate
        locate(app.diag,app.settings_swipe,'about')
        app.process.send_signal(signal.SIGUSR1);time.sleep(.2)
        (output/'settings-details.png').write_bytes((pathlib.Path(directory)/'lvgl-diagnostic.png').read_bytes())
        app.setting('about');assert app.diag()['modal']
        app.process.send_signal(signal.SIGUSR1);time.sleep(.2)
        (output/'about.png').write_bytes((pathlib.Path(directory)/'lvgl-diagnostic.png').read_bytes())
    finally:app.close()
print('Fresh PenChat: isolated empty database, no providers/models/messages, settings and welcome rendered.')

# Exercise the real CMake include: patch-only edits must trigger reconfiguration
# without manually invoking cmake -S again or touching the actual project.
with tempfile.TemporaryDirectory(prefix='penchat-build-identity-') as directory:
    root=pathlib.Path(directory);(root/'cmake').mkdir()
    for name in ['Identity.cmake','app_identity.hpp.in']:
        shutil.copyfile(ROOT/'cmake'/name,root/'cmake'/name)
    package=json.loads((ROOT/'package.json').read_text());package['version']='1.0.0'
    (root/'package.json').write_text(json.dumps(package))
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(identity NONE)\nset(ROOT "${CMAKE_CURRENT_SOURCE_DIR}")\ninclude(cmake/Identity.cmake)\n')
    subprocess.run(['cmake','-S',str(root),'-B',str(root/'build')],check=True,capture_output=True)
    header=root/'build/generated/app_identity.hpp'
    assert '#define PENCHAT_VERSION "1.0.0"' in header.read_text()
    time.sleep(1.05)
    package['version']='1.0.1';(root/'package.json').write_text(json.dumps(package))
    subprocess.run(['cmake','--build',str(root/'build')],check=True,capture_output=True)
    assert '#define PENCHAT_VERSION "1.0.1"' in header.read_text()
print('Incremental CMake build: patch-only package version change regenerated native identity.')
