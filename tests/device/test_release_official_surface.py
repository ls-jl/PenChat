import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch
from release_official_surface import official_visual
from release_official_readback import owned_fullscreen


class OfficialObservationTest(unittest.TestCase):
  def device(self, docked=False, page_visible=False, panel=False, official=True):
    state = {'keyboard': True, 'page_visible': page_visible,
      'editor': {'active': True, 'official': official, 'docked': docked},
      'system_overlay': {'app_focused': False, 'panel_visible': panel}}
    return SimpleNamespace(official_readback_probe=True, state=lambda:state,
      system_capture=Mock(return_value='sdk'))

  def test_hidden_fullscreen_uses_sdk_without_weakening_readback(self):
    d=self.device()
    with patch('release_official_readback.capture') as raw:
      self.assertEqual(official_visual(d,'owned-fullscreen'),'sdk')
      raw.assert_not_called()

  def test_retained_field_still_requires_primary_ownership(self):
    d=self.device(docked=True,page_visible=True)
    with patch('release_official_readback.capture',return_value='raw') as raw:
      self.assertEqual(official_visual(d,'retained'),'raw')
      raw.assert_called_once_with(d,'retained')
      d.system_capture.assert_not_called()

  def test_panel_or_unowned_request_does_not_bypass_readback_guard(self):
    for d in [self.device(panel=True),self.device(official=False)]:
      with patch('release_official_readback.capture',side_effect=AssertionError('ownership')):
        with self.assertRaises(AssertionError):official_visual(d,'rejected')
        d.system_capture.assert_not_called()

  def test_explicit_fullscreen_owner_matches_process_and_edit_session(self):
    d=self.device();s=d.state();s['pid']=123;s['editor']['session']='owned-edit'
    owner={'pid':'123','session':'owned-edit'}
    self.assertTrue(owned_fullscreen(s,owner))
    self.assertFalse(owned_fullscreen(s,{'pid':'124','session':'owned-edit'}))
    self.assertFalse(owned_fullscreen(s,{'pid':'123','session':'stale-edit'}))
    s['editor']['session']=''
    self.assertFalse(owned_fullscreen(s,{'pid':'123','session':''}))
    s['editor']['session']='owned-edit'
    s['system_overlay']['panel_visible']=True
    self.assertFalse(owned_fullscreen(s,owner))

  def test_matching_fullscreen_owner_uses_fresh_primary_readback(self):
    d=self.device();s=d.state();s['pid']=123;s['editor']['session']='owned-edit'
    d.owned_official_request={'pid':'123','session':'owned-edit'}
    with patch('release_official_readback.capture',return_value='raw') as raw:
      self.assertEqual(official_visual(d,'owned-fullscreen'),'raw')
      raw.assert_called_once_with(d,'owned-fullscreen')
      d.system_capture.assert_not_called()


if __name__=='__main__':unittest.main()
