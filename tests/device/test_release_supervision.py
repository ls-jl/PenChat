"""Host-only recovery supervision regressions. Never invokes adb."""
import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

from release_acceptance import ReleaseDevice, defer_cleanup_signals, stop_owned_child
from release_matrix import supervise


class RecoverySupervisionTests(unittest.TestCase):
  def test_cleanup_defers_repeated_term_and_interrupt(self):
    original = {n: signal.getsignal(n) for n in (signal.SIGTERM, signal.SIGINT)}
    with defer_cleanup_signals() as deferred:
      os.kill(os.getpid(), signal.SIGTERM)
      os.kill(os.getpid(), signal.SIGTERM)
      os.kill(os.getpid(), signal.SIGINT)
      completed = True
    self.assertTrue(completed)
    self.assertEqual(deferred, [signal.SIGTERM, signal.SIGTERM, signal.SIGINT])
    self.assertEqual(original, {n: signal.getsignal(n) for n in original})

  def test_live_child_timeout_retains_isolation_and_blocks_next_scope(self):
    child = mock.Mock(pid=12345)
    child.poll.return_value = None
    child.wait.side_effect = subprocess.TimeoutExpired('owned-child', .01)
    self.assertFalse(stop_owned_child(child, timeout=.01))
    child.send_signal.assert_called_once_with(signal.SIGTERM)
    with tempfile.TemporaryDirectory() as temp:
      device = object.__new__(ReleaseDevice)
      device.active_child, device.out = child, pathlib.Path(temp)
      device.serial, device.result = 'MEA2300009007113', {}
      device.record = mock.Mock()
      device.sh = mock.Mock(side_effect=AssertionError('No device work allowed'))
      with self.assertRaisesRegex(RuntimeError, 'Owned child still alive'):
        device.require_reaped_child()
      self.assertFalse(device.result['restored'])
      self.assertEqual(json.loads((device.out/'live-child-recovery.json').read_text())['pid'], 12345)
      device.sh.assert_not_called()
      # The blocker must reject the next run before any preflight/device access.
      with mock.patch('release_acceptance.ROOT', pathlib.Path(temp)):
        with self.assertRaisesRegex(RuntimeError, 'explicit recovery'):
          device.run()
      device.sh.assert_not_called()

  def test_matrix_forwards_once_and_waits_for_child_recovery(self):
    with tempfile.TemporaryDirectory() as temp:
      folder = pathlib.Path(temp)
      script = folder/'child.py'
      script.write_text('''import pathlib,signal,time
p=pathlib.Path(__file__).parent
count=0
stop=False
def term(n,f):
  global count,stop
  count+=1
  stop=True
signal.signal(signal.SIGTERM,term)
(p/'ready').write_text('ready')
while not stop: time.sleep(.01)
time.sleep(.4)
(p/'recovered').write_text(str(count))
''')
      def interrupt():
        deadline = time.monotonic()+5
        while not (folder/'ready').exists() and time.monotonic() < deadline:
          time.sleep(.01)
        os.kill(os.getpid(), signal.SIGTERM)
        time.sleep(.15)
        os.kill(os.getpid(), signal.SIGTERM)
        os.kill(os.getpid(), signal.SIGINT)
      sender = threading.Thread(target=interrupt)
      sender.start()
      with (folder/'log').open('w') as log:
        result = supervise([sys.executable, str(script)], log, recovery_timeout=5)
      sender.join()
      self.assertEqual(result['returncode'], 0)
      self.assertTrue(result['interrupted'])
      self.assertFalse(result['recovery_timeout'])
      self.assertEqual((folder/'recovered').read_text(), '1')

  def test_matrix_records_recovery_then_stops_after_interrupt(self):
    import release_matrix
    with tempfile.TemporaryDirectory() as temp:
      root = pathlib.Path(temp)
      serial = 'MEA2300009007113'
      source = root/'.work/release-0.3.1/devices'/serial
      source.mkdir(parents=True)
      def recovered(*args):
        (source/'result.json').write_text(json.dumps({'restored': True,
          'host_unchanged': True, 'functional_selection_completed': False}))
        return {'returncode': 1, 'interrupted': True, 'recovery_timeout': False,
          'child_pid': 12345, 'stop_forwarded': True}
      argv = ['release_matrix.py', '--serial', serial, '--package', str(root/'fake.amr'),
        '--candidate', 'host-test', '--features', 'smoke', 'keyboard-review',
        '--continue-functional-failures']
      with mock.patch.object(release_matrix, 'ROOT', root), \
           mock.patch.object(release_matrix, 'supervise', side_effect=recovered) as supervisor, \
           mock.patch.object(sys, 'argv', argv), mock.patch('builtins.print'):
        release_matrix.main()
      supervisor.assert_called_once()
      result = json.loads((root/'.work/release-0.3.1/matrix/host-test'/serial/'manifest.json').read_text())
      self.assertEqual(len(result['modules']), 1)
      self.assertTrue(result['modules'][0]['restoration']['restored'])
      self.assertIn('Termination requested', result['stopped'])

  def test_matrix_timeout_does_not_kill_owned_child(self):
    child = mock.Mock(pid=23456)
    child.poll.return_value = None
    with mock.patch('release_matrix.subprocess.Popen', return_value=child), \
         mock.patch('release_matrix.time.sleep', side_effect=lambda _: os.kill(os.getpid(), signal.SIGTERM)):
      with tempfile.TemporaryFile(mode='w') as log:
        result = supervise(['owned-child'], log, recovery_timeout=0)
    self.assertTrue(result['recovery_timeout'])
    self.assertIsNone(result['returncode'])
    child.send_signal.assert_called_once_with(signal.SIGTERM)
    child.kill.assert_not_called()
    child.terminate.assert_not_called()


if __name__ == '__main__':
  unittest.main()
