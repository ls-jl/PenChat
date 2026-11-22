"""Use the verified public AOSP copies of the scanner's PNG and zlib headers."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PNG_HEADERS = ('png.h', 'pngconf.h', 'pnglibconf.h')
ZLIB_HEADERS = ('zlib.h', 'zconf.h')


def check(path, expected):
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise ValueError(f'Scanner build input hash mismatch: {path}')


def source_headers(component, override=None):
    lock = json.loads((ROOT / 'native/scanner/dependencies.lock.json').read_text())
    upstream = lock['public_headers'][component]
    if component == 'libpng':
        checksums = {name: lock['png_source'][name] for name in (*PNG_HEADERS, 'LICENSE')}
    else:
        checksums = upstream['sha256']
    if override is not None:
        for name, digest in checksums.items():
            check(override / name, digest)
        return override

    cache = ROOT / '.deps/scanner-public-headers'
    cache.mkdir(parents=True, exist_ok=True)
    destination = cache / f"{component}-{upstream['commit']}"
    if not destination.is_dir():
        with tempfile.TemporaryDirectory(dir=cache) as directory:
            scratch = Path(directory)
            repo = scratch / 'git'
            subprocess.run(['git', 'init', '-q', str(repo)], check=True)
            subprocess.run(['git', '-C', str(repo), 'fetch', '-q', '--depth=1',
                            upstream['url'], upstream['commit']], check=True)
            verified = scratch / 'verified'
            verified.mkdir()
            for name, digest in checksums.items():
                path = verified / name
                path.write_bytes(subprocess.check_output(
                    ['git', '-C', str(repo), 'show', f"{upstream['commit']}:{name}"]))
                check(path, digest)
            try:
                verified.rename(destination)
            except OSError:
                if not destination.is_dir():
                    raise
    for name, digest in checksums.items():
        check(destination / name, digest)
    return destination
