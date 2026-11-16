"""Real sensor -> model -> normal seal -> original viewer/cache; never sends."""
import hashlib,json,pathlib,subprocess,time

def close_viewer(d):
 # A synchronous diagnostic screenshot can outlast the 300ms input lease on
 # ARMv7. Wait for the real read-only channel to recover before injecting a tap.
 s=d.wait(lambda:(s if (s:=d.state())['input_focused'] and s['touch_input']['active'] else None))
 d.input_release()
 scale=s['ui_scale']['applied'];dp=lambda n:(n*scale+50)//100
 target=max(44,dp(44));portrait=d.h>d.w
 right=s['screen_safety']['navigation_x'] if portrait else dp(16)
 top=s['safe_insets'][1] if portrait else dp(8)
 d.touch(d.w-right-target/2,top+target/2)
 d.wait(lambda:not d.state()['image_viewer']['open'])

def run(d):
 assert d.s6
 d.chat();d.wait(lambda:d.state()['scanner'].get('type')=='ready' and d.state()['scanner'].get('model_ready'),90)
 pid=d.pid();r=d.state()['scanner'];worker=r['worker_pid']
 assert r['profile']=='s6-pag7920' and [r['sensor_width'],r['sensor_height']]==[320,240]
 manual=getattr(d,'manual_paper_scan',False)
 if manual:
  initial=d.state()['scan_id']
  print(json.dumps({'stage':'manual-paper-ready','profile':r['profile'],'scan_id_before':initial,'size':[d.w,d.h]}),flush=True)
  def completed():
   s=d.state()
   return s['scan_result'] if s['scan_id']>initial and s['scan_result'].get('type')=='finished' else None
  result=d.wait(completed,600)
 else:
  d.sh(d.key_helper+' /dev/input/by-path/platform-led_control-event 134 1 500 100')
  result=d.wait(lambda:(s['scan_result'] if (s:=d.state())['scan_result'].get('type')=='finished' else None),120)
 (d.out/'scan-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
 assert result['usable'],{'stage':'scan_result','reason':result.get('reason'),'counts':result.get('counts')}
 scan=d.state()['scan_id'];rows=[]
 for cycle in range(3):
  d.tap(d.state()['scan_preview_area'])
  initial_view=d.state()['image_viewer']
  assert initial_view['open']
  def loaded():
   s=d.state();v=s['image_viewer']
   if v.get('failed'):raise AssertionError('Original image stage failed: '+str(v))
   return s if v.get('loaded') else None
  s=d.wait(loaded,60);assert s['image_viewer']['scan_result'] and s['scan_id']==scan and not s['scan_released']
  files=d.sh('find '+d.data+'/.scan-pending -maxdepth 1 -name "*.png" -exec sha256sum {} \\;')
  assert files
  if rows:assert files==rows[0]['files_sha256']
  rows.append({'cycle':cycle,'initial_view':initial_view,'frame':s['image_viewer']['frame'],'files_sha256':files,'latency':s.get('scan_latency',{}),'prepare_timing':s['scan_result'].get('preparation',{}),'native_memory':d.sh("grep -E 'VmRSS|VmHWM' /proc/"+pid+'/status')})
  if manual and cycle==0:
   for line in files.splitlines():
    digest,remote=line.split(None,1);remote=remote.strip()
    if remote.endswith('.preview.png'):continue
    assert pathlib.PurePosixPath(remote).parent==pathlib.PurePosixPath(d.data+'/.scan-pending')
    local=d.out/'paper-original.png'
    subprocess.run(['adb','-s',d.serial,'pull',remote,str(local)],check=True,capture_output=True)
    assert hashlib.sha256(local.read_bytes()).hexdigest()==digest
  # Loaded is a data-ready marker; allow presentation before a visual capture.
  time.sleep(.12)
  d.capture('scan-original-'+str(cycle))
  close_viewer(d)
 assert d.pid()==pid and d.state()['scanner']['worker_pid']==worker
 return {'profile':r['profile'],'sensor_geometry':[320,240],'original_cycles':rows,'result_preserved':True,'scan_id':scan,'no_confirmation_or_send':True,'manual_paper_scan':manual,'scan_result':result,'paper_quality':'User forward paper scan; optical quality requires separate review' if manual else 'Not measured; source is automatic sensor capture'}


def latency(d):
 """Confirm only the outer runner's fixture; verify bytes after DB commit."""
 import sqlite3
 from release_lifecycle import pull_database
 evidence=run(d)
 assert d.s6 and (d.w,d.h)==(640,260) and d.state()['ui_scale']['applied']==100
 original=next(line.split()[0] for line in evidence['original_cycles'][0]['files_sha256'].splitlines() if not line.endswith('.preview.png'))
 generation=d.state()['scanner']['generation']
 d.touch(d.w-205,54)
 s=d.wait(lambda:(s if (s:=d.state())['view']=='chat' and s['scan_latency']['stored'] else None),60)
 assert not s['generating'] and s['scanner']['generation']==generation
 backup=d.out/'confirmed-fixture.db';pull_database(d.serial,d.data,backup,d.sh)
 with sqlite3.connect(backup) as db:
  assert db.execute('select count(*) from chat_messages').fetchone()[0]==2
  assets=db.execute('select stored_path,display_src,width,height,source_type from message_assets').fetchall()
 assert len(assets)==1 and assets[0][4]=='scan'
 stored,preview,width,height,_=assets[0]
 assert stored.startswith(d.data+'/assets/') and preview.startswith(d.data+'/assets/')
 assert d.sh('sha256sum '+stored).split()[0]==original
 evidence['review_stage_no_confirmation_or_send']=evidence.pop('no_confirmation_or_send')
 evidence.update(confirmed_into_isolated_fixture=True,network_send=False,stored_original_sha256=original,
  stored_size=[width,height],final_latency=s['scan_latency'])
 d.capture('scan-confirmed-chat')
 return evidence


def codec_fixture(d,folder):
 """Own synthetic files in the outer isolated fixture, never sensor captures."""
 import sqlite3,sys
 from refresh_bottom_acceptance import ROOT
 sys.path.insert(0,str(ROOT/'tests'))
 from test_image_viewer import png
 preview=folder/'scan-preview.png';png(preview,100,40)
 d.sh('mkdir -p '+d.data+'/assets');d.push(preview,d.data+'/assets/scan-preview.png')
 original=d.data+'/assets/scan-original.png'
 with sqlite3.connect(folder/'penchat.db') as db:
  seq=db.execute("select coalesce(max(seq),0)+1 from chat_messages where session_id='refresh-bottom'").fetchone()[0]
  db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('codec-scan','refresh-bottom',?,'user','complete','image','',1,1)",(seq,))
  db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,display_src,width,height,created_at) values('codec-asset','refresh-bottom','codec-scan','image','scan',?,?,?,12000,32,1)",(original,original,d.data+'/assets/scan-preview.png'))
  db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('codec-part','refresh-bottom','codec-scan',0,'image','codec-asset')")


