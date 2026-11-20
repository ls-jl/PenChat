"""Marker-verified real Falcon moveToBack, always cleaned under fake data."""
import hashlib
import json
import pathlib
import time

from release_shell_probe import focus_sequence

PROBE = '96919a4f006fe60a5cf63a4af2491c1c95f70bdc3bf7141a8507a537b515af47'
BASELINE = 'a38a94b8ce390e2e59ab2227c84b84c786e12370ecfd89647ffa07429478c028'


def run(d, generation=False):
  assert hashlib.sha256(d.package.read_bytes()).hexdigest()==PROBE
  root=pathlib.Path(__file__).resolve().parents[2]
  baseline=root/'.work/release-0.3.1/packages/candidate-5-native-back-rk3562.amr'
  assert hashlib.sha256(baseline.read_bytes()).hexdigest()==BASELINE
  d.result.update(shell_marker_cleared=False,defer_original_resume=True)
  result={'package_sha256':PROBE,'background_method':'documented own-app am.moveToBack(), once 20s after ready'}
  try:
    before=d.state();seq=focus_sequence(before)
    assert 3000000<=seq<4000000,'Background shell live marker missing'
    result['focus_sequence']=seq
    assert before['draft_length']==len('penchatbackgroundfixture')
    assert before['scroll_y']>0 and not before['following_latest']
    if before['composer_hidden']:
      # The actual current landscape UI exposes a diagnosed double-tap
      # shortcut; an ordinary reader drag does not invoke this control.
      shortcut=before['composer_shortcut'];assert shortcut['available']
      b=shortcut['bounds'];x,y=d.raw((b[0]+b[2])/2,(b[1]+b[3])/2)
      assert before['input_focused'] and before['touch_input']['active']
      d.input_release()
      d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' --double '+str(x)+' '+str(y)+' 100 2>&1')
      d.input_release()
      d.wait(lambda:not d.state()['composer_hidden']);before=d.state()
      result['composer_revealed_by_native_shortcut']=True
    if generation:
      d.touch(before['send_center_x2']/2,before['send_center_y2']/2)
      d.wait(lambda:d.state()['receiving_reply'] and d.state()['output_length']>100)
    else:
      d.tap(before['composer_text_bounds'])
      d.wait(lambda:d.state()['editor']['active'])
    before=d.state();native=d.pid()
    result['before']={k:before.get(k) for k in ('pid','session','draft_length','scroll_y','scroll_bottom','following_latest','editor','page_visible','touch_input','generating','receiving_reply','output_length')}
    hidden=None
    deadline=time.monotonic()+25
    while time.monotonic()<deadline:
      assert d.pid()==native,'Real background ended the native process; same-PID retention is not passed'
      s=d.state()
      if not s['page_visible'] and not s['visible']:
        hidden=s;break
      time.sleep(.35)
    assert hidden,'Documented API produced no real onHide within the one-shot window'
    if generation:
      result['onhide_first_boundary']={k:hidden.get(k) for k in ('pid','page_visible','generating','receiving_reply','output_length')}
      assert hidden['generating'] and hidden['receiving_reply']
      length=hidden['output_length'];d.background_server.command('more')
      d.wait(lambda:d.state()['output_length']>length,8)
      hidden=d.state()
    assert not hidden['touch_input']['active']
    assert hidden['session']==before['session']
    if generation:
      assert before['generating'] and hidden['generating']
      assert hidden['output_length']>=before['output_length']>100
      assert len(d.background_server.requests)==1
    else:
      assert hidden['editor']['active'] and hidden['editor']['length']==before['editor']['length']
    result['hidden']={k:hidden.get(k) for k in ('pid','visible','page_visible','window_focused','input_focused','touch_input','editor','scroll_y','following_latest','scanner','generating','receiving_reply','output_length')}
    d.launch();after=d.state()
    assert d.pid()==native and after['session']==before['session']
    if generation:
      assert after['generating'] and after['output_length']>=hidden['output_length']
      assert len(d.background_server.requests)==1
      d.background_server.command('finish');d.wait(lambda:not d.state()['generating'])
      assert len(d.background_server.requests)==1
      result.update(generation_retained_on_real_background=True,hidden_stream_progress=True,
        partial_output_retained=True,no_automatic_retry=True,single_request_completed_after_resume=True)
    else:
      assert after['editor']['active'] and after['editor']['length']==before['editor']['length']
    assert after['draft_length']==before['draft_length']
    assert after['scroll_y']==hidden['scroll_y'] and after['following_latest']==hidden['following_latest']
    result['after']={k:after.get(k) for k in ('pid','page_visible','input_focused','touch_input','editor','scroll_y','following_latest','draft_length','generating','receiving_reply','output_length')}
    result.update(real_onhide=True,same_pid_resume=True,editing_and_reading_preserved=not generation,
      generation='passed hidden local stream progress/same-PID resume/no resend/final completion' if generation else 'pending separate local-stream background fixture')
    d.capture('real-background-resumed-fake-editor')
  except BaseException as error:
    result['error']=str(error)
    if d.pid():
      result['failure_boundary']=d.state()
    (d.out/'real-background-probe-failure.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    raise
  finally:
    # Resume only this explicitly initiated own-app background handoff, close its
    # native page normally, then prove plain shell is live before real restore.
    if d.pid():
      s=d.state()
      if not s['page_visible']:
        d.launch()
      d.close()
    d.push(baseline,d.remote_package)
    assert '"ret": 0' in d.sh('miniapp_cli install '+d.remote_package)
    d.package=baseline;d.launch();seq=focus_sequence(d.state())
    cleared=0<seq<1000000
    d.result.update(restore_package_hash=BASELINE,shell_marker_cleared=cleared,
      restored_shell_focus_sequence=seq,defer_original_resume=not cleared)
    assert cleared,'One-shot background shell still cached; never start restored user data under it'
    d.close()
    result.update(restore_package_hash=BASELINE,shell_marker_cleared=True,restored_focus_sequence=seq)
    (d.out/'real-background-probe-results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
  return result
