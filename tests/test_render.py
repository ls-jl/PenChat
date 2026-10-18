#!/usr/bin/env python3
"""Pixel regression against the reviewed ORANGE font/rendering baseline."""
import hashlib, json, pathlib, struct, subprocess, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / 'tests/golden/manifest.json').read_text())
font = ROOT / '.deps/chinese.ttf'
assert hashlib.sha256(font.read_bytes()).hexdigest() == manifest['font_sha256'], 'Font changed: review and regenerate the ORANGE screenshot baseline'
with tempfile.TemporaryDirectory(prefix='penchat-render-') as directory:
    image = pathlib.Path(directory) / 'render.png'
    subprocess.run([str(ROOT/'build/host/penchat-lvgl'), '--headless', '--data-dir', directory,
                    '--workspace', str(ROOT), '--font', str(font), '--tex-resources', str(ROOT/'.deps/microtex/res'),
                    '--demo-markdown', str(ROOT/'tests/markdown-demo.md'), '--frames', str(manifest['frames']),
                    '--screenshot', str(image)], check=True)
    content = image.read_bytes()
    assert list(struct.unpack_from('>II', content, 16)) == manifest['size']
    assert hashlib.sha256(content).hexdigest() == manifest['image_sha256'], 'Markdown screenshot differs from the reviewed baseline'
print('ORANGE 960x266 Markdown screenshot matches the reviewed pixel baseline.')
