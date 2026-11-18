#!/usr/bin/env python3
"""Scoped real-touch rate/bottom acceptance with complete private/public restore.

Requires an explicitly supplied development package. Uses only a local fixture;
never restarts Falcon, sends a network request, or touches another app's data.
"""
import argparse
import hashlib
import json
import pathlib
import shlex
import signal
from release_lifecycle import install_termination_handler,defer_cleanup_signals
import sqlite3
import struct
import subprocess
import tempfile
import time
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
APP = json.loads((ROOT/'package.json').read_text())
ALLOWED = ('JDA0700006552216', 'MEB0400008900794', 'MEA2300009007113', '7G12000010905426')


class Device:
    def __init__(self, serial, package):
        assert serial in ALLOWED, 'Device is outside the explicit allowlist'
        self.serial, self.package = serial, package.resolve()
        self.s6 = serial == ALLOWED[0]
        self.target = 'cv1826-s' if self.s6 else 'rk3562'
        self.data = ('/userdisk/miniapp' if self.s6 else '/userdisk/secondary/miniapp')+'/data/mini_app/pkg/'+APP['appid']+'/data'
        self.public = '/userdisk/Favorite/PenChat'
        self.saved = self.data+'.refresh-bottom-saved'
        self.public_saved = self.public+'.refresh-bottom-saved'
        self.helper = '/tmp/penchat-refresh-bottom-tap'
        self.remote_package = '/tmp/penchat-refresh-bottom.amr'
        self.snapshot = '/tmp/penchat-refresh-bottom-snapshot.db'
        self.snapshot_helper = '/tmp/penchat-refresh-bottom-snapshot'
        self.input_state_probe = '/tmp/penchat-refresh-bottom-input-state'
        self.out = ROOT/'.work/refresh-bottom-device'/serial
        self.out.mkdir(parents=True, exist_ok=True)
        self.result = {'serial': serial, 'version': APP['version'], 'checks': []}
        with zipfile.ZipFile(self.package) as archive:
            manifest = json.loads(archive.read('manifest.json'))
            assert (manifest['appid'], manifest['version']) == (APP['appid'], APP['version']), 'Package identity differs from this app'
            binary = archive.read('assets/bin/penchat-lvgl')
            assert binary[:4] == b'\x7fELF' and struct.unpack_from('<H', binary, 18)[0] == (40 if self.s6 else 183), 'Package architecture differs from explicit target'
        self.result['package_sha256'] = hashlib.sha256(self.package.read_bytes()).hexdigest()

    def sh(self, command):
        return subprocess.check_output(['adb', '-s', self.serial, 'shell', command], text=True).replace('\r', '').strip()

    def push(self, source, destination):
        subprocess.run(['adb', '-s', self.serial, 'push', str(source), destination], check=True, capture_output=True)

    def wait(self, fn, seconds=20):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            try:
                result = fn()
            except (ValueError, subprocess.CalledProcessError):
                result = None
            if result:
                return result
            time.sleep(.1)
        raise AssertionError('Device transition timed out')

    def pid(self):
        for pid in self.sh('pidof penchat-lvgl || true').split():
            status = self.sh('cat /proc/'+pid+'/stat 2>/dev/null || true').rsplit(')', 1)[-1].split()
            if status and status[0] not in ('Z', 'X'):
                return pid
        return ''

    def state(self):
        pid = self.pid()
        assert pid, 'Native app is not running'
        self.wait(lambda: int(self.sh("awk '/SigCgt/{print $2}' /proc/"+pid+'/status'), 16)&(1 << 11))
        old = self.sh('cat '+self.data+'/lvgl-diagnostic.json 2>/dev/null || true')
        previous = json.loads(old) if old else {}
        stamp = previous.get('diagnostic_time', -1) if str(previous.get('pid')) == pid else -1
        self.sh('kill -USR2 '+pid)
        def fresh():
            d = json.loads(self.sh('cat '+self.data+'/lvgl-diagnostic.json'))
            return d if str(d.get('pid')) == pid and d.get('diagnostic_time', -1) > stamp else None
        return self.wait(fresh)

    def close(self):
        from release_lifecycle import close_app
        result = close_app(self.serial, APP['appid'], self.sh, self.pid, self.state)
        self.result.setdefault('lifecycle_closes', []).append(result)

    def launch(self):
        self.sh('miniapp_cli start '+APP['appid'])
        self.wait(self.pid)
        def ready():
            d = self.state()
            return d if d['visible'] and d['window_focused'] and d['touch_input']['active'] else None
        self.wait(ready)
        # Foreground touch ownership can settle after Falcon's visibility ACK.
        # Require a continuous quiet interval before injecting the first touch.
        since = time.monotonic()
        deadline = since+20
        d = None
        while time.monotonic() < deadline:
            d = ready()
            if not d:
                since = time.monotonic()
            elif time.monotonic()-since >= .6:
                return d
            time.sleep(.1)
        raise AssertionError('Foreground/native touch did not remain ready')

    def hashes(self, path):
        return sorted(self.sh('find '+shlex.quote(path)+' -type f -exec sha256sum {} \\;').splitlines())

    def raw(self, x, y):
        x, y = round(x), round(y)
        assert 0 <= x < self.w and 0 <= y < self.h, (x, y, self.w, self.h)
        rotation = self.screen.get('tp_direction', self.screen.get('direction', 0))
        x, y = (y, self.w-1-x) if rotation == 270 else (self.w-1-x, self.h-1-y) if rotation == 180 else (self.h-1-y, x) if rotation == 90 else (x, y)
        return x+self.screen.get('tp_xoffset', 0), y+self.screen.get('tp_yoffset', 0)

    def touch(self, x, y):
        point = self.raw(x, y)
        self._tap_with_ack(lambda: self.sh('PENCHAT_TOUCH_DEVICE='+self.event+' '+self.helper+' '+' '.join(map(str, point))+' 2>/dev/null'))

    def _tap_with_ack(self, emit, settle=.12):
        from touch_ack import retry_unseen_tap
        def ready():
            d = self.state()
            assert d['visible'] and d.get('page_visible', True) and d['window_focused'] and not d.get('keyboard') and not d.get('system_overlay', {}).get('panel_visible'), 'Refresh tap stopped after foreground loss'
            return d if d['input_focused'] and d['touch_input']['active'] and not d.get('surface_motion') else None
        for attempt in range(2):
            before = self.wait(ready, 3) if self.pid() else None
            emit()
            time.sleep(settle)
            if before is None or self.pid() != str(before['pid']):
                return
            observed = []
            def acknowledged():
                if self.pid() != str(before['pid']):
                    return True
                current = self.state();observed.append(current)
                return current.get('touch', {}).get('gesture') != before.get('touch', {}).get('gesture') and not current.get('touch', {}).get('pressed', False)
            try:
                self.wait(acknowledged, 1.2)
                return
            except AssertionError:
                if attempt or not observed or not retry_unseen_tap(before, observed[-1], allow_cancelled_down=True):
                    raise
                if self.sh('test -x '+self.input_state_probe+' && echo yes') != 'yes':
                    raise
                kernel = json.loads(self.sh(self.input_state_probe+' '+self.event))
                assert not kernel.get('error') and not kernel.get('btn_touch') and not kernel.get('active_slots'), 'Do not repress an active kernel contact'
                self.wait(ready, 3)
                self.sh(self.input_state_probe+' '+self.event+' --release');time.sleep(.2)
                current = self.state()
                if current.get('touch', {}).get('gesture') != before.get('touch', {}).get('gesture') and not current.get('touch', {}).get('pressed', False):
                    return
                assert retry_unseen_tap(before, current, allow_cancelled_down=True), 'UI changed while resynchronizing the cancelled contact'
                self.result['cancelled_input_tap_retries'] = self.result.get('cancelled_input_tap_retries', 0)+1

    def tap(self, bounds):
        self.touch((bounds[0]+bounds[2])/2, (bounds[1]+bounds[3])/2)

    def drag(self, x, y, ex, ey):
        values = (*self.raw(x, y), *self.raw(ex, ey))
        # tap.cpp's --drag-hold also holds at the *start*, which activates text
        # selection on a message. Ordinary movement begins after only 150ms.
        self.sh('PENCHAT_TOUCH_DEVICE='+self.event+' '+self.helper+' '+' '.join(map(str, values))+' 2>/dev/null')
        time.sleep(.45)

    def capture(self, name):
        self.sh('rm -f '+self.data+'/lvgl-diagnostic.png')
        self.sh('kill -USR1 '+self.pid())
        self.wait(lambda: self.sh('test -s '+self.data+'/lvgl-diagnostic.png && echo yes') == 'yes')
        subprocess.run(['adb', '-s', self.serial, 'pull', self.data+'/lvgl-diagnostic.png', str(self.out/(name+'.png'))], check=True, capture_output=True)

    def reveal(self, get_bounds):
        for _ in range(40):
            d = self.state()
            b, v = get_bounds(d), d['page']['viewport']
            if b[1] >= v[1]+8 and b[3] <= v[3]-8:
                return b
            delta = -1 if b[3] > v[3]-8 else 1
            x = (v[0]+v[2])/2
            y = v[1]+(v[3]-v[1])*(.75 if delta < 0 else .25)
            self.drag(x, y, x, y+delta*min(100, (v[3]-v[1])*.5))
        raise AssertionError('Setting is unreachable')

    def settings(self):
        d = self.state()
        scale = d['ui_scale']['applied']
        dp = lambda n: (n*scale+50)//100
        x = d['screen_safety']['navigation_x'] if self.h > self.w else dp(16)
        y = d['safe_insets'][1] if self.h > self.w else dp(8)
        self.touch(x+max(44, dp(48))/2, y+max(44, dp(48))/2)
        self.wait(lambda: self.state()['navigation_open'])
        self.tap(self.state()['navigation_footer']['settings']['bounds'])
        self.wait(lambda: self.state()['view'] == 'settings' and not self.state()['surface_motion'])

    def rate(self, hz):
        self.tap(self.reveal(lambda d: d['settings']['rows']['refresh_rate']['bounds']))
        self.wait(lambda: self.state()['context_menu']['kind'] == 'choice:refresh_rate')
        time.sleep(.18)
        self.tap(self.state()['context_menu']['choices'][str(hz)]['bounds'])
        self.wait(lambda: self.state()['refresh_rate']['applied'] == hz and not self.state()['refresh_rate']['pending'])
        self.check_rate(hz)

    def check_rate(self, hz):
        rate = self.state()['refresh_rate']
        assert rate['applied'] == hz and rate['configured'] == hz and not rate['pending'], rate
        assert all(rate[k] == 1000//hz for k in ('period_ms', 'animation_period_ms', 'input_period_ms')), rate

    def idle_bottom(self, name):
        self.wait(lambda: self.state()['at_latest_bottom'] and not self.state()['composer_hidden'])
        time.sleep(.35)
        samples = []
        for _ in range(8):
            d = self.state()
            samples.append({key: d[key] for key in ('scroll_y', 'scroll_bottom', 'composer_hidden', 'toolbar_y', 'following_latest', 'at_latest_bottom',
                                                   'to_bottom_visible', 'to_bottom_x', 'to_bottom_y')})
            assert d['session'] == 'refresh-bottom' and d['draft_length'] == 0
            assert d['at_latest_bottom'] and not d['composer_hidden'], samples
            assert not d['to_bottom_visible'], 'Return-to-bottom button remained visible at the latest bottom'
            time.sleep(.12)
        assert len({tuple(row.items()) for row in samples}) == 1, samples
        self.result['checks'].append({'name': name, 'samples': samples})
        self.capture(name)

    def bottom(self, hz):
        d = self.state()
        original_scroll = d['scroll_y']
        b = d['chat_header']['reader_bounds']
        x = (b[0]+b[2])/2
        # Leave enough history below the viewport to reverse direction without
        # reaching the real latest bottom.
        for _ in range(2):
            self.drag(x, b[1]+70, x, min(b[3]-12, b[1]+180))
        d = self.state()
        assert not d['at_latest_bottom'] and not d['following_latest'], d['scroll_bottom']
        assert d['scroll_y'] < original_scroll, 'Downward touch did not move into older content'
        if self.h <= self.w < 3*self.h:
            assert d['composer_hidden'], 'Compact composer appeared above the latest bottom'
        b = d['chat_header']['reader_bounds']
        older_scroll = d['scroll_y']
        self.drag(x, b[1]+145, x, b[1]+115)
        d = self.state()
        assert d['scroll_y'] > older_scroll, 'Touch reversal did not move toward the latest bottom'
        assert not d['at_latest_bottom'], 'Direction-reversal fixture unexpectedly reached bottom'
        if self.h <= self.w < 3*self.h:
            assert d['composer_hidden'], 'Direction reversal exposed the compact composer'
        self.result['checks'].append({'name': str(hz)+'hz-direction-reversal',
                                      'older_scroll_y': older_scroll, 'reversed_scroll_y': d['scroll_y'],
                                      'scroll_bottom': d['scroll_bottom'], 'composer_hidden': d['composer_hidden']})
        assert d['to_bottom_visible']
        self.touch(d['to_bottom_x']+22, d['to_bottom_y']+22)
        self.idle_bottom(str(hz)+'hz-button-return')
        # Exercise an actual reader drag to bottom separately from its button.
        d = self.state()
        b = d['chat_header']['reader_bounds']
        self.drag(x, b[1]+70, x, min(b[3]-12, b[1]+165))
        assert not self.state()['at_latest_bottom']
        for _ in range(8):
            d = self.state()
            if d['at_latest_bottom']:
                break
            b = d['chat_header']['reader_bounds']
            self.drag(x, min(b[3]-15, b[1]+140), x, b[1]+12)
        self.idle_bottom(str(hz)+'hz-drag-return')

    def preflight(self, discard_input=False, discard_images=False):
        if not self.pid():
            return
        d = self.state()
        # Explicit release authorization may discard an unsent editor/draft.
        # Saved data and unrelated background jobs retain their normal guards.
        editing = d['editor']
        input_busy = not discard_input and (d['draft_length'] or d['keyboard'] or editing['active'])
        ocr_busy = (d.get('ocr') or d.get('ocr_release_pending')) and (
            not discard_input or not editing['active'] or editing.get('ocr_busy') or editing.get('ocr_preparing'))
        volatile = (input_busy or d['generating'] or
                    d.get('compressing') or d['storage']['unsaved'] or d['provider_workspace']['dirty'] or
                    d.get('transfer_busy') or d.get('crop_busy') or
                    (not discard_images and (d['view'] in ('camera', 'crop', 'scan') or d.get('scan_result') or d.get('scan_id'))) or ocr_busy or
                    d.get('title_busy') or d.get('title_pending') or
                    d.get('pull_active') or d.get('session_info_busy') or d.get('chat_render_pending') or
                    d.get('message_interactions', {}).get('editing') or
                    d.get('message_interactions', {}).get('busy') or
                    d.get('reply_actions', {}).get('busy') or d.get('reply_actions', {}).get('confirm') or
                    (not discard_input and editing.get('pending')) or d.get('feedback', {}).get('collecting') or
                    (not discard_images and d.get('image_viewer', {}).get('open')) or
                    any(d.get(key, {}).get('pending') for key in ('ui_scale', 'appearance', 'refresh_rate')))
        assert not volatile, 'Pending input/work preserved; scoped acceptance deferred'
        # Only image-only tail rows in the *current* session can represent
        # pending input. Historical completed images in other sessions are
        # preserved by whole-directory isolation and must not block acceptance.
        assert self.sh('test ! -e '+self.snapshot+' && test ! -e '+self.snapshot_helper+' && echo safe') == 'safe', 'Old snapshot needs recovery'
        library = ROOT/'.deps'/(self.target+'-libs')/'libsqlite3.so.0'
        expected = hashlib.sha256(library.read_bytes()).hexdigest()
        assert self.sh('sha256sum /usr/lib/libsqlite3.so.0').split()[0] == expected, 'Unverified SQLite ABI; acceptance deferred'
        with tempfile.TemporaryDirectory(prefix='penchat-refresh-preflight-') as temp:
            folder = pathlib.Path(temp)
            compiler = subprocess.check_output(['bash', '-c', 'source scripts/native-target.sh '+self.target+'; printf "%s" "$PENCHAT_BUILD_CXX"'], cwd=ROOT, text=True)
            helper = folder/'snapshot'
            subprocess.run([compiler, '-std=c++11', '-Os', '-Wl,--as-needed', '-I'+str(ROOT/'.deps/include'),
                            str(ROOT/'tests/device/sqlite_snapshot.cpp'), str(library), '-Wl,--allow-shlib-undefined', '-o', str(helper)], check=True)
            try:
                self.push(helper, self.snapshot_helper)
                self.sh('chmod 700 '+self.snapshot_helper)
                assert self.sh(self.snapshot_helper+' '+self.data+'/penchat.db '+self.snapshot) == 'snapshot-ready'
                local = folder/'snapshot.db'
                subprocess.run(['adb', '-s', self.serial, 'pull', self.snapshot, str(local)], check=True, capture_output=True)
                with sqlite3.connect('file:'+str(local)+'?mode=ro', uri=True) as db:
                    sql = "select count(*) from chat_messages where session_id=? and role='user' and message_type='image' and coalesce(output_text,'')='' and seq>coalesce((select max(seq) from chat_messages where session_id=? and role='assistant'),0)"
                    count = db.execute(sql, (d['session'], d['session'])).fetchone()[0]
                    row = db.execute("select value_json from app_settings where key='ui_refresh_hz'").fetchone()
                    self.result['original_refresh_setting'] = {'exists': row is not None, 'value_json': row[0] if row else None}
                    value = json.loads(row[0]) if row else None
                    self.original_rate = value if type(value) is int and value in (30, 60) else 0
                assert count == 0 or discard_images, 'Current-session pending images preserved; scoped acceptance deferred'
                if discard_images:
                    self.result['discard_pending_images_authorized'] = True
                    self.result['pending_images_in_original_snapshot'] = count
            finally:
                self.sh('rm -f '+self.snapshot+' '+self.snapshot_helper)

    def fixture(self, path):
        schema = (ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
        with sqlite3.connect(path) as db:
            db.executescript(schema)
            for key, value in [('onboarding', {'version': 1, 'complete': True, 'step': 6}),
                               ('active_session_id', 'refresh-bottom'), ('auto_title_enabled', False),
                               ('auto_context_compression', False), ('theme_mode', 'dark')]:
                db.execute('insert into app_settings values(?,?,1)', (key, json.dumps(value)))
            db.execute("insert into providers(id,name,type,base_url,api_key,created_at,updated_at) values('p','Local rate fixture','openai-compatible','http://127.0.0.1:1/v1','test-only-fake-key',1,1)")
            db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,is_default,created_at,updated_at) values('a','p','fixture','Local fixture','chat',1,1,1)")
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at,default_provider_id,default_model_config_id) values('refresh-bottom','Refresh bottom fixture','active',1,1,1,'p','a')")
            text = '\n\n'.join('第 %d 段。底部阅读位置与输入框保持稳定。English 30/60 Hz。'%i for i in range(60))
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('reply','refresh-bottom',1,'assistant','complete','text',?,1,1)", (text,))

    def run(self):
        self.screen = json.loads(self.sh('cat /etc/miniapp/resources/cfg.json'))['screen']
        self.w, self.h = self.screen['width'], self.screen['height']
        if self.s6 and self.screen.get('direction') in (90, 270):
            self.w, self.h = self.h, self.w
        self.preflight()
        self.host = self.sh('pidof miniapp')
        self.boot = self.sh('cat /proc/sys/kernel/random/boot_id')
        assert self.sh('test ! -e '+self.saved+' && test ! -e '+self.public_saved+' && echo safe') == 'safe', 'Previous isolation needs recovery'
        had_data = self.sh('test -d '+self.data+' && echo yes') == 'yes'
        had_public = self.sh('test -d '+self.public+' && echo yes') == 'yes'
        public_hashes = self.hashes(self.public) if had_public else []
        private_hashes = []
        private = public = False
        recovery = {'serial': self.serial, 'host': self.host, 'boot': self.boot,
                    'data': self.data, 'saved': self.saved, 'public': self.public,
                    'public_saved': self.public_saved, 'had_data': had_data, 'had_public': had_public,
                    'public_hashes': public_hashes}
        def save_recovery():
            (self.out/'recovery.json').write_text(json.dumps(recovery, ensure_ascii=False, indent=2)+'\n')
        save_recovery()
        try:
            if had_public:
                self.sh('mv '+self.public+' '+self.public_saved)
            public = True
            self.sh('mkdir -p '+self.public)
            self.close()
            private_hashes = self.hashes(self.data) if had_data else []
            recovery['private_hashes'] = private_hashes
            save_recovery()
            if had_data:
                self.sh('mv '+self.data+' '+self.saved)
            private = True
            self.sh('mkdir -p '+self.data)
            with tempfile.TemporaryDirectory(prefix='penchat-refresh-device-') as temp:
                folder = pathlib.Path(temp)
                self.fixture(folder/'penchat.db')
                self.push(folder/'penchat.db', self.data+'/penchat.db')
                compiler = subprocess.check_output(['bash', '-c', 'source scripts/native-target.sh '+self.target+'; printf "%s" "$PENCHAT_BUILD_CXX"'], cwd=ROOT, text=True)
                helper = folder/'tap'
                subprocess.run([compiler, '-std=c++11', '-Os', '-Wl,--as-needed', str(ROOT/'tests/device/tap.cpp'), '-o', str(helper)], check=True)
                self.push(helper, self.helper)
                probe = folder/'input-state'
                subprocess.run([compiler, '-std=c++11', '-Os', str(ROOT/'tests/device/release_input_state.cpp'), '-o', str(probe)], check=True)
                self.push(probe, self.input_state_probe)
            self.sh('chmod 755 '+self.helper)
            self.sh('chmod 700 '+self.input_state_probe)
            self.event = self.sh(self.helper+' --discover')
            assert self.event.startswith('/dev/input/event') and self.event.rsplit('event', 1)[1].isdigit()
            self.push(self.package, self.remote_package)
            assert self.sh('sha256sum '+self.remote_package).split()[0] == self.result['package_sha256'], 'Uploaded package hash differs'
            assert '"ret": 0' in self.sh('miniapp_cli install '+self.remote_package)
            d = self.launch()
            assert [d['width'], d['height']] == [self.w, self.h]
            assert d['refresh_rate']['applied'] == (60 if self.h > self.w else 30)
            for hz in (30, 60):
                self.settings()
                self.rate(hz)
                self.close()
                self.launch()
                self.check_rate(hz)
                self.bottom(hz)
            self.result['passed'] = True
        except BaseException as error:
            self.result['error'] = str(error)
            if private:
                try:
                    # Only the isolated fake fixture is captured, never the
                    # restored original database or user content.
                    (self.out/'failure-diagnostic.json').write_text(json.dumps(self.state(), ensure_ascii=False, indent=2)+'\n')
                    self.capture('failure-fixture')
                except Exception:
                    pass
            raise
        finally:
            with defer_cleanup_signals():
                errors = []
                try:
                    self.close()
                except Exception as error:
                    errors.append('native close: '+str(error))
                try:
                    stopped = not self.pid()
                except Exception:
                    stopped = False
                for isolated, current, saved, existed in ((private, self.data, self.saved, had_data),
                                                           (public, self.public, self.public_saved, had_public)):
                    if not isolated:
                        continue
                    if not stopped:
                        errors.append('Retained isolation for recovery: '+saved)
                        continue
                    try:
                        self.sh('rm -rf '+current)
                        if existed:
                            self.sh('mv '+saved+' '+current)
                    except Exception as error:
                        errors.append('restore '+current+': '+str(error))
                try:
                    self.result['restored'] = (not private or not had_data or self.hashes(self.data) == private_hashes) and (not public or not had_public or self.hashes(self.public) == public_hashes) and not errors
                    self.result['host_unchanged'] = self.sh('pidof miniapp') == self.host and self.sh('cat /proc/sys/kernel/random/boot_id') == self.boot
                    self.sh('rm -f '+self.remote_package+' '+self.helper+' '+self.snapshot+' '+self.snapshot_helper+' '+self.input_state_probe)
                except Exception as error:
                    errors.append('restore verification: '+str(error))
                self.result['recovery_errors'] = errors
                (self.out/'result.json').write_text(json.dumps(self.result, ensure_ascii=False, indent=2)+'\n')
                assert self.result.get('restored') and self.result.get('host_unchanged') and not errors, self.result
                restored = self.launch()
                if hasattr(self, 'original_rate'):
                    assert restored['refresh_rate']['configured'] == self.original_rate, 'Original explicit refresh setting changed'
                    expected_rate = self.original_rate or (60 if self.h > self.w else 30)
                    assert restored['refresh_rate']['applied'] == expected_rate, 'Original effective refresh rate changed'
                    self.result['restored_refresh_rate'] = restored['refresh_rate']
                with zipfile.ZipFile(self.package) as package:
                    expected = hashlib.sha256(package.read('assets/bin/penchat-lvgl')).hexdigest()
                actual = self.sh('sha256sum /proc/'+self.pid()+'/exe').split()[0]
                assert actual == expected, 'Restored app is running a different payload'
                self.result['native_sha256'] = actual
                (self.out/'result.json').write_text(json.dumps(self.result, ensure_ascii=False, indent=2)+'\n')
        print(json.dumps(self.result, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, choices=ALLOWED)
    parser.add_argument('--package', required=True, type=pathlib.Path)
    args = parser.parse_args()
    install_termination_handler()
    Device(args.serial, args.package).run()
