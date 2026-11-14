#!/usr/bin/env python3
"""Actual LVGL: usable inner edge starts retain native Back arbitration."""
import os
import pathlib
import tempfile
import time
import test_lvgl_app as t

BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', t.ROOT/'build/host/penchat-lvgl'))


def move(app, x, y, down=True, **extra):
    app.send('touch', x=x, y=y, pressed=down, **extra)
    time.sleep(.025)


def run(width, height):
    with tempfile.TemporaryDirectory(prefix='penchat-back-hit-') as directory:
        app = t.Native(BINARY, pathlib.Path(directory), seed_model=False,
                       official_adapter=False,
                       screen={'width': width, 'height': height, 'direction': 0})
        try:
            app.open_settings()
            geometry = app.diag()['back_navigation']
            edge, slop, distance = (geometry[key] for key in ('edge', 'slop', 'distance'))
            assert edge == 44, geometry
            assert slop == max(8, int(width*.0125+.5)), geometry
            assert distance == max(44, int(width*.10+.5)), geometry
            y = height//2
            # Both the newly available inner start and the final inner pixel
            # claim and return. This does not simulate the system outer preview.
            for start in (34, edge-1):
                move(app, start, y)
                move(app, start+slop+1, y)
                assert app.diag()['back_navigation']['claimed']
                move(app, start+distance, y, False)
                assert app.diag()['view'] == 'chat'
                app.open_settings()
            for end, cancel in ((34+distance-1, False), (35, False),
                                (34+distance, True)):
                move(app, 34, y)
                move(app, 34+slop+1, y)
                assert app.diag()['back_navigation']['claimed']
                move(app, end, y, False, cancelled=cancel)
                assert app.diag()['view'] == 'settings'
            # A start beyond the target cannot claim, even with enough travel.
            move(app, edge+1, y)
            move(app, edge+distance+1, y)
            assert not app.diag()['back_navigation']['claimed']
            move(app, edge+distance+1, y, False)
            assert app.diag()['view'] == 'settings'
            # Focus loss cancels the held sequence; restoration cannot complete it.
            app.send('input.focus', sequence=1, active=True)
            move(app, 34, y)
            move(app, 34+distance, y)
            assert app.diag()['back_navigation']['claimed']
            app.send('input.focus', sequence=2, active=False)
            app.send('input.focus', sequence=3, active=True)
            move(app, 34+distance, y, False)
            assert app.diag()['view'] == 'settings'
            assert not app.diag()['back_navigation']['claimed']
        finally:
            app.close()


if __name__ == '__main__':
    for size in ((960, 266), (640, 260), (568, 1210)):
        run(*size)
    print('Native Back inner hit area: claim, completion, short/retreat/cancel, outside exclusion and focus cancellation passed at three geometries.')
