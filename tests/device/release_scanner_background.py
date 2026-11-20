"""Real own-app onHide with a healthy scanner model, never optical acceptance."""
import hashlib
import json
import pathlib
import time

from release_lifecycle import pull_database
from release_shell_probe import focus_sequence

PROBE='90cecca30fae1df1d87517a3f602d73f6fb095ccc08a25e218295941a2103576'
BASELINE='a38a94b8ce390e2e59ab2227c84b84c786e12370ecfd89647ffa07429478c028'
PROFILES={'MEB0400008900794':('orange-sc2356',496,360),
  'MEA2300009007113':('y02-sc035gs',640,480),
  '7G12000010905426':('y07-sc2356',752,440)}


def run(d):
  assert hashlib.sha256(d.package.read_bytes()).hexdigest()==PROBE
  baseline=pathlib.Path(__file__).resolve().parents[2]/'.work/release-0.3.1/packages/candidate-5-native-back-rk3562.amr'
  assert hashlib.sha256(baseline.read_bytes()).hexdigest()==BASELINE
  d.result.update(shell_marker_cleared=False,defer_original_resume=True)
  profile,width,height=PROFILES[d.serial]
  result={'profile':profile,'source':'One real moveToBack at 60s; no forged visibility or Home',
    'paper_quality_validated':False,'owned_review_result_retention':'not exercised; each completed result is explicitly cancelled before onHide'}
  controlled_hidden=False
  try:
    entry=time.monotonic();s=d.state();seq=focus_sequence(s)
    assert 4000000<=seq<5000000,'60s scanner probe live marker not found'
    result['probe_focus_sequence']=seq;native=d.pid()
    def ready():
      s=d.state();scan=s['scanner']
      return scan if scan.get('type')=='ready' and scan.get('model_ready') else None
    initial=d.wait(ready,25);worker=str(initial['worker_pid'])
    assert initial['profile']==profile and [initial['sensor_width'],initial['sensor_height']]==[width,height]
    def handles():
      rows=d.sh('ls -l /proc/'+worker+'/fd').splitlines()
      return sorted(row.split(' -> ')[0].split()[-1]+' -> '+row.split(' -> ')[-1]
        for row in rows if ' -> /dev/dri/' in row or ' -> /dev/rknpu' in row)
    before_handles=handles();assert before_handles
    def capture_cancel(label):
      s=d.state();assert s['visible'] and s['page_visible'] and s['input_focused'] and s['touch_input']['active']
      old=s['scan_id']
      receipt=d.sh(d.key_helper+' /dev/input/by-path/led_control 134 1 500 100')
      assert 'sent 1 paired events code=134' in receipt
      def finished():
        s=d.state();r=s.get('scan_result',{})
        return r if r.get('type')=='finished' and r['scanId']!=old else None
      scan=d.wait(finished,25);assert scan['counts']['received']>=5 and str(scan['worker_pid'])==worker
      d.capture('scanner-background-'+label+'-discarded-review')
      s=d.state();scale=s['ui_scale']['applied'];dp=lambda n:(n*scale+50)//100
      if d.h>d.w:
        y=d.h-s['safe_insets'][3]-max(44,dp(44))/2 if s['screen_safety']['rounded'] else d.h-dp(58)+dp(44)/2
        d.touch(d.w/2,y)
      else:d.touch(d.w-dp(205),dp(108))
      d.wait(lambda:d.state()['view']=='chat');d.wait(ready)
      return {'scan_id':scan['scanId'],'received':scan['counts']['received'],'usable':scan['usable'],
        'worker_pid':scan['worker_pid'],'model_prepared_us':scan.get('model_prepared_us'),'explicitly_cancelled':True}
    result['before_scan']=capture_cancel('before')
    before=d.state();assert before['scanner'].get('hardware_armed')
    assert time.monotonic()-entry<50,'Preparation consumed the controlled background window'
    while time.monotonic()-entry<75:
      assert d.pid()==native,'Native process ended instead of a retained background handoff'
      hidden=d.state()
      if not hidden['page_visible']:
        if time.monotonic()-entry<50:
          d.result.setdefault('foreground_failures',[]).append({'reason':'Unexpected early handoff before the probe timer','pid':hidden['pid']})
          raise AssertionError('Unexpected early foreground handoff; do not resume over another task')
        controlled_hidden=True
        break
      time.sleep(.4)
    else:raise AssertionError('One-shot probe produced no real onHide')
    d.wait(lambda:d.state()['scan_released'],15);hidden=d.state()
    assert not hidden['visible'] and not hidden['touch_input']['active']
    assert d.sh('pidof penchat-scanner')==worker and handles()==before_handles
    light='/sys/class/leds/flash-led-gpio/brightness' if profile=='y07-sc2356' else '/sys/class/gpio/gpio43/value'
    assert d.sh('cat '+light)=='0'
    samples=[]
    result['guardian_resume_boundaries']=samples
    def guardian_ready():
      states={}
      for pid in d.sh('pidof CaptureFrame').split():
        value=d.sh('cat /proc/'+pid+'/stat 2>/dev/null || true')
        if value:states[pid]=value.rsplit(')',1)[1].split()[0]
      samples.append({'host_time_ns':time.time_ns(),'states':states})
      return states if any(state not in ('T','t','Z','X') for state in states.values()) else None
    # scan_released acknowledges our resource release. The system CaptureFrame
    # process is resumed asynchronously; require its running state explicitly.
    guardian_states=d.wait(guardian_ready,12)
    d.wait(lambda:d.sh('v4l2-ctl -d /dev/video23 --get-fmt-video')==d.result['scanner_format_before_probe'],8)
    result['hidden']={'native_pid':hidden['pid'],'worker_pid':worker,'scan_released':True,
      'touch_input_active':False,'light_off':True,'CaptureFrame_resumed':True,'format_restored':True,
      'npu_handles':before_handles,'prepared_us':hidden['scanner'].get('model_prepared_us'),
      'CaptureFrame_process_states':guardian_states}
    d.launch();resumed=d.wait(ready);assert d.pid()==native and str(resumed['worker_pid'])==worker
    assert handles()==before_handles and resumed['model_ready']
    result['after_scan']=capture_cancel('after')
    assert result['before_scan']['model_prepared_us']>0
    assert result['after_scan']['model_prepared_us']==result['before_scan']['model_prepared_us']
    snapshot=d.out/'scanner-background-fake.db';pull_database(d.serial,d.data,snapshot,d.sh)
    import sqlite3
    with sqlite3.connect(snapshot) as db:
      assert db.execute('select count(*) from chat_messages').fetchone()[0]==0
      assert db.execute('select count(*) from message_assets').fetchone()[0]==0
    result.update(real_onhide=True,same_native_and_worker_pid=True,same_npu_handles=True,
      prepared_model_reused=True,rearmed_after_resume=True,no_message_or_asset_confirmation=True,
      limitation='Background is entered from prepared idle state after an explicit discard; active unsealed cancellation and usable owned-result retention are not claimed')
  except BaseException as error:
    result['error']=str(error)
    (d.out/'scanner-background-failure.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    raise
  finally:
    if d.pid():
      s=d.state()
      if not s['page_visible'] and controlled_hidden and not d.result.get('foreground_failures'):d.launch()
      d.close()
    d.push(baseline,d.remote_package);assert '"ret": 0' in d.sh('miniapp_cli install '+d.remote_package)
    d.package=baseline
    if d.result.get('foreground_failures'):
      d.result.update(restore_package_hash=BASELINE,defer_original_resume=True)
      result.update(restore_package_hash=BASELINE,shell_marker_cleared=False,
        cleanup_note='Ordinary package reinstalled without focus takeover; live shell verification deferred')
      (d.out/'scanner-background-results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
      raise RuntimeError('Unexpected foreground handoff; ordinary live-shell verification deferred')
    d.launch();seq=focus_sequence(d.state());cleared=0<seq<1000000
    d.result.update(restore_package_hash=BASELINE,shell_marker_cleared=cleared,
      restored_shell_focus_sequence=seq,defer_original_resume=not cleared)
    assert cleared,'Probe shell still cached; original data must not start under it'
    d.close();result.update(restore_package_hash=BASELINE,shell_marker_cleared=True)
    (d.out/'scanner-background-results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
  return result
