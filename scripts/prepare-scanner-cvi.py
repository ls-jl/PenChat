#!/usr/bin/env python3
"""Verify the accepted CV1826 bundle; stage only the model, never device libraries."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path('/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/cv1826_int8_complete_20261018')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, default=DEFAULT)
    parser.add_argument('--stage', type=Path, help='Explicit private/generated destination')
    args = parser.parse_args()
    lock_path = ROOT / 'native/scanner/cvi-dependencies.lock.json'
    lock = json.loads(lock_path.read_text())
    for relative, entry in lock['files'].items():
        path = args.bundle / relative
        if path.stat().st_size != entry['bytes']:
            raise ValueError('CVI source size mismatch: ' + relative)
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if digest != entry['sha256']:
            raise ValueError('CVI source hash mismatch: ' + relative)
    if args.stage:
        args.stage.mkdir(parents=True, exist_ok=True)
        model = 'models/cvimodel/pintu_rgb32_int8_experimental.cvimodel'
        shutil.copy2(args.bundle / model, args.stage / 'pintu_rgb32_int8.cvimodel')
        shutil.copy2(lock_path, args.stage / lock_path.name)
        shutil.copy2(ROOT / 'native/scanner/NOTICE', args.stage / 'NOTICE')
    print('Accepted CV1826 INT8 model, runtime reference and provenance verified.')


if __name__ == '__main__':
    main()
