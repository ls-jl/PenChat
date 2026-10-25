#!/usr/bin/env python3
"""Keep application code and native unit tests free of shell evaluation."""
import pathlib
import re
from runtime_policy import FORBIDDEN, check_native_payload

ROOT = pathlib.Path(__file__).resolve().parents[1]
pattern = re.compile(r'\b(' + '|'.join(sorted(FORBIDDEN)) + r')\s*\(')
roots = ['native/lvgl-app/src', 'native/camera/src', 'native/scanner/src',
         'native/launcher/src', 'tests/native']
launches = {}
for root in roots:
    for path in (ROOT/root).rglob('*'):
        if path.suffix not in ('.cpp', '.c', '.hpp', '.h'):
            continue
        source = path.read_text()
        assert not pattern.search(source), str(path)
        calls = re.findall(r'\b(fork|vfork|execv|execve|execl|posix_spawn)\s*\(', source)
        if calls:
            launches[str(path.relative_to(ROOT))] = sorted(calls)
assert launches == {
    'native/camera/src/accelerometer.cpp': ['posix_spawn'],
    'native/launcher/src/Launcher.cpp': ['execl', 'fork'],
    'native/lvgl-app/src/scanner.cpp': ['execve', 'fork'],
    'tests/native/StorageTest.cpp': ['fork'],  # Crash-recovery child; no exec.
}, launches

for path in (ROOT/'src').rglob('*'):
    if path.suffix in ('.js', '.vue'):
        source = path.read_text()
        assert not re.search(r'''['"](?:child_process|shell|os)['"]''', source), path

for name in ('penchat-lvgl', 'penchat-sensor-guard'):
    check_native_payload((ROOT/'build/host'/name).read_bytes(), name)
check_native_payload((ROOT/'build/scanner-host/penchat-scanner').read_bytes(), 'scanner')
check_native_payload((ROOT/'build/host/libjsapi_penchat_launcher.so').read_bytes(), 'launcher')
print('Runtime command policy: no shell evaluation; only three reviewed native child launch sites.')
