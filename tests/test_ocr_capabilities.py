#!/usr/bin/env python3
"""Runtime OCR capability reports drive all keyboard toolbars without device I/O."""
import tempfile
import test_lvgl_app as t
from image_gestures import capture

t.port = 1
BINARY = t.ROOT/'build/host/penchat-lvgl'
OUT = t.ROOT/'.work/ocr-capabilities'
OUT.mkdir(parents=True, exist_ok=True)


def run(width, height, capabilities):
    with tempfile.TemporaryDirectory(prefix='penchat-ocr-capability-') as folder:
        a = t.Native(BINARY, folder, seed_model=False, official_adapter=False,
                     screen={'width': width, 'height': height}, ocr_capabilities=capabilities)
        try:
            def state(): return a.diag()['editor']
            def report(request, **values):
                a.send('ocr.capabilities', requestId=request['requestId'],
                       session=request['session'], **values)
            def current_request():
                request = a.receive('ocr.start')
                while request['requestId'] != state()['ocr_request']:
                    request = a.receive('ocr.start')
                return request
            def assert_toolbar(supported):
                e = state()
                assert e['ocr_multiline_supported'] == supported
                if e['compact_full']:
                    assert ('tools' in e['actions']) == supported
                    assert ('clipboard' in e['actions']) != supported
                else:
                    assert ('ocr-mode' in e['actions']) == supported
            bounds = a.diag()['composer_text_bounds']
            a.tap((bounds[0]+bounds[2])//2, (bounds[1]+bounds[3])//2)
            t.wait_for(lambda: state()['active'] and state()['keys'])
            request = current_request()
            assert request['mode'] == 'single'
            supported = capabilities.get('single') is True and capabilities.get('multi') is True
            assert_toolbar(supported)
            # Unknown/malformed reports must not inherit the platform's multi default.
            if not supported:
                a.send('ocr.capabilities', generation=a.generation-1,
                       requestId=request['requestId'], session=request['session'], single=True, multi=True)
                assert_toolbar(False)
                a.send('ocr.capabilities', requestId='obsolete', session=request['session'], single=True, multi=True)
                assert_toolbar(False)
                a.send('ocr.capabilities', requestId=request['requestId'], session='old-field', single=True, multi=True)
                assert_toolbar(False)
                if state()['compact_full']:
                    a.editor_action('compact-view');assert_toolbar(False)
                    capture(a, OUT/f'{width}-single-split.png')
                    a.editor_action('compact-view')
                capture(a, OUT/f'{width}-single.png')
                a.send('ocr.result', requestId=request['requestId'], session=request['session'],
                       mode='single', capture=1, confirmed=True, text='单行\n文字')
                t.wait_for(lambda: state()['length'] == 5)
                assert not state()['ocr_multi']
                report(request, single=True, multi=True)
                t.wait_for(lambda: state()['ocr_multiline_supported'])
            assert_toolbar(True)
            session = state()['session']
            # A late capability update cannot delete held key objects or lose a key.
            key = next(k for k in state()['keys'] if k['key'] == 'a')
            a.send('touch', x=key['x'], y=key['y'], pressed=True,
                   points=[{'id': 'held', 'x': key['x'], 'y': key['y']}])
            before = state()
            report(request, single=True, multi=False)
            assert not state()['ocr_multiline_supported']
            assert state()['build_count'] == before['build_count']
            a.send('touch', x=key['x'], y=key['y'], pressed=False, points=[])
            t.wait_for(lambda: state()['build_count'] > before['build_count'])
            t.wait_for(lambda: state()['preedit_length'] == 1)
            assert_toolbar(False)
            # A capability refresh must not change the editor session or contents.
            assert state()['session'] == session
            report(request, single=True, multi=True)
            t.wait_for(lambda: state()['ocr_multiline_supported'])
            # Portrait composition deliberately hides all function controls.
            # Commit the held letter before exercising the visible OCR switch.
            assert state()['preedit_length'] == 1
            enter = next(k for k in state()['keys'] if k['key'] == 'enter')
            a.tap(enter['x'], enter['y'])
            t.wait_for(lambda: state()['preedit_length'] == 0)
            committed_length = before['length'] + 1
            assert state()['length'] == committed_length
            if state()['compact_full']: a.editor_action('tools')
            assert 'ocr-mode' in state()['actions']
            capture(a, OUT/f'{width}-multi-available.png')
            a.editor_action('ocr-mode')
            request = current_request()
            assert request['mode'] == 'multi' and state()['ocr_multi']
            a.ocr_cancel_ack = False
            report(request, single=True, multi=False)
            cancel = a.receive('ocr.cancel')
            assert cancel['requestId'] == request['requestId']
            assert a.diag()['ocr_release_pending'] and not state()['ocr_multi']
            assert_toolbar(False)
            a.send('ocr.result', requestId=request['requestId'], session=request['session'],
                   mode='multi', capture=1, confirmed=True, text='stale')
            assert state()['length'] == committed_length
            a.send('ocr.stopped', requestId='obsolete', ok=True)
            assert a.diag()['ocr_release_pending']
            a.send('ocr.stopped', requestId=cancel['requestId'], ok=True)
            a.ocr_cancel_ack = True
            single = current_request()
            assert single['mode'] == 'single' and single['requestId'] != request['requestId']
            report(request, single=True, multi=True)
            assert_toolbar(False)
            assert state()['session'] == session and state()['length'] == committed_length
            assert not a.read_messages()
            print(f'OCR capability {width}x{height} {capabilities}: passed', flush=True)
        finally:
            a.close()


if __name__ == '__main__':
    for width, height in [(960,266), (640,260), (568,1210)]:
        for capabilities in [{'single': True, 'multi': False}, {'single': True, 'multi': True}]:
            run(width, height, capabilities)
    run(720, 280, {})
    run(936, 280, {'single': True, 'multi': 'true'})
    print('OCR capability toolbars: wide/compact/split/portrait, unknown defaults, stale IDs, held keys and cleanup ACK passed.')
