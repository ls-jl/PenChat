#!/usr/bin/env python3
"""Exit evidence survives missing IPC delivery and records real fatal signals."""
import json,pathlib,signal,tempfile
import test_lvgl_app as t

t.port=1
for case in ('explicit_exit','ipc_eof','signal','fault'):
  with tempfile.TemporaryDirectory(prefix='penchat-exit-') as folder:
    a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False)
    try:
      pid=a.process.pid
      if case=='explicit_exit':a.send('exit')
      elif case=='ipc_eof':a.parent.close()
      elif case=='signal':a.process.send_signal(signal.SIGTERM)
      else:a.process.send_signal(signal.SIGABRT)
      result=a.process.wait(timeout=12)
      root=pathlib.Path(folder)
      if case=='fault':
        assert result==128+signal.SIGABRT,result
        assert (root/'native-fault.txt').read_text()==f'pid={pid}\nsignal=06\n'
      else:
        assert result==0,result
        record=json.loads((root/'native-exit.json').read_text())
        assert record['pid']==pid and record['reason']==case and record['phase']=='shutdown_complete',record
        assert record['signal']==(signal.SIGTERM if case=='signal' else 0),record
      print('EXIT_EVIDENCE',case,'passed',flush=True)
    finally:
      a.parent.close()
      if a.process.poll() is None:a.process.kill();a.process.wait()
      a.log.close()

# Falcon can reap a child before the launcher queries it. The independent child
# record must still explain a graceful signal exit when waitpid has no status.
old_handler=signal.signal(signal.SIGCHLD,signal.SIG_IGN)
try:
  with tempfile.TemporaryDirectory(prefix='penchat-autoreap-') as folder:
    a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False)
    try:
      a.process.send_signal(signal.SIGTERM)
      path=pathlib.Path(folder)/'native-exit.json'
      t.wait_for(lambda:path.exists() and json.loads(path.read_text()).get('phase')=='shutdown_complete')
      import os,time
      time.sleep(.1)
      try:os.waitpid(a.process.pid,os.WNOHANG)
      except ChildProcessError:pass
      else:raise AssertionError('Expected unavailable wait status after automatic reaping')
      assert json.loads(path.read_text())['reason']=='signal'
      print('EXIT_EVIDENCE autoreaped_child_with_durable_reason passed',flush=True)
    finally:a.close()
finally:signal.signal(signal.SIGCHLD,old_handler)
