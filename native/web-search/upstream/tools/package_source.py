"""Create a source-only archive with a hash, excluding host libraries and test output."""
import argparse
import hashlib
from pathlib import Path
import re
import tarfile


def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,default=root/'test-output')
    args=parser.parse_args()
    version=re.search(r'project\(EdgeSearch VERSION ([0-9.]+)',(root/'CMakeLists.txt').read_text()).group(1)
    args.output.mkdir(parents=True,exist_ok=True)
    archive=args.output/f'edge-search-{version}-source.tar.gz'
    def excluded(relative):
        return any(part in {'.git','.host-deps','test-output','__pycache__','build'} or part.startswith('build-') for part in relative.parts)
    with tarfile.open(archive,'w:gz') as tar:
        for path in sorted(root.rglob('*')):
            relative=path.relative_to(root)
            if excluded(relative) or not path.is_file() or path.resolve()==archive.resolve():continue
            tar.add(path,arcname=str(Path(f'edge-search-{version}')/relative))
    with tarfile.open(archive) as tar:
        names=tar.getnames()
        for relative in ['CMakeLists.txt','buildroot/package/edge-search/edge-search.mk','include/edgesearch/search.hpp','cmake/FindCurlImpersonate.cmake']:
            assert str(Path(f'edge-search-{version}')/relative) in names
    archive.with_suffix(archive.suffix+'.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
    print(archive)


if __name__=='__main__':main()
