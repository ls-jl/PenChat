"""Prepare a deterministic, hash-pinned source bundle; compilation requires no network."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024*1024):
            digest.update(chunk)
    return digest.hexdigest()


def obtain(entry, cache, reuse, offline):
    path = cache/entry['file']
    if not path.exists() and reuse:
        previous = reuse/(entry['name']+'-prefix')/'src'/entry.get('cache_file', entry['file'])
        if previous.is_file():
            if sha256(previous) != entry['sha256']:
                raise ValueError('cached build source has wrong hash: '+str(previous))
            shutil.copyfile(previous, path)
    if not path.exists():
        if offline:
            raise FileNotFoundError('offline cache missing: '+str(path))
        temporary = path.with_suffix(path.suffix+'.download')
        try:
            with urllib.request.urlopen(entry['url'], timeout=60) as source, temporary.open('wb') as target:
                total = 0
                while chunk := source.read(1024*1024):
                    total += len(chunk)
                    if total > 128*1024*1024:
                        raise ValueError('source exceeds download bound')
                    target.write(chunk)
            if sha256(temporary) != entry['sha256']:
                raise ValueError('source hash mismatch: '+entry['name'])
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    if sha256(path) != entry['sha256']:
        raise ValueError('source hash mismatch: '+str(path))
    return path


def license_bytes(path, filename):
    # Every pinned source archive has one top-level source directory.
    if path.suffix == '.zip':
        with zipfile.ZipFile(path) as archive:
            names = [x for x in archive.namelist() if x.count('/') == 1 and x.endswith('/'+filename)]
            if len(names) != 1 or archive.getinfo(names[0]).file_size > 256*1024:
                raise ValueError('unexpected license layout: '+str(path))
            return archive.read(names[0])
    with tarfile.open(path) as archive:
        members = [x for x in archive.getmembers() if x.name.count('/') == 1 and x.name.endswith('/'+filename) and x.isfile()]
        if len(members) != 1 or members[0].size > 256*1024:
            raise ValueError('unexpected license layout: '+str(path))
        return archive.extractfile(members[0]).read()


def adapt_cmake(source, manifest):
    text = source
    for entry in manifest['dependencies']:
        expression = r'set\('+entry['cmake_url']+r' "[^"\n]+"\)'
        replacement = 'set('+entry['cmake_url']+' "${CMAKE_SOURCE_DIR}/archives/'+entry['file']+'")'
        text, count = re.subn(expression, lambda _: replacement, text)
        if count != 1:
            raise ValueError('unexpected upstream URL declaration: '+entry['name'])
        if 'SHA256='+entry['sha256'] not in text:
            raise ValueError('upstream dependency hash changed: '+entry['name'])
    marker = 'set(_common_cmake_args\n'
    if text.count(marker) != 1:
        raise ValueError('upstream toolchain forwarding marker changed')
    text = text.replace(marker, 'if(CMAKE_TOOLCHAIN_FILE)\n  list(APPEND _toolchain_cmake_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")\nendif()\n\n'+marker)
    for language in ['c', 'cxx']:
        old = 'string(PREPEND _curl_'+language+'_flags "-I${DEPS_INSTALL_DIR}/include ")'
        new = 'string(PREPEND _curl_'+language+'_flags "-I\\"${DEPS_INSTALL_DIR}/include\\" ")'
        if text.count(old) != 1:
            raise ValueError('upstream include wiring changed')
        text = text.replace(old, new)
    text += '\nif(USE_LIBIDN2)\n  message(FATAL_ERROR "This offline bundle is prepared with USE_LIBIDN2=OFF")\nendif()\n'
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache-dir', type=Path, default=ROOT/'.host-deps/impersonate-source-cache')
    parser.add_argument('--reuse-build', type=Path)
    parser.add_argument('--output-dir', type=Path, default=ROOT/'.host-deps/impersonate-source-bundles')
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((ROOT/'tools/impersonate-sources.json').read_text())
    args.cache_dir.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    entries = [manifest['upstream'], *manifest['dependencies']]
    paths = {e['name']: obtain(e, args.cache_dir, args.reuse_build, args.offline) for e in entries}
    name = 'edge-curl-impersonate-'+manifest['bundle_version']
    archive_path = args.output_dir/(name+'.tar')
    with tempfile.TemporaryDirectory(prefix='bundle-', dir=args.output_dir) as temporary:
        folder = Path(temporary)
        with tarfile.open(paths['curl-impersonate']) as archive:
            if sum(x.size for x in archive.getmembers()) > 16*1024*1024:
                raise ValueError('unexpected upstream source size')
            archive.extractall(folder, filter='data')
        roots = list(folder.iterdir())
        if len(roots) != 1 or not roots[0].is_dir():
            raise ValueError('unexpected upstream source root')
        source = roots[0]
        original = (source/'CMakeLists.txt').read_text()
        adapted = adapt_cmake(original, manifest)
        (source/'CMakeLists.txt').write_text(adapted)
        (source/'edge-build-wiring.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True), adapted.splitlines(True), 'a/CMakeLists.txt', 'b/CMakeLists.txt')))
        (source/'edge-source-manifest.json').write_text(json.dumps(manifest, sort_keys=True, indent=2)+'\n')
        shutil.copyfile(ROOT/'tools/namespace_impersonate.py',source/'edge_namespace.py')
        (source/'archives').mkdir()
        (source/'licenses').mkdir()
        hashes = []
        for entry in entries:
            shutil.copyfile(paths[entry['name']], source/'archives'/entry['file'])
            license_name = 'LICENSE_'+entry['name'].upper().replace('-', '_')
            data = license_bytes(paths[entry['name']], entry['license_file'])
            (source/'licenses'/license_name).write_bytes(data)
            hashes.append('sha256  '+hashlib.sha256(data).hexdigest()+'  licenses/'+license_name)
        own_license=(ROOT/'LICENSE').read_bytes()
        (source/'licenses/LICENSE_EDGE_NAMESPACE').write_bytes(own_license)
        hashes.append('sha256  '+hashlib.sha256(own_license).hexdigest()+'  licenses/LICENSE_EDGE_NAMESPACE')
        # Inputs are already compressed. A plain canonical tar avoids an outer
        # compressor version changing the Buildroot source hash across hosts.
        target = folder/(name+'.tar')
        with tarfile.open(target, mode='w', format=tarfile.PAX_FORMAT) as archive:
            for path in sorted(source.rglob('*')):
                info = archive.gettarinfo(str(path), arcname=str(Path(name)/path.relative_to(source)))
                info.uid = info.gid = 0; info.uname = info.gname = 'root'; info.mtime = 0
                info.pax_headers = {}
                info.mode = 0o755 if path.is_dir() or path.stat().st_mode & 0o111 else 0o644
                if path.is_file():
                    with path.open('rb') as stream: archive.addfile(info, stream)
                else: archive.addfile(info)
        target.replace(archive_path)
        digest = sha256(archive_path)
        hash_text = '# Sources and versions pinned in tools/impersonate-sources.json\nsha256  '+digest+'  '+archive_path.name+'\n'+'\n'.join(hashes)+'\n'
        archive_path.with_suffix('.hash').write_text(hash_text)
        archive_path.with_suffix('.tar.sha256').write_text(digest+'  '+archive_path.name+'\n')
    print(json.dumps({'archive':str(archive_path.resolve()),'sha256':digest,'source_archives':len(entries),'bytes':archive_path.stat().st_size},indent=2))


if __name__ == '__main__':
    main()
