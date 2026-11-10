"""Host-only fetch of a pinned native library; never runs the downloaded code."""

import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request

VERSION = 'v2.3.2'
HASHES = {
    'x86_64-linux-gnu': '7716be1ec87f13ecc776c211003bd1122c345287829b1f18a494a5467c90ab1e',
    'aarch64-linux-gnu': '1cb74a3b5103df020c252f9efef5a779c87380ee6244b365a90a0e4f7086864e',
    'arm-linux-gnueabihf': '1cc6b1a140cbcfdf96ab8d889be5b9780fe5871853b6142f9f30e9f589d6eb32',
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--arch', choices=HASHES, default='x86_64-linux-gnu')
    parser.add_argument('--prefix', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]/'.host-deps'
    root.mkdir(exist_ok=True)
    prefix = args.prefix or root/('curl-impersonate' if args.arch == 'x86_64-linux-gnu' else 'curl-impersonate-'+args.arch)
    filename = f'libcurl-impersonate-{VERSION}.{args.arch}.tar.gz'
    archive = root/filename
    url = f'https://github.com/lexiforest/curl-impersonate/releases/download/{VERSION}/{filename}'
    if not archive.exists():
        temporary = archive.with_suffix('.download')
        try:
            with urllib.request.urlopen(url, timeout=60) as response, temporary.open('wb') as out:
                while chunk := response.read(1024*1024):
                    out.write(chunk)
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != HASHES[args.arch]:
        raise RuntimeError(f'SHA256 mismatch: {archive}; no extraction performed')
    prefix.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        tar.extractall(prefix, filter='data')
    metadata = dict(version=VERSION, architecture=args.arch, sha256=digest, source=url,
                    verification='archive hash verified; target ABI/CPU/runtime compatibility not established')
    if args.arch == 'arm-linux-gnueabihf':
        metadata['known_local_failure'] = 'v2.3.2 prebuilt rejected by GNU time64/C++ exception smoke tests; build from source with the target SDK instead'
    (prefix/'edge-search-dependency.json').write_text(json.dumps(metadata, indent=2))
    print(json.dumps(dict(prefix=str(prefix.resolve()), **metadata), indent=2))


if __name__ == '__main__':
    main()
