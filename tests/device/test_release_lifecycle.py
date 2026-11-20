"""Host-only lifecycle race: a finishing UI cannot answer diagnostics."""
import unittest
from unittest.mock import patch
import release_lifecycle as lifecycle

class FinishRace(unittest.TestCase):
 def test_root_finish_waits_for_reap_without_querying_stopped_ui(self):
  closing=False;polls=0
  def pid():
   nonlocal polls
   if closing:polls+=1
   return '123' if not closing or polls<3 else ''
  def touch(*args):
   nonlocal closing
   closing=True
  def state():
   self.assertFalse(closing,'Exiting UI must not receive diagnostic request')
   return {'visible':True,'page_visible':True,'window_focused':True,'input_focused':True,'touch_input':{'active':True},'editor':{},'back_navigation':{'root':True,'edge':44},'view':'chat'}
  with patch.object(lifecycle,'_context',return_value={'screen':{'width':640,'height':260}}),patch.object(lifecycle,'_touch',side_effect=touch),patch.object(lifecycle,'free_back_start',return_value=(43,70)):
   result=lifecycle.close_app('JDA0700006552216','8001792332371649',lambda _: '8001792332371649',pid,state)
  self.assertTrue(result['normal_finish']);self.assertFalse(result['forced_term'])
if __name__=='__main__':unittest.main()
