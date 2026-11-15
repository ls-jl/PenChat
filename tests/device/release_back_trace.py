"""Bounded native-only, payload-free syscall evidence for one Y07 back gesture."""
import json
import pathlib
import subprocess
import time

from release_lifecycle import free_back_start


def run(d):
  assert d.serial == '7G12000010905426'
  s = d.chat()
  assert s['visible'] and s.get('page_visible') and s['input_focused'] and s['touch_input']['active']
  assert not s['generating'] and not s['editor']['active']
  native = d.pid()
  assert d.sh('command -v strace') == '/bin/strace'
  assert 'raw=SET' in d.sh('/bin/strace -h 2>&1'), 'Installed strace has no raw syscall argument support'
  x, y = free_back_start(s, d.w, d.h)
  points = (*d.raw(x, y), *d.raw(min(d.w-1, x+max(80, d.w/4)), y))
  d.input_release()
  before = d.state()
  assert str(before['pid']) == native and before['input_focused']
  fds_before = d.sh('ls -l /proc/'+native+'/fd')
  remote = '/tmp/penchat-release-back-'+native+'.strace'
  pid_file = remote+'.pid'
  local = d.out/'y07-single-back.strace'
  # Attach only the native UI PID, with no -f and no payload decoding. Device
  # event structs may be decoded by ioctl, but IPC read/write buffers stay raw.
  syscalls = 'read,recvfrom,recvmsg,write,sendto,sendmsg,openat,close,ioctl,ppoll,?poll,?epoll_wait,epoll_pwait'
  raw = 'read,recvfrom,recvmsg,write,sendto,sendmsg'
  command = ('umask 077\n/bin/strace -I 1 -ttt -T -e trace='+syscalls+' -e raw='+raw+
             ' -o '+remote+' -p '+native+' 2>&1 &\npenchat_tracer=$!\nprintf "%s\\n" "$penchat_tracer" > '+pid_file+'\nwait "$penchat_tracer"')
  process = subprocess.Popen(['adb', '-s', d.serial, 'shell', command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
  tracer = ''
  receipt = ''
  started = time.monotonic()
  try:
    time.sleep(.1)
    tracer = d.sh('cat '+pid_file+' 2>/dev/null || true')
    assert tracer.isdigit(), 'No explicit tracer identity'
    time.sleep(.2)
    receipt = d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+' '.join(map(str, points))+' 2>&1\n'
                   'penchat_back_status=$?\nprintf "\\nremote_exit=%s\\n" "$penchat_back_status"')
    remaining = 2.0-(time.monotonic()-started)
    if remaining > 0:
      time.sleep(remaining)
  finally:
    if tracer:
      owner = d.sh("tr '\\0' ' ' < /proc/"+tracer+'/cmdline 2>/dev/null || true')
      if '/bin/strace' in owner and '-p '+native in owner:
        # Background shells may inherit SIGINT=ignored. SIGTERM to our own,
        # positively identified tracer reliably detaches without signaling the
        # native tracee and keeps the observation close to its two-second bound.
        d.sh('kill -TERM '+tracer)
    try:
      tail = process.communicate(timeout=2)[0]
    except subprocess.TimeoutExpired:
      owner = d.sh("tr '\\0' ' ' < /proc/"+tracer+'/cmdline 2>/dev/null || true') if tracer else ''
      if '/bin/strace' in owner and '-p '+native in owner:
        d.sh('kill -KILL '+tracer)
      process.terminate()
      tail = process.communicate(timeout=5)[0]
    subprocess.run(['adb', '-s', d.serial, 'pull', remote, str(local)], check=True, capture_output=True)
    d.sh('rm -f '+remote+' '+pid_file)
  after = d.state() if d.pid() else None
  fds_after = d.sh('ls -l /proc/'+native+'/fd 2>/dev/null || true')
  neutral = d.input_release()
  result = {'native_pid': native, 'tracer_pid': tracer, 'requested_trace_window_seconds': 2.0,
    'total_workflow_seconds': time.monotonic()-started,
    'trace': str(local), 'syscalls': syscalls, 'raw_syscalls': raw, 'follow_forks': False,
    'gesture': {'start': [x, y], 'end': [min(d.w-1, x+max(80, d.w/4)), y], 'receipt': receipt},
    'fd_before': fds_before.splitlines(), 'fd_after': fds_after.splitlines(),
    'before': {k: before.get(k) for k in ('pid','visible','page_visible','window_focused','input_focused','touch_input','back_navigation','touch','diagnostic_time')},
    'after': None if after is None else {k: after.get(k) for k in ('pid','visible','page_visible','window_focused','input_focused','touch_input','back_navigation','touch','diagnostic_time')},
    'kernel_neutral': neutral, 'tracer_messages': tail,
    'limitations': 'Diagnostic trace adds syscall overhead. No performance claim; only before/after diagnostics, no screenshot, no host/other-app tracing or decoded IPC payloads.'}
  (d.out/'y07-single-back-trace.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
  assert 'Operation not permitted' not in tail and 'invalid system call' not in tail, 'strace attach/filter failed: '+tail
  assert receipt.endswith('remote_exit=0'), 'Back event helper did not finish'
  return result
