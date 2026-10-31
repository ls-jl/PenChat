#!/usr/bin/env python3
"""Real constructor failures and failed official-editor IPC handoff."""
import json,os,pathlib,signal,socket,sqlite3,subprocess,tempfile,time
import test_lvgl_app as t

t.port=1

BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/startup-recovery-0.2.16';OUT.mkdir(parents=True,exist_ok=True)

def database_failure():
    with tempfile.TemporaryDirectory(prefix='penchat-startup-recovery-') as tmp:
        folder=pathlib.Path(tmp);a=t.Native(BINARY,folder);args=list(a.process.args);a.close()
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute("CREATE TRIGGER fail_start BEFORE INSERT ON schema_meta BEGIN SELECT RAISE(ABORT,'fixture startup failure'); END")
        parent,child=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET);parent.settimeout(15)
        args[args.index('--bridge-fd')+1]=str(child.fileno())
        process=subprocess.Popen(args,pass_fds=[child.fileno()],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);child.close()
        try:
            message=json.loads(parent.recv(65536));assert message['type']=='fatal' and message['error']=='fixture startup failure',message
            log=process.communicate(timeout=15)[0].decode(errors='replace')
            assert process.returncode==1 and not any(marker in log for marker in ['terminate called','ERROR: AddressSanitizer','ERROR: LeakSanitizer','runtime error:']),(process.returncode,log)
        finally:
            parent.close()
            if process.poll() is None:process.kill();process.wait()
        with sqlite3.connect(folder/'penchat.db') as db:db.execute('DROP TRIGGER fail_start')
        a=t.Native(BINARY,folder)
        try:assert a.diag()['view']=='chat'
        finally:a.close()
    print('Database startup exception reaches fatal bridge; clean exit and restart passed.',flush=True)

def official_failure():
    with tempfile.TemporaryDirectory(prefix='penchat-official-recovery-') as tmp:
        a=t.Native(BINARY,pathlib.Path(tmp),official_adapter=False,bridge_send_buffer=8192)
        def state():return a.diag()['editor']
        def key(value):
            item=next(k for k in state()['keys'] if k['key']==value);a.tap(item['x'],item['y'])
        try:
            a.tap(500,232);t.wait_for(lambda:state()['active'])
            a.editor_action('official');request=a.receive('keyboard.open')
            text='字'*6000
            a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=text)
            t.wait_for(lambda:not state()['active'] and a.diag()['draft_length']==6000)
            a.tap(500,232);t.wait_for(lambda:state()['active'])
            key('n');key('i');t.wait_for(lambda:state()['preedit_length']==2)
            t.wait_for(lambda:not state()['pending']);time.sleep(.35)
            before=state()
            # This packet exceeds the real socket send capacity; no mock send.
            a.editor_action('official')
            t.wait_for(lambda:state()['active'] and not state()['official'] and state()['revision']>before['revision'])
            after=state()
            for field in ['length','cursor','preedit_length','parked_count','selection_start','selection_end','text_scroll']:
                assert after[field]==before[field],(field,before[field],after[field])
            assert after['ocr_active'] and after['notice_present'] and a.diag()['display_visible']
            # A result for the preceding handoff cannot commit after rollback.
            a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='stale')
            assert state()['length']==6000 and state()['active']
            image=a.directory/'lvgl-diagnostic.png';a.process.send_signal(signal.SIGUSR1);t.wait_for(image.exists)
            (OUT/'official-send-failed.png').write_bytes(image.read_bytes())
            key('delete');t.wait_for(lambda:state()['preedit_length']==1)
            key('enter');t.wait_for(lambda:state()['preedit_length']==0)
            a.editor_action('done');t.wait_for(lambda:not state()['active'])
            assert a.diag()['draft_length']>=6000
        finally:a.close()
    print('Official IPC failure restores display, editor, composition, cursor, OCR and further input; stale result ignored.',flush=True)

if __name__=='__main__':
    database_failure();official_failure()
