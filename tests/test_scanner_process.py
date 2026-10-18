#!/usr/bin/env python3
"""Real scanner subprocess: bounded previews, confirmation, stale IPC and EOF."""
import json, mmap, os, pathlib, socket, struct, subprocess, tempfile, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
BUNDLE=pathlib.Path(os.environ.get('PENCHAT_SCAN_BUNDLE','/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/stream32_complete_20261018'))

class Scanner:
    def __init__(self,directory):
        self.directory=pathlib.Path(directory)
        self.socket,child=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        self.socket.settimeout(10);self.fd=os.memfd_create('scanner-test',0)
        os.ftruncate(self.fd,1024*1024);self.memory=mmap.mmap(self.fd,1024*1024)
        self.generation=1;self.scan=0;self.request=0;self.session='fixture'
        env=dict(os.environ,LD_LIBRARY_PATH=str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread'),OPENBLAS_NUM_THREADS='1')
        self.log=(self.directory/'worker.log').open('wb')
        self.process=subprocess.Popen([os.environ.get('PENCHAT_SCANNER_TEST_BINARY',str(ROOT/'build/scanner-host/penchat-scanner')),'--bridge-fd',str(child.fileno()),'--preview-fd',str(self.fd),'--resources',str(BUNDLE/'resources/illumination'),'--data-dir',str(self.directory/'pending'),'--simulate'],pass_fds=[child.fileno(),self.fd],env=env,stdout=self.log,stderr=self.log)
        child.close();self.send('arm');self.receive('ready')
    def send(self,kind,**values):
        self.request+=1
        self.socket.send(json.dumps(dict(v=1,type=kind,generation=self.generation,requestId=self.request,scanId=self.scan,session=self.session,**values)).encode())
    def receive(self,kind):
        while True:
            p=json.loads(self.socket.recv(32768))
            assert p['type'] not in ('error','fatal'),p
            assert p.get('generation')==self.generation,p
            if p['type']==kind:
                if kind=='started':self.scan=p['scanId']
                return p
    def press(self):self.send('test.press',pressed=True);return self.receive('started')
    def release(self):self.send('test.press',pressed=False)
    def frames(self,n):
        for i in range(n):self.send('test.frame',sequence=i)
    def close(self):
        self.socket.close();self.process.wait(timeout=10)
        assert self.process.returncode==0, (self.process.returncode, (self.directory/"worker.log").read_text(errors="replace")[-16000:])
        self.memory.close();os.close(self.fd);self.log.close()

def run():
    with tempfile.TemporaryDirectory(prefix='penchat-scanner-') as directory:
        s=Scanner(directory)
        try:
            s.press();s.release()
            empty=s.receive('finished');assert not empty['usable'] and empty['counts']['received']==0
            s.send('discard');s.receive('ready')
            # While the tip remains down, a complete first batch is already drawn.
            started=s.press();s.frames(32)
            end=time.monotonic()+10
            while True:
                progress=s.receive('progress')
                if progress['counts']['committed']>=24:break
                assert time.monotonic()<end
            assert progress['counts']['batches']==1 and progress['counts']['selected']==32
            assert not list((pathlib.Path(directory)/'pending').glob('*.png'))
            s.frames(24)
            while s.receive('progress')['counts']['committed']<48:pass
            s.release();result=s.receive('finished')
            assert result['status']==1 and result['counts']['committed']==56,result
            assert result['firstFrameUs']>=started['pressedUs']
            assert result['counts']['unlocated']==0
            seq,w,h=struct.unpack_from('<III',s.memory)
            gen,scan=struct.unpack_from('<QQ',s.memory,16)
            assert seq>0 and seq%2==0 and 0<w<=1024 and 0<h<=256
            assert gen==s.generation and scan==s.scan
            # No image is saved until explicit confirmation.
            assert not list((pathlib.Path(directory)/'pending').glob('*.png'))
            s.send('confirm');image=s.receive('exported')['image']
            for key in ['path','previewPath']:
                path=pathlib.Path(image[key])
                assert path.read_bytes().startswith(b'\x89PNG\r\n\x1a\n') and path.stat().st_size<=16*1024*1024
            s.send('confirm');assert s.receive('exported')['image']==image
            s.send('discard');s.receive('ready')
            assert not pathlib.Path(image['path']).exists()
            # Real sensor discontinuities stop and preserve an explicit partial result.
            for sequence,reason,dropped in [(0,'sensor_sequence_reset',0),(5,'sensor_sequence_gap',3)]:
                s.press();s.frames(2);s.send('test.frame',rawSequence=sequence)
                partial=s.receive('finished')
                assert partial['reason']==reason and partial['status']!=1,partial
                assert partial['counts']['dropped_raw']==dropped,partial
                assert partial['counts']['received']==2,partial
                s.release();s.send('discard');s.receive('ready')
            # UINT32 hardware sequence wrap is continuous, not a gap.
            s.press();s.send('test.frame',rawSequence=0xfffffffe);s.frames(2);s.release()
            wrapped=s.receive('finished')
            assert wrapped['status']==1 and wrapped['counts']['received']==3,wrapped
            s.send('discard');s.receive('ready')
            # Prior session/generation commands cannot finish or export a new scan.
            s.generation+=1;s.session='next';s.send('arm');s.receive('ready')
            s.socket.send(json.dumps(dict(v=1,type='confirm',generation=1,session='fixture',scanId=scan)).encode())
            s.press();s.frames(1);s.release();one=s.receive('finished')
            assert one['usable'] and one['counts']['committed']==1,one
        finally:s.close()
    # EOF can race a final asynchronous preview: it is a clean cancellation.
    for _ in range(6):
        with tempfile.TemporaryDirectory(prefix='penchat-eof-') as directory:
            s=Scanner(directory)
            try:
                s.press();s.frames(1);s.release();s.receive('finished')
            finally:s.close()
    print('Scanner IPC: concurrent 32/24 windows, lighting pipeline, bounded shared preview, confirmation, duplicate/stale commands, single/empty tail and EOF cleanup passed.')
if __name__=='__main__':run()
