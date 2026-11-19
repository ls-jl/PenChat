"""Host-only framing, binary payload and EOF tests for the device test relay."""
import json
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import tempfile
import threading
import unittest
ROOT=Path(__file__).resolve().parents[2]
def exact(peer,n):
 result=bytearray()
 while len(result)<n:
  part=peer.recv(n-len(result))
  if not part:raise EOFError()
  result.extend(part)
 return bytes(result)
class TestRelay(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='penchat-relay-test-');cls.binary=Path(cls.temp.name)/'relay'
  subprocess.run(['g++','-std=c++14','-O2',str(ROOT/'tests/device/http_relay.cpp'),'-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def start(self):
  token=secrets.token_hex(32);p=subprocess.Popen([str(self.binary),token],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  info=json.loads(p.stdout.readline());control=socket.create_connection(('127.0.0.1',info['control']),timeout=5);control.sendall(token.encode())
  return p,info,control
 def test_parallel_binary_streams_and_eof(self):
  p,info,control=self.start();stopped=threading.Event();errors=[];opened=[]
  def echo():
   try:
    while not stopped.is_set():
     ident,size,kind=struct.unpack('!IIB',exact(control,9));data=exact(control,size)
     if kind==1:opened.append(ident)
     elif kind==2:control.sendall(struct.pack('!IIB',ident,size,2)+data)
   except (EOFError,OSError) as error:
    if not stopped.is_set():errors.append(error)
  thread=threading.Thread(target=echo);thread.start();peers=[]
  try:
   for _ in range(3):
    peer=socket.create_connection(('127.0.0.1',info['port']),timeout=5);peers.append(peer)
   for i,peer in enumerate(peers):
    data=bytes(range(256))*300+str(i).encode();peer.sendall(data);self.assertEqual(exact(peer,len(data)),data)
   peers[0].close();data=b'data: final\n\n';peers[1].sendall(data);self.assertEqual(exact(peers[1],len(data)),data)
   self.assertEqual(len(opened),3);self.assertFalse(errors)
  finally:
   stopped.set()
   for peer in peers:peer.close()
   # Keep the read half open until the relay observes an orderly input EOF.
   # Closing both halves with queued close frames can send a TCP reset, which
   # is a transport failure rather than the clean EOF this test exercises.
   control.shutdown(socket.SHUT_WR)
   try:self.assertEqual(p.wait(timeout=5),0)
   finally:
    control.close();thread.join(5);p.stdout.close();p.stderr.close()
 def test_oversized_frame_is_rejected(self):
  p,info,control=self.start()
  control.sendall(struct.pack('!IIB',1,32769,2));self.assertEqual(p.wait(timeout=5),1)
  control.close();p.stdout.close();p.stderr.close()
if __name__=='__main__':unittest.main()
