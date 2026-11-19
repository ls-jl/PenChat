import copy,json,unittest,tempfile,pathlib
from unittest.mock import Mock,patch
from refresh_bottom_acceptance import Device


class RefreshTapTest(unittest.TestCase):
  def fixture(self, states, active=False):
    device=Device.__new__(Device)
    folder=tempfile.TemporaryDirectory(prefix="penchat-tap-diagnostic-");self.addCleanup(folder.cleanup)
    device.out=pathlib.Path(folder.name)
    device.pid=lambda:'11'
    values=iter(states);device.state=lambda:copy.deepcopy(next(values))
    def wait(predicate,seconds):
      result=predicate()
      if result:return result
      raise AssertionError('not acknowledged')
    device.wait=wait;device.result={};device.event='/dev/input/fixture';device.input_state_probe='/probe'
    device.sh=Mock(side_effect=lambda cmd:'yes' if cmd.startswith('test -x ') else json.dumps({'btn_touch':active,'active_slots':[]}))
    return device

  def states(self):
    before={'pid':11,'diagnostic_time':100,'visible':True,'page_visible':True,
      'window_focused':True,'input_focused':True,'touch_input':{'active':True},
      'view':'settings','touch':{'gesture':7,'pressed':False},'editor':{'active':False,'actions':{}}}
    gap=copy.deepcopy(before);gap['diagnostic_time']=600
    gap['input_focus_trace']={'entries':[{'time':400,'reason':'touch_lease_applied_before_cancel',
      'event_value':False,'page_visible':True,'window_focused':True,'focus_active':True,
      'lease_age_ms':310,'overlay':{'panel_visible':False}}]}
    accepted=copy.deepcopy(gap);accepted['touch']['gesture']=8
    return before,gap,accepted

  def test_complete_native_contact_needs_no_retry(self):
    before,gap,accepted=self.states();device=self.fixture([before,accepted]);emit=Mock()
    with patch('refresh_bottom_acceptance.time.sleep'):device._tap_with_ack(emit)
    self.assertEqual(emit.call_count,1);device.sh.assert_not_called()

  def test_proven_unseen_expiry_retries_once_after_empty_kernel_resync(self):
    before,gap,accepted=self.states();device=self.fixture([before,gap,gap,gap,gap,accepted]);emit=Mock()
    with patch('refresh_bottom_acceptance.time.sleep'):device._tap_with_ack(emit)
    self.assertEqual(emit.call_count,2)
    self.assertEqual(device.result['cancelled_input_tap_retries'],1)
    self.assertEqual(len(json.loads((device.out/'unacknowledged-input.json').read_text())),1)
    self.assertEqual(sum('--release' in c.args[0] for c in device.sh.call_args_list),1)

  def test_active_kernel_contact_never_represses(self):
    before,gap,accepted=self.states();device=self.fixture([before,gap],active=True);emit=Mock()
    with patch('refresh_bottom_acceptance.time.sleep'):
      with self.assertRaises(AssertionError):device._tap_with_ack(emit)
    self.assertEqual(emit.call_count,1)

  def test_changed_ui_or_foreign_panel_never_replays(self):
    before,gap,accepted=self.states();gap['view']='chat';emit=Mock();device=self.fixture([before,gap])
    with patch('refresh_bottom_acceptance.time.sleep'):
      with self.assertRaises(AssertionError):device._tap_with_ack(emit)
    self.assertEqual(emit.call_count,1);device.sh.assert_not_called()
    before['system_overlay']={'panel_visible':True};device=self.fixture([before]);emit=Mock()
    with self.assertRaises(AssertionError):device._tap_with_ack(emit)
    emit.assert_not_called()


if __name__=='__main__':unittest.main()
