#!/usr/bin/env python3
"""Both payload architectures reject shell APIs and malformed ELF tables."""
import struct
from runtime_policy import check_native_payload

def elf(wide,name):
 data=bytearray(512);data[:6]=b'\x7fELF'+bytes([2 if wide else 1,1])
 table=b'\0'+name.encode()+b'\0';data[64:64+len(table)]=table
 struct.pack_into('<Q' if wide else '<I',data,40 if wide else 32,256)
 struct.pack_into('<HH',data,58 if wide else 46,64 if wide else 40,3)
 fmt='<IIQQQQIIQQ' if wide else '<IIIIIIIIII';entry=64 if wide else 40;symbol=24 if wide else 16
 struct.pack_into(fmt,data,256+entry,0,3,0,0,64,len(table),0,0,1,0)
 struct.pack_into(fmt,data,256+2*entry,0,11,0,0,128,symbol,1,0,8 if wide else 4,symbol)
 struct.pack_into('<I',data,128,1)
 return data

def rejects(data):
 try:check_native_payload(data,'fixture')
 except AssertionError:return
 raise AssertionError('Forbidden or malformed runtime payload was accepted')

for wide in [False,True]:
 rejects(elf(wide,'safe_function')[:51 if not wide else 60])
 check_native_payload(elf(wide,'safe_function'),'fixture')
 for name in ['system','popen','popen64','wordexp','execvp','execlp','posix_spawnp']:
  rejects(elf(wide,name))
 data=elf(wide,'safe_function');data[200:208]=b'/bin/sh\0';rejects(data)
 data=elf(wide,'safe_function');struct.pack_into('<Q' if wide else '<I',data,40 if wide else 32,500);rejects(data)
print('ELF32_ELF64_SHELL_API_INTERPRETER_AND_BOUNDS_POLICY_OK')
