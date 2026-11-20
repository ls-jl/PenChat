#!/usr/bin/env python3
"""Modern C++17 headers/language, target SDK CRT and private worker-only linkage."""
import os,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1]
sdk=pathlib.Path(os.environ['PENCHAT_WEB_GCC']);triple=os.environ['PENCHAT_WEB_TRIPLE']
headers=pathlib.Path(os.environ['PENCHAT_WEB_CXX_HEADERS'])
args=sys.argv[1:]; compiler='/usr/lib/llvm-18/bin/clang++'
flags=['--target='+triple,'--gcc-toolchain='+str(sdk),'--sysroot='+str(sdk/triple/'libc'),'-nostdinc++',
       '-isystem',str(headers),'-isystem',str(headers/triple),'-isystem',str(headers/'backward'),
       '-include',str(root/'native/scanner/board-compat.hpp')]
if triple.startswith('arm-'):flags+=['-march=armv7-a','-mfpu=neon','-mfloat-abi=hard']
else:flags+=['-mno-outline-atomics']
if '-c' not in args and '-E' not in args:
    target='rk3562' if triple.startswith('aarch64') else 'cv1826-s'
    flags+=['-L'+str(root/('build/web-transport-'+target)/'deps/install/lib')]
os.execv(compiler,[compiler,*flags,*args])
