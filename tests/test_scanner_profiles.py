#!/usr/bin/env python3
"""Deterministic real-process profile parity, including PNG and preview pixels."""
import argparse
import hashlib
import json
import os
import pathlib
import tempfile
from test_scanner_process import Scanner, ROOT


def sample(binary):
    previous = os.environ.get('PENCHAT_SCANNER_TEST_BINARY')
    os.environ['PENCHAT_SCANNER_TEST_BINARY'] = str(binary)
    results = {}
    try:
        for profile in ('orange', 'y02', 'y07', 'melon', 's6'):
            with tempfile.TemporaryDirectory(prefix='penchat-profile-parity-') as directory:
                worker = Scanner(directory, profile=profile, resources=ROOT/'assets/scanner')
                try:
                    worker.press()
                    worker.frames(80)
                    worker.release()
                    result = worker.receive('finished')
                    worker.send('confirm')
                    exported = worker.receive('exported')['image']
                    selected = {key: result.get(key) for key in (
                        'status', 'reason', 'usable', 'profile', 'sensor_width',
                        'sensor_height', 'sensor_rotation', 'sensor_region')}
                    selected['counts'] = {key: result['counts'][key] for key in (
                        'received', 'selected', 'committed', 'unlocated', 'batches')}
                    selected['images'] = {key: hashlib.sha256(pathlib.Path(exported[key]).read_bytes()).hexdigest()
                                          for key in ('path', 'previewPath')}
                    assert selected['counts'] == dict(received=80, selected=80, committed=80, unlocated=0, batches=3)
                    results[profile] = selected
                finally:
                    worker.close()
    finally:
        if previous is None:
            os.environ.pop('PENCHAT_SCANNER_TEST_BINARY', None)
        else:
            os.environ['PENCHAT_SCANNER_TEST_BINARY'] = previous
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    actual = sample(ROOT/'build/scanner-host/penchat-scanner')
    if args.baseline:
        expected = sample(args.baseline.resolve())
        assert actual == expected, json.dumps({'actual': actual, 'expected': expected}, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(actual, indent=2)+'\n')
    print('SCANNER_FIVE_PROFILES_THREE_BATCHES_PNG_PARITY_OK')
