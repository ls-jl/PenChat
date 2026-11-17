#!/usr/bin/env python3
"""Keyboard templates: real-process geometry and optional pre-refactor comparison."""
import argparse
import json
import pathlib
import tempfile
import time
import test_lvgl_app as t
from image_gestures import capture

t.port = 1
FIELDS = ('keys', 'text_bounds', 'candidate_y', 'function_y', 'panel_top',
          'keyboard_split', 'keyboard_origin', 'coordinate_space',
          'composition_bounds', 'candidate_bounds', 'keyboard_swapped')


def scenario(binary, output, width, height, split=False, swap=False, scale=100, blink=False):
    name = f'{width}x{height}-' + ('split' if split else 'default') + ('-swap' if swap else '')
    result = {}
    with tempfile.TemporaryDirectory(prefix='penchat-keyboard-design-') as folder:
        app = t.Native(binary, folder, seed_model=False, official_adapter=False,
                       screen={'width': width, 'height': height, 'direction': 0},
                       ui_scale_percent=None if scale==100 else scale)
        try:
            if swap:
                app.open_settings()
                app.setting('keyboard')
                app.setting('wide_keyboard_swap')
                app.to_chat()
            bounds = app.diag()['composer_text_bounds']
            app.tap((bounds[0]+bounds[2])//2, (bounds[1]+bounds[3])//2)
            def state(): return app.diag()['editor']
            t.wait_for(lambda: state()['active'] and bool(state()['keys']))
            if split: app.editor_action('compact-view')
            session = state()['session']
            def point(x, y):
                space = state()['coordinate_space']
                return round(x*width/space['width']), round(y*height/space['height'])
            def key(value):
                item = next(k for k in state()['keys'] if k['key'] == value)
                app.tap(*point(item['x'], item['y']))
            def layout(value):
                globe = next(k for k in state()['keys'] if k['key'] == 'language')
                app.long_press(*point(globe['x'], globe['y']), .56)
                t.wait_for(lambda: state()['layout_menu'])
                app.editor_action('layout:'+str(value))
            def record(label):
                t.wait_for(lambda:not app.diag().get('surface_motion',False))
                time.sleep(.25) # Compare resting layouts, not two different entry-animation frames.
                current = state()
                if scale!=100:
                    assert current['requested_scale']==scale
                    assert min(100,scale)<=current['effective_scale']<=scale
                assert current['keyboard_swapped'] == (swap and not current['compact_full'])
                assert current['session'] == session
                result[label] = {k: current[k] for k in FIELDS}
                for item in current['keys']:
                    x1,y1,x2,y2 = item['hit_bounds']
                    assert x1 <= item['x'] < x2 and y1 <= item['y'] < y2
                capture(app, output/(name+'-'+label+'.png'))
                if blink:
                    time.sleep(.52)
                    capture(app, output/(name+'-'+label+'-blink.png'))
            record('empty')
            for letter in 'nihao': key(letter)
            t.wait_for(lambda: state()['candidate_count'] and not state()['pending'])
            record('composition')
            app.editor_action('compact-view' if state()['compact_full'] else 'expand')
            record('expanded')
            app.editor_action('compact-view' if state()['compact_full'] else 'expand')
            key('enter')
            layout(1)
            record('english')
            key('mode'); record('numbers')
            key('symbols'); record('symbols')
            key('mode'); layout(2)
            for digit in '64426': key('t9:'+digit)
            t.wait_for(lambda: state()['candidate_count'] and not state()['pending'])
            record('nine')
            key('enter')
            app.editor_action('cancel' if 'cancel' in state()['actions'] else 'done')
            assert not app.read_messages()
        finally:
            app.close()
    (output/(name+'.json')).write_text(json.dumps(result, ensure_ascii=False, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=pathlib.Path, default=t.ROOT/'build/host/penchat-lvgl')
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, default=t.ROOT/'.work/keyboard-design')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    fixtures = [(960,266,False,False), (936,280,False,True),
                (640,260,False,False), (720,280,False,False),
                (640,260,True,False), (720,280,True,True),
                (568,1210,False,False), (600,1280,False,False)]
    for fixture in fixtures:
        before = None
        if args.baseline:
            directory = args.output/'baseline'; directory.mkdir(exist_ok=True)
            before = scenario(args.baseline.resolve(), directory, *fixture, blink=True)
        after = scenario(args.binary.resolve(), args.output, *fixture, blink=bool(args.baseline))
        if before is not None:
            for state, snapshot in before.items():
                for field, value in snapshot.items():
                    assert after[state][field] == value, (fixture, state, field, value, after[state][field])
            width,height,split,swap=fixture
            prefix=f'{width}x{height}-'+('split' if split else 'default')+('-swap' if swap else '')
            for state in before:
                name=prefix+'-'+state+'.png'
                old={(directory/name).read_bytes(),(directory/name.replace('.png','-blink.png')).read_bytes()}
                new={(args.output/name).read_bytes(),(args.output/name.replace('.png','-blink.png')).read_bytes()}
                assert old & new, name # Require an exact frame match across actual cursor blink phases.
        print('Keyboard template geometry, modes and session preserved:', fixture, flush=True)
