#!/usr/bin/env python3
"""Prepare isolated S6 link references. Never stage proprietary media DSOs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def checked(path, entry):
    return path.is_file() and path.stat().st_size == entry['bytes'] and digest(path) == entry['sha256']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default=os.environ.get('PENCHAT_ADB_SERIAL', 'JDA0700006552216'))
    parser.add_argument('--png-include', type=Path, default=Path(os.environ.get('PENCHAT_S6_PNG_INCLUDE',
        '/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK/external/libpng')))
    parser.add_argument('--opencv-archive', type=Path, default=Path(os.environ.get(
        'PENCHAT_S6_OPENCV_ARCHIVE', str(ROOT / '.deps/opencv-3.4.3.tar.gz'))))
    args = parser.parse_args()
    lock = json.loads((ROOT / 'native/scanner/cvi-media.lock.json').read_text())
    for relative, expected in lock.get('adapters', {}).items():
        if digest(ROOT / relative) != expected:
            raise ValueError('S6 adapter hash mismatch: ' + relative)
    cache = ROOT / '.deps/s6-media'
    cache.mkdir(parents=True, exist_ok=True)
    missing = [(name, entry) for name, entry in lock['libraries'].items() if not checked(cache / name, entry)]
    if missing:
        board = subprocess.check_output(['adb', '-s', args.serial, 'shell', 'cat', '/proc/device-tree/model']).decode().strip('\0\r\n')
        if board != lock['device']:
            raise ValueError('Refusing media reference fetch from a different board')
        for name, entry in missing:
            temporary = cache / (name + '.download')
            try:
                subprocess.run(['adb', '-s', args.serial, 'pull', entry['source'], str(temporary)], check=True)
                if not checked(temporary, entry):
                    raise ValueError('Unverified S6 firmware library: ' + name)
                temporary.replace(cache / name)
            finally:
                temporary.unlink(missing_ok=True)
    source = ROOT / '.deps/opencv-s6-source'
    marker = source / '.penchat-source-sha256'
    if not marker.is_file() or marker.read_text().strip() != lock['opencv']['sha256']:
        if not checked(args.opencv_archive, lock['opencv']):
            raise ValueError('Set PENCHAT_S6_OPENCV_ARCHIVE to the hash-locked OpenCV 3.4.3 archive; no unverified download')
        temporary = ROOT / '.deps/opencv-s6-extract'
        shutil.rmtree(temporary, ignore_errors=True)
        temporary.mkdir()
        try:
            with tarfile.open(args.opencv_archive) as archive:
                for member in archive.getmembers():
                    parts = Path(member.name).parts
                    if member.issym() or member.islnk() or Path(member.name).is_absolute() or '..' in parts:
                        raise ValueError('Unsafe OpenCV source entry')
                archive.extractall(temporary, filter='data')
            extracted = temporary / 'opencv-3.4.3'
            if not (extracted / 'CMakeLists.txt').is_file():
                raise ValueError('OpenCV source root mismatch')
            shutil.rmtree(source, ignore_errors=True)
            extracted.rename(source)
            marker.write_text(lock['opencv']['sha256'] + '\n')
        finally:
            shutil.rmtree(temporary, ignore_errors=True)
    png = ROOT / '.deps/s6-png'
    for relative, entry in lock['png'].items():
        destination = png / relative
        if checked(destination, entry):
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        if relative.startswith('include/'):
            source = args.png_include / Path(relative).name
            if not checked(source, entry):
                raise ValueError('Unverified PNG header: ' + str(source))
            shutil.copy2(source, destination)
        else:
            board = subprocess.check_output(['adb', '-s', args.serial, 'shell', 'cat', '/proc/device-tree/model']).decode().strip('\0\r\n')
            if board != lock['device']:
                raise ValueError('Refusing PNG runtime fetch from another board')
            temporary = destination.with_suffix('.download')
            try:
                subprocess.run(['adb', '-s', args.serial, 'pull', '/usr/lib/' + Path(relative).name, str(temporary)], check=True)
                if not checked(temporary, entry):
                    raise ValueError('Unverified S6 PNG runtime: ' + relative)
                temporary.replace(destination)
            finally:
                temporary.unlink(missing_ok=True)
    print('S6 firmware media hashes and isolated OpenCV source verified.')


if __name__ == '__main__':
    main()