def codec(d):
 import sys
 from refresh_bottom_acceptance import ROOT
 sys.path.insert(0,str(ROOT/'tests'));from test_image_viewer import png
 d.chat();pid=d.pid();session=d.state()['session'];draft=d.state()['draft_length']
 def open_image():
  def visible():
   s=d.state()
   return next((v for v in s['chat_images'] if v['y']>=0 and v['y']+v['height']<d.h-10),None)
  image=visible()
  for _ in range(8):
   if image:break
   d.drag(d.w//2,d.h//2,d.w//2,d.h//4);image=visible()
  assert image,'Synthetic scan preview must be visible'
  d.touch(image['x']+image['width']//2,image['y']+image['height']//2)
  d.wait(lambda:d.state()['image_viewer']['open'])
 open_image();d.wait(lambda:d.state()['image_viewer']['failed'])
 close_viewer(d)
 original=d.out/'synthetic-long-original.png';png(original,12000,32)
 d.push(original,d.data+'/assets/scan-original.png')
 rows=[]
 for cycle in range(3):
  open_image();s=d.wait(lambda:(s if (s:=d.state())['image_viewer'].get('loaded') else None),30)
  assert s['image_viewer']['frame']==[12000,32]
  # Two stationary taps form the viewer's existing 2x zoom gesture.
  x,y=d.raw(d.w//2,d.h//2)
  command='PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(x)+' '+str(y)
  d.sh(command+'; '+command);d.wait(lambda:d.state()['image_viewer']['zoom']==2)
  rows.append({'cycle':cycle,'frame':[12000,32],'native_memory':d.sh("grep -E 'VmRSS|VmHWM' /proc/"+pid+'/status')})
  if cycle<2:close_viewer(d)
 # Capture after the reopen workload so synchronous PNG diagnostics cannot
 # expire the input lease between its taps. Outer cleanup closes the last view.
 d.capture('synthetic-scan-original-final')
 assert d.pid()==pid and d.state()['session']==session and d.state()['draft_length']==draft and not d.state()['generating']
 return {'synthetic_source':True,'real_sensor_or_model_acceptance':False,'failed_decode_repaired_and_retried':True,'cycles':rows,'top_panel_disabled_in_image_workspace':True,'no_send':True}
