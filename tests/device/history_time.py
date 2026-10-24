#!/usr/bin/env python3
"""ORANGE history time priority and full-title checks in isolated user data."""
import pathlib,shutil,sys
import acceptance as d
import settings_acceptance as isolated
from chat_acceptance import push
sys.path.insert(0,str(d.ROOT/'tests'))
from test_history_time import seed_history

def fixture():
    p=d.ROOT/'.work/device-history-time'
    if p.exists():shutil.rmtree(p)
    p.mkdir();seed_history(p,1);push(p/'penchat.db',d.DATA+'/penchat.db')

def choose(key):
    d.to_chat();d.open_settings();d.setting('history_time');d.wait(lambda:d.state()['modal'])
    d.capture('history-time-picker')
    width=(d.state()['width']-162)//3;index={'time':0,'title':1,'off':2}[key]
    d.tap(100+index*(width+12)+width//2,90)
    d.wait(lambda:not d.state()['modal']);d.to_chat();d.open_navigation()

def exercise():
    d.open_navigation();s=d.state();assert s['history_captions']['today']['time']
    assert s['history_captions']['missing']['time']==''
    assert s['history_captions']['year']['time'].count('年')==1
    d.capture('history-time-priority')
    row=next(r for r in s['history_rows'] if r['id']=='today')
    d.adb('/tmp/penchat-ui-tap --hold '+str(row['y']+113)+' '+str(959-row['x'])+' 650 2>/dev/null')
    d.wait(lambda:d.state()['history_context']=='today')
    assert d.state()['history_details']['title_length']>100
    assert len(d.state()['history_actions'])==5
    d.capture('history-time-static-details');d.tap(950,256)
    choose('title');assert not d.state()['history_captions']['today']['time'];d.capture('history-title-priority')
    choose('off');assert all(not c['time'] for c in d.state()['history_captions'].values());d.capture('history-time-off')
    choose('time');assert d.state()['history_captions']['today']['time']
    d.to_chat()

if __name__=='__main__':
    isolated.fixture=fixture;isolated.exercise=exercise
    isolated.main({'history_time_modes':3,'full_static_title':True,'missing_time_hidden':True,'private_public_files_restored':True,'host_unchanged':True})
