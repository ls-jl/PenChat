#!/usr/bin/env python3
"""Verify the accepted CV1826 bundle; stage only the model, never device libraries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path('/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/cv1826_int8_complete_20261018')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument('--bundle', type=Path, help='Full S6 scan resources (PENCHAT_CVI_SCAN_BUNDLE)')
    source.add_argument('--model', type=Path, help='Exact locked CVI model (PENCHAT_CVI_SCAN_MODEL)')
    parser.add_argument('--stage', type=Path, help='Explicit private/generated destination')
    args = parser.parse_args()
    lock_path = ROOT / 'native/scanner/cvi-dependencies.lock.json'
    lock = json.loads(lock_path.read_text())
    model = 'models/cvimodel/pintu_rgb32_int8_experimental.cvimodel'
    model_path = args.model
    if not model_path and not args.bundle and os.environ.get('PENCHAT_CVI_SCAN_MODEL'):
        model_path = Path(os.environ['PENCHAT_CVI_SCAN_MODEL'])
    if model_path:
        inputs = [(model_path, lock['files'][model])]
    else:
        bundle = args.bundle or Path(os.environ.get('PENCHAT_CVI_SCAN_BUNDLE') or DEFAULT)
        model_path = bundle / model
        inputs = [(bundle / name, entry) for name, entry in lock['files'].items()]
    for path, entry in inputs:
        if path.stat().st_size != entry['bytes']:
            raise ValueError('CVI source size mismatch: ' + str(path))
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if digest != entry['sha256']:
            raise ValueError('CVI source hash mismatch: ' + str(path))
    if args.stage:
        args.stage.mkdir(parents=True, exist_ok=True)
        shutil.copy2(model_path, args.stage / 'pintu_rgb32_int8.cvimodel')
        shutil.copy2(lock_path, args.stage / lock_path.name)
        shutil.copy2(ROOT / 'native/scanner/NOTICE', args.stage / 'NOTICE')
    print('CV1826 INT8 model and supplied bundle inputs verified.')


if __name__ == '__main__':
    main()
