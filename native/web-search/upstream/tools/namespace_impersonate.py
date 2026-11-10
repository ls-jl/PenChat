"""Build a private ELF backend from the upstream PIC curl archive/private deps.

No upstream patches are edited. GNU objcopy renames public curl APIs in the
relocatable archive; the final linker hides every implementation symbol.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

PREFIX = 'edgesearch_'
SONAME = 'libedge-curl-impersonate.so.4'
FILENAME = SONAME+'.8.0'
BAD_ARMHF = 'c2edd8dca9da3dc2550675b99a091c5a23f8def5476b9b1795226eb6225abad5'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-prefix',type=Path,required=True)
    p.add_argument('--output-prefix',type=Path,required=True)
    p.add_argument('--cxx',default='g++')
    p.add_argument('--nm',default='nm')
    p.add_argument('--objcopy',default='objcopy')
    p.add_argument('--strip')
    p.add_argument('--flag',action='append',default=[])
    p.add_argument('--header-subdir',default='include')
    args=p.parse_args();source=args.source_prefix.resolve();output=args.output_prefix.resolve()
    archive=next((x for x in [source/'lib/libcurl-impersonate.a',source/'libcurl-impersonate.a'] if x.is_file()),None)
    if not archive:raise ValueError('upstream full PIC archive is required')
    dependencies=sorted((source/'lib/curl-impersonate').glob('*.a'))
    for lib in list(source.glob('libcurl-impersonate.so.*'))+list((source/'lib').glob('libcurl-impersonate.so.*')):
        if sha(lib)==BAD_ARMHF:raise ValueError('known incompatible ARMHF prebuilt; rebuild with the target SDK')
    raw=subprocess.check_output([args.nm,'-g','--defined-only',str(archive)],text=True,stderr=subprocess.DEVNULL)
    symbols=sorted({line.split()[-1] for line in raw.splitlines() if line.split() and re.fullmatch(r'curl_[A-Za-z0-9_]+',line.split()[-1])})
    required={'curl_easy_init','curl_easy_impersonate','curl_easy_setopt','curl_easy_getinfo','curl_multi_init','curl_multi_poll','curl_url','curl_version_info'}
    if not required.issubset(symbols):raise ValueError('archive misses required curl APIs')
    include=output/args.header_subdir
    if Path(args.header_subdir).is_absolute() or '..' in Path(args.header_subdir).parts:raise ValueError('header-subdir must stay within prefix')
    (output/'lib').mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.namespace-',dir=output) as work:
        work=Path(work);mapping=work/'symbols.txt';mapping.write_text(''.join(name+' '+PREFIX+name+'\n' for name in symbols))
        renamed=work/'renamed.a'
        subprocess.run([args.objcopy,'--redefine-syms='+str(mapping),str(archive),str(renamed)],check=True)
        exports=work/'exports.map';exports.write_text('{ global: edgesearch_curl_*; local: *; };\n')
        marker=work/'marker.cpp';marker.write_text('extern "C" __attribute__((visibility("default"))) unsigned edgesearch_curl_namespace_abi() { return 1; }\n')
        library=work/FILENAME
        command=[args.cxx,*args.flag,'-fPIC','-shared','-Wl,--no-undefined','-Wl,-soname,'+SONAME,
                 '-Wl,--version-script='+str(exports),str(marker),'-Wl,--whole-archive',str(renamed),
                 '-Wl,--no-whole-archive','-Wl,--start-group',*[str(x) for x in dependencies],'-Wl,--end-group',
                 '-lm','-ldl','-lpthread','-o',str(library)]
        subprocess.run(command,check=True)
        defined=subprocess.check_output([args.nm,'-D','--defined-only',str(library)],text=True)
        exported={line.split()[-1] for line in defined.splitlines() if line.split()}
        if not exported or any(not name.startswith('edgesearch_curl_') for name in exported):
            raise ValueError('unexpected global implementation symbol escaped the private backend')
        if not {PREFIX+name for name in required}.issubset(exported):raise ValueError('required namespaced API missing')
        if args.strip:subprocess.run([args.strip,'--strip-unneeded',str(library)],check=True)
        include.mkdir(parents=True,exist_ok=True)
        shutil.copytree(source/'include/curl',include/'curl',dirs_exist_ok=True)
        if (source/'include/curl-impersonate').exists():
            shutil.copytree(source/'include/curl-impersonate',include/'curl-impersonate',dirs_exist_ok=True)
        # curl also defines function-like convenience macros in its public
        # headers. Rename those declarations/macros together, rather than letting
        # them overwrite the API aliases with unprefixed recursive calls.
        names=re.compile(r'\b(?:'+'|'.join(re.escape(n) for n in symbols)+r')\b')
        for file in (include/'curl').rglob('*.h'):
            file.write_text(names.sub(lambda match:PREFIX+match.group(0),file.read_text()))
        header='''#ifndef EDGESEARCH_CURL_NAMESPACE_H
#define EDGESEARCH_CURL_NAMESPACE_H
#define EDGESEARCH_CURL_NAMESPACE_ABI 1
#define CURL_DISABLE_TYPECHECK 1
#ifdef __cplusplus
extern "C" {
#endif
unsigned edgesearch_curl_namespace_abi(void);
#ifdef __cplusplus
}
#endif
'''+''.join('#define '+name+' '+PREFIX+name+'\n' for name in symbols)+'#endif\n'
        (include/'edgesearch_curl_namespace.h').write_text(header)
        library.replace(output/'lib'/FILENAME)
        for name in [SONAME,'libedge-curl-impersonate.so']:
            path=output/'lib'/name
            if path.is_symlink() or path.is_file():path.unlink()
            path.symlink_to(FILENAME)
        for file in source.glob('LICENSE*'):
            if file.is_file():shutil.copy2(file,output/file.name)
        metadata={'namespace_abi':1,'source_archive_sha256':sha(archive),'api_symbols':symbols,
                  'private_archives':{file.name:sha(file) for file in dependencies},
                  'exported_symbols':sorted(exported),'library_sha256':sha(output/'lib'/FILENAME),
                  'soname':SONAME,'header_subdir':args.header_subdir,'link_command':command,
                  'scope':'same full upstream implementation; curl API names isolated and private dependency exports hidden'}
        (output/'edge-curl-namespace.json').write_text(json.dumps(metadata,indent=2))
    print(json.dumps({'prefix':str(output),'symbols':len(symbols),'sha256':metadata['library_sha256']},indent=2))


if __name__=='__main__':main()
