#!/usr/bin/env python3
"""C++17 objects with the production board CRT; never use a diagnostic _start."""
import os, pathlib, sys
research = pathlib.Path(os.environ.get('PENCHAT_SCAN_CROSS_ROOT',
    '/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/.deps/arm-cross'))
sdk = pathlib.Path(os.environ.get('RK_SDK_ROOT',
    '/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK'))
legacy = pathlib.Path(os.environ.get('JSAPI_TOOLCHAIN_ROOT',
    str(sdk/'prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu')))
args = sys.argv[1:]
if '-c' in args or '-E' in args or not any(v.endswith(('.o', '.a')) for v in args):
    compiler = research/'root/usr/bin/aarch64-linux-gnu-g++-12'
    os.environ['LD_LIBRARY_PATH'] = str(research/'root/usr/lib/x86_64-linux-gnu')
    cpp = research/'root/usr/aarch64-linux-gnu/include/c++/12'
    sysroot = legacy/'aarch64-linux-gnu/libc'
    includes = [cpp, cpp/'aarch64-linux-gnu', cpp/'backward',
        research/'root/usr/lib/gcc-cross/aarch64-linux-gnu/12/include', sysroot/'usr/include']
    flags = ['-nostdinc', '-nostdinc++', '--sysroot='+str(sysroot)]
    for path in includes: flags += ['-isystem',str(path)]
    compatibility = pathlib.Path(__file__).resolve().parents[1]/'native/scanner/board-compat.hpp'
    os.execv(str(compiler), [str(compiler), '-mno-outline-atomics',
        *flags, '-include',str(compatibility), *args])
else:
    compiler = legacy/'bin/aarch64-linux-gnu-g++'
    args = [v for v in args if not v.startswith('-std=') and v != '-mno-outline-atomics']
    private = pathlib.Path(__file__).resolve().parents[1]/'.deps/scanner-png/lib'
    os.execv(str(compiler), [str(compiler), '-L'+str(private), '-Wl,-rpath-link,'+str(private), *args])
