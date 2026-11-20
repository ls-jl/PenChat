#!/usr/bin/env python3
import pathlib,json,hashlib,urllib.request,tarfile,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1];lock=json.loads((ROOT/'native/web-search/dependencies.lock.json').read_text())['xml']
archive=ROOT/'.deps/web-libxml2.tar.xz';target=ROOT/'.deps/web-libxml2'
if not archive.exists():
    cache=pathlib.Path('/home/ls-jl/Documents/ChatGPT/web search/edge-search/.host-deps/libxml2-2.15.4.tar.xz')
    if cache.exists():shutil.copyfile(cache,archive)
    else:urllib.request.urlretrieve(lock['url'],archive)
if hashlib.sha256(archive.read_bytes()).hexdigest()!=lock['sha256']:raise ValueError('libxml2 source hash mismatch')
if not target.exists():
    with tarfile.open(archive) as tar:
        for m in tar.getmembers():
            parts=pathlib.PurePosixPath(m.name).parts[1:]
            if not parts:continue
            if '..' in parts or m.issym() or m.islnk() or m.name.startswith('/'):raise ValueError('Unsafe source member')
            dest=target.joinpath(*parts)
            if m.isdir():dest.mkdir(parents=True,exist_ok=True)
            elif m.isfile():dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(tar.extractfile(m).read())
