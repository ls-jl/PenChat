import copy,unittest
from touch_ack import retry_unseen_tap
class TapAck(unittest.TestCase):
 def fixture(self):
  before={'pid':11,'diagnostic_time':100,'visible':True,'page_visible':True,'window_focused':True,'input_focused':True,'view':'chat','touch':{'gesture':7,'pressed':False},'editor':{'active':False,'actions':{}}}
  after=copy.deepcopy(before);after['diagnostic_time']=600;after['input_focus_trace']={'entries':[{'time':400,'reason':'touch_lease_applied_before_cancel','event_value':False,'page_visible':True,'window_focused':True,'focus_active':True,'lease_age_ms':310,'overlay':{'panel_visible':False}}]};return before,after
 def test_only_proven_unseen_cancellation(self):
  a,b=self.fixture();self.assertTrue(retry_unseen_tap(a,b))
 def test_observed_contact_never_replays(self):
  a,b=self.fixture();b['touch']['gesture']=8;self.assertFalse(retry_unseen_tap(a,b))
 def test_held_or_changed_process_never_replays(self):
  for key in ('held','pid'):
   a,b=self.fixture()
   if key=='held':b['touch']['pressed']=True
   else:b['pid']=12
   self.assertFalse(retry_unseen_tap(a,b))
 def test_foreign_window_or_panel_never_replays(self):
  a,b=self.fixture();b['window_focused']=False;self.assertFalse(retry_unseen_tap(a,b))
  a,b=self.fixture();b['system_overlay']={'panel_visible':True};self.assertFalse(retry_unseen_tap(a,b))
 def test_changed_target_or_text_never_replays(self):
  for field in ('view','session'):
   a,b=self.fixture();b[field]='changed';self.assertFalse(retry_unseen_tap(a,b))
  a,b=self.fixture();b['editor']['length']=1;self.assertFalse(retry_unseen_tap(a,b))
 def test_send_or_save_effect_never_replays(self):
  for field in ('draft_length','reply_actions','provider_workspace','transfer_data'):
   a,b=self.fixture();b[field]={'changed':True};self.assertFalse(retry_unseen_tap(a,b))
 def test_cancelled_down_needs_explicit_kernel_resync_permission(self):
  a,b=self.fixture();b['touch']={'gesture':8,'pressed':True}
  self.assertFalse(retry_unseen_tap(a,b));self.assertTrue(retry_unseen_tap(a,b,allow_cancelled_down=True))
  b['editor']['cursor']=1;self.assertFalse(retry_unseen_tap(a,b,allow_cancelled_down=True))
 def test_missing_old_or_explicit_focus_loss_never_replays(self):
  a,b=self.fixture();b['input_focus_trace']={};self.assertFalse(retry_unseen_tap(a,b))
  a,b=self.fixture();b['input_focus_trace']['entries'][0]['time']=99;self.assertFalse(retry_unseen_tap(a,b))
  a,b=self.fixture();b['input_focus_trace']['entries'].append({'time':500,'reason':'input_focus_applied','event_value':False});self.assertFalse(retry_unseen_tap(a,b))
if __name__=='__main__':unittest.main()
