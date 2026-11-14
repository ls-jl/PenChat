#!/usr/bin/env python3
"""Pixel regression against the reviewed ORANGE font/rendering baseline."""
import hashlib, json, os, pathlib, sqlite3, struct, subprocess, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
manifest = json.loads((ROOT / 'tests/golden/manifest.json').read_text())
font = ROOT / '.deps/chinese.ttf'
assert hashlib.sha256(font.read_bytes()).hexdigest() == manifest['font_sha256'], 'Font changed: review and regenerate the ORANGE screenshot baseline'
with tempfile.TemporaryDirectory(prefix='penchat-render-') as directory:
    image = pathlib.Path(directory) / 'render.png'
    schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
        db.executescript(schema)
        db.execute("insert into app_settings values('onboarding',?,1)",(json.dumps({'version':1,'complete':True,'step':6}),))
    config=pathlib.Path(directory)/'screen.json'
    config.write_text(json.dumps({'screen':{'width':manifest['size'][0],'height':manifest['size'][1],'direction':0}}))
    subprocess.run([str(BINARY), '--headless', '--screen-config',str(config),'--data-dir', directory,
                    '--workspace', str(ROOT), '--font', str(font), '--tex-resources', str(ROOT/'.deps/microtex/res'),
                    '--demo-markdown', str(ROOT/'tests/markdown-demo.md'), '--frames', str(manifest['frames']),
                    '--screenshot', str(image)], check=True)
    content = image.read_bytes()
    assert list(struct.unpack_from('>II', content, 16)) == manifest['size']
    if hashlib.sha256(content).hexdigest() != manifest['image_sha256']:
        output=pathlib.Path(os.environ.get('PENCHAT_RENDER_ARTIFACTS',ROOT/'.work/render-regression'))
        output.mkdir(parents=True,exist_ok=True)
        (output/'current.png').write_bytes(content)
        (output/'reviewed.png').write_bytes((ROOT/'tests/golden/markdown.png').read_bytes())
    assert hashlib.sha256(content).hexdigest() == manifest['image_sha256'], 'Markdown screenshot differs from the reviewed baseline'
print('ORANGE 960x266 Markdown screenshot matches the reviewed pixel baseline.')
