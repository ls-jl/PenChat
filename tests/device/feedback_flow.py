"""Read-only hardware inventory and explicit key capture. Never submits a report."""
import json,subprocess,time

def run(c):
    read,wait,tap,swipe,shell=c['state'],c['wait'],c['tap'],c['settings_swipe'],c['shell']
    W,H=c['W'],c['H'];capture=c['capture'];data=c['DATA']
    def state():return read()
    def go(key):
        for _ in range(35):
            d=state();b=d['feedback']['actions'][key]
            if d['page']['scrolling']:time.sleep(.1);continue
            if b[1]>=0 and b[3]<H:
                tap((b[0]+b[2])//2,(b[1]+b[3])//2);time.sleep(.15);return
            swipe(120 if b[1]<0 else -120);time.sleep(.15)
        raise AssertionError(key)
    def draft():return json.loads(shell('cat '+data+'/.feedback/draft.json'))
    def steps():return draft().get('steps',{})
    def rows(name):
        for _ in range(35):
            d=read();b=d['settings']['rows'][name]['bounds']
            if d['settings']['scrolling']:time.sleep(.1);continue
            if b[1]>=0 and b[3]<H:return b
            swipe(120 if b[1]<0 else -120);time.sleep(.15)
        raise AssertionError(name)
    tap(40,32);wait(lambda:read()['navigation_open']);tap(W*2//3-130,H-30);wait(lambda:read()['view']=='settings')
    b=rows('feedback');before=read()['scroll_y'];tap((b[0]+b[2])//2,(b[1]+b[3])//2);wait(lambda:read()['view']=='feedback')
    capture('feedback-form')
    # Switch is row-wide; no recording begins just by checking it.
    tap(W-55,152);wait(lambda:draft().get('adapt'));assert not read()['feedback']['collecting']
    go('adapt');capture('feedback-adapt');go('start_scan');wait(lambda:steps().get('scan',{}).get('status')=='listening')
    # Device helper only synthesizes the authorized hardware button pair. The
    # application collector itself is read-only and discovers its event node.
    output=shell('/tmp/penchat-display-key /dev/input/by-path/led_control 134 1 120 150')
    assert 'sent 1 paired events' in output,output
    try:wait(lambda:steps().get('scan',{}).get('status') in ('complete','cancelled','release_missing'))
    finally:(c['OUT']/'feedback-capture-fixture.json').write_text(json.dumps(draft(),ensure_ascii=False,indent=2))
    assert any(x['code']==134 and x['value']==1 for x in steps()['scan']['events'])
    scan_status=steps()['scan']['status']
    if not read()['visible']:
        shell('miniapp_cli start '+c['APP']['appid']);wait(lambda:read()['visible']);time.sleep(.5)
    go('start_home');wait(lambda:steps().get('home',{}).get('status')=='listening')
    output=shell('/tmp/penchat-display-key /dev/input/by-path/gpio-keys 102 1 100 150')
    assert 'sent 1 paired events' in output,output
    # Home must actually navigate to the system UI, without stopping the
    # explicit Home observer. Restart is a foreground navigation, not a host restart.
    # Some firmware freezes the hidden native main thread, so requesting a
    # fresh UI diagnostic before returning can itself wait forever. Capture
    # system Home directly, then foreground and inspect persisted key data.
    time.sleep(.3)
    shell('miniapp_cli capture /tmp/penchat-feedback-home.png')
    subprocess.run(['adb','-s',c['SERIAL'],'pull','/tmp/penchat-feedback-home.png',str(c['OUT']/'feedback-system-home.png')],check=True,capture_output=True)
    shell('miniapp_cli start '+c['APP']['appid']);wait(lambda:read()['visible']);time.sleep(.5)
    wait(lambda:steps().get('home',{}).get('status') in ('complete','release_missing'))
    (c['OUT']/'feedback-capture-fixture.json').write_text(json.dumps(draft(),ensure_ascii=False,indent=2))
    assert any(x['code']==102 and x['value']==1 for x in steps()['home']['events'])
    shell('rm -f /tmp/penchat-feedback-home.png')
    capture('feedback-home-return')
    if read()['view']=='chat':
        tap(40,32);wait(lambda:read()['navigation_open']);tap(W*2//3-130,H-30);wait(lambda:read()['view']=='settings')
        b=rows('feedback');before=read()['scroll_y'];tap((b[0]+b[2])//2,(b[1]+b[3])//2);wait(lambda:read()['view']=='feedback')
    if read()['feedback']['screen']=='form':go('adapt')
    capture('feedback-resumed-review')
    go('review');go('preview');wait(lambda:read()['feedback']['screen']=='preview');capture('feedback-preview')
    assert not list(filter(None,shell('find '+data+'/.feedback/reports -type f').splitlines()))
    tap(40,32);wait(lambda:read()['feedback']['screen']=='form')
    go('adapt');go('start_scan');wait(lambda:read()['feedback']['collecting']);go('stop_keys');wait(lambda:not read()['feedback']['collecting'])
    native=c['pid']()
    remaining=shell('ls -l /proc/'+native+'/fd')
    # Native touch may own one input FD; no extra scan/Home observer remains.
    assert remaining.count(' -> /dev/input/')<=1,remaining
    tap(40,32);tap(40,32);wait(lambda:read()['view']=='settings');assert abs(read()['scroll_y']-before)<=2
    c['result']['feedback']={'read_only':True,'scan_code':134,'home_code':102,'home_background_return':True,
        'scan_result':scan_status,'no_upload':True,'listeners_closed':True,'draft_saved':True,'screen_from_falcon':True}
