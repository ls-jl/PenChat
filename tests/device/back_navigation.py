#!/usr/bin/env python3
"""Real ORANGE edge swipes; restore all private/public user files afterwards."""
import time
import acceptance as d
import settings_acceptance as isolated
from image_viewer import fixture,open_photo

def swipe(x=20,y=120,end=160):
    d.adb('/tmp/penchat-ui-tap --drag-hold '+str(y+113)+' '+str(959-x)+' '+str(y+113)+' '+str(959-end)+' 450 100 2>/dev/null')
    time.sleep(.15)

def blocked(value):
    d.wait(lambda:d.state()['back_navigation']['policy_ok'] and d.state()['back_navigation']['blocked']==value)

def exercise():
    native=d.pid()
    d.open_settings();blocked(True);d.capture('back-settings')
    d.setting('models');blocked(True);swipe()
    s=d.state();print('child swipe',s['view'],s['touch'],s['back_navigation'],flush=True)
    d.capture('back-after-child-swipe');d.view('settings');assert d.pid()==native
    swipe(end=25);assert d.state()['view']=='settings' # stays below firmware slop
    swipe();d.wait(lambda:d.state()['navigation_open']);assert d.pid()==native
    # The history drawer still closes only through a backdrop tap.
    blocked(True);swipe(x=780,end=920);assert d.state()['navigation_open']
    d.tap(850,80);d.wait(lambda:not d.state()['navigation_open']);blocked(False)
    before=open_photo();blocked(True);d.capture('back-image-viewer')
    swipe(x=300,end=480);assert d.state()['image_viewer']['open']
    swipe();d.wait(lambda:not d.state()['image_viewer']['open']);blocked(False)
    assert d.state()['session']==before['session'] and d.pid()==native
    state=d.state()
    if state['to_bottom_visible']:d.tap(state['to_bottom_x']+22,state['to_bottom_y']+22)
    d.wait(lambda:d.state()['toolbar_y']==d.state()['height']-58)
    d.tap(500,232);d.wait(lambda:d.state()['editor']['active']);blocked(True)
    swipe();d.wait(lambda:not d.state()['editor']['active']);blocked(False)
    assert d.pid()==native and d.state()['draft_length']==0
    for entry,view in ((38,'camera'),(82,'album')):
        d.tap(128,232);d.tap(200,entry);d.view(view);blocked(True)
        if view=='camera':d.wait(lambda:d.state()['camera_ready'])
        d.capture('back-'+view);swipe();d.view('chat');blocked(False)
        assert d.pid()==native
    # At the true root Falcon retains its own exit behavior.
    swipe()
    d.wait(lambda:not d.pid() or not d.state()['visible'])
    d.adb('miniapp_cli start '+d.APP_ID);d.wait(d.pid);d.wait(lambda:d.state()['visible']);time.sleep(.7)
    d.capture('back-root-restored')

if __name__=='__main__':
    isolated.fixture=fixture;isolated.exercise=exercise
    isolated.main({'nested_swipes':True,'editor_viewer_return':True,'drawer_tap_only':True,
                   'camera_album_return':True,'root_system_exit':True,'user_files_restored':True,'host_unchanged':True})
