#!/usr/bin/env python3
import pathlib,os,socket,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
binary=pathlib.Path(os.environ.get('PENCHAT_SENSOR_GUARD_TEST_BINARY',ROOT/'build/host/penchat-sensor-guard-test'))
with tempfile.TemporaryDirectory(prefix='penchat-sensor-') as directory:
    path=pathlib.Path(directory)/'report'
    for original in ['0','1']:
        path.write_text(original)
        parent,child=socket.socketpair();parent.settimeout(3)
        process=subprocess.Popen([str(binary),'--test-report',str(path)],stdin=subprocess.PIPE,stdout=child,stderr=subprocess.PIPE)
        child.close();assert parent.recv(16)==b'READY\n';assert path.read_text()=='1'
        # Closing the owning pipe is also what happens when the app crashes.
        process.stdin.close();assert process.wait(timeout=3)==0
        assert path.read_text()==original
        parent.close();process.stderr.close()
print('Accelerometer guard: owned reporting restored on EOF; pre-existing reporting preserved.')
