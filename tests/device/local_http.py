"""Test-only local HTTP transport; no device LAN route or adb reverse required."""
import fcntl
import http.server
import json
import os
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import threading

ALLOWED={'MEB0400008900794','MEA2300009007113','7G12000010905426','JDA0700006552216'}

class Relay:
 def __init__(self,root,serial,local_port):
  assert serial in ALLOWED
  self.serial=serial;self.local_port=local_port;self.clients={};self.lock=threading.Lock();self.send_lock=threading.Lock();self.stopped=False;self.errors=[]
  target='cv1826-s' if serial=='JDA0700006552216' else 'rk3562'
  source=root/'tests/device/http_relay.cpp';folder=root/'.work/device-http-relay'/target;folder.mkdir(parents=True,exist_ok=True)
  binary=folder/'relay'
  with (folder/'build.lock').open('a') as lock:
   fcntl.flock(lock,fcntl.LOCK_EX)
   if not binary.exists() or binary.stat().st_mtime_ns<source.stat().st_mtime_ns:
    compiler=subprocess.check_output(['bash','-c','source scripts/native-target.sh '+target+'; printf "%s" "$PENCHAT_BUILD_CXX"'],cwd=root,text=True)
    pending=folder/('relay-'+str(os.getpid()))
    subprocess.run([compiler,'-std=c++14','-Os','-static-libstdc++','-static-libgcc',str(source),'-o',str(pending)],check=True)
    pending.replace(binary)
  self.remote='/tmp/penchat-http-relay-'+secrets.token_hex(8)
  subprocess.run(['adb','-s',serial,'push',str(binary),self.remote],check=True,capture_output=True)
  subprocess.run(['adb','-s',serial,'shell','chmod 700 '+self.remote],check=True,capture_output=True)
  token=secrets.token_hex(32)
  self.process=subprocess.Popen(['adb','-s',serial,'shell',self.remote+' '+token],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  ready=json.loads(self.process.stdout.readline());self.pid=ready['pid'];self.port=ready['port']
  self.forward=int(subprocess.check_output(['adb','-s',serial,'forward','tcp:0','tcp:'+str(ready['control'])],text=True))
  self.control=socket.create_connection(('127.0.0.1',self.forward),timeout=5);self.control.settimeout(None);self.control.sendall(token.encode())
  self.thread=threading.Thread(target=self.read,daemon=True);self.thread.start()
 def send(self,ident,kind,data=b''):
  if self.stopped:return
  assert len(data)<=32768
  with self.send_lock:self.control.sendall(struct.pack('!IIB',ident,len(data),kind)+data)
 def receive(self,n):
  data=bytearray()
  while len(data)<n:
   part=self.control.recv(n-len(data))
   if not part:raise EOFError()
   data.extend(part)
  return data
 def read(self):
  try:
   while not self.stopped:
    ident,size,kind=struct.unpack('!IIB',self.receive(9));assert size<=32768
    data=self.receive(size)
    if kind==1:
     peer=socket.create_connection(('127.0.0.1',self.local_port),timeout=5);peer.settimeout(None)
     with self.lock:
      assert len(self.clients)<32;self.clients[ident]=peer
     threading.Thread(target=self.reply,args=(ident,peer),daemon=True).start()
    else:
     with self.lock:peer=self.clients.get(ident)
     if peer:
      try:
       if kind==2:peer.sendall(data)
       elif kind==3:
        with self.lock:self.clients.pop(ident,None)
        peer.shutdown(socket.SHUT_RDWR);peer.close()
       else:raise ValueError('Unknown relay packet')
      except OSError:
       with self.lock:self.clients.pop(ident,None)
       peer.close()
       if kind!=3:self.send(ident,3)
  except (OSError,EOFError,ValueError,AssertionError) as error:
   if not self.stopped:self.errors.append(type(error).__name__)
 def reply(self,ident,peer):
  try:
   while not self.stopped:
    data=peer.recv(32768)
    if not data:break
    self.send(ident,2,data)
  except OSError:pass
  finally:
   with self.lock:owned=self.clients.pop(ident,None)
   if owned:
    owned.close()
    try:self.send(ident,3)
    except OSError:pass
 def close(self):
  if self.stopped:return
  self.stopped=True
  try:self.control.shutdown(socket.SHUT_RDWR)
  except OSError:pass
  self.control.close()
  with self.lock:peers=list(self.clients.values());self.clients.clear()
  for peer in peers:
   try:peer.shutdown(socket.SHUT_RDWR)
   except OSError:pass
   peer.close()
  self.thread.join(3)
  try:self.process.wait(timeout=8)
  except subprocess.TimeoutExpired:
   subprocess.run(['adb','-s',self.serial,'shell','kill -TERM '+str(self.pid)],check=True,capture_output=True)
   self.process.wait(timeout=8)
  subprocess.run(['adb','-s',self.serial,'forward','--remove','tcp:'+str(self.forward)],check=True,capture_output=True)
  subprocess.run(['adb','-s',self.serial,'shell','rm -f '+self.remote],check=True,capture_output=True)
  assert self.process.returncode==0, self.process.stderr.read().decode(errors='replace')

def server_for_device(root,serial,handler):
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),handler);server.daemon_threads=True
 try:relay=Relay(root,serial,server.server_port)
 except BaseException:server.server_close();raise
 shutdown,close=server.shutdown,server.server_close
 def stop():
  relay.close();shutdown()
 server.shutdown=stop;server.server_close=close
 server.server_port=relay.port;server.server_address=('127.0.0.1',relay.port)
 server.relay=relay
 threading.Thread(target=server.serve_forever,daemon=True).start()
 return server,'127.0.0.1'
