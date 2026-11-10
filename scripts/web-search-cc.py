#!/usr/bin/env python3
import os,pathlib,sys
sdk=pathlib.Path(os.environ['PENCHAT_WEB_GCC']);triple=os.environ['PENCHAT_WEB_TRIPLE']
flags=['--target='+triple,'--gcc-toolchain='+str(sdk),'--sysroot='+str(sdk/triple/'libc')]
if triple.startswith('arm-'):flags+=['-march=armv7-a','-mfpu=neon','-mfloat-abi=hard']
else:flags+=['-mno-outline-atomics']
os.execv('/usr/lib/llvm-18/bin/clang',['clang',*flags,*sys.argv[1:]])
