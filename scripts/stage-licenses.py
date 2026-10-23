#!/usr/bin/env python3
"""Validate tracked notices and stage them without inferring missing grants."""
import hashlib
import json
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[1]


def license_payload():
    directory = ROOT / 'licenses'
    manifest = json.loads((directory / 'manifest.json').read_text())
    files = {}
    for entry in manifest['files']:
        name = entry['file']
        if pathlib.PurePosixPath(name).name != name or name in files:
            raise RuntimeError('Invalid or duplicate license path: ' + name)
        source = directory / name
        if hashlib.sha256(source.read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError('License checksum mismatch: ' + name)
        files[name] = source
    if {p.name for p in directory.iterdir() if p.is_file()} != set(files) | {'manifest.json'}:
        raise RuntimeError('License inventory and tracked files differ')
    files['manifest.json'] = directory / 'manifest.json'
    for name in ['LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md']:
        files[name] = ROOT / name
    files['dependencies.lock.json'] = ROOT / 'native/lvgl-app/dependencies.lock.json'
    return files


def stage():
    files = license_payload()  # Verify everything before changing generated output.
    target = ROOT / 'assets/licenses'
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for name, source in files.items():
        shutil.copyfile(source, target / name)
    print(f'Staged {len(files)} license and provenance files; unresolved grants remain documented.')


if __name__ == '__main__':
    stage()
