"""Offline real-touch gated stream acceptance, always under directory isolation."""
import json,sqlite3,subprocess,sys,time
from http_pool_flow import DeviceServer

def prepare(root,folder,data,serial):
    import shutil
    sys.path.insert(0,str(root/'tests'));from test_reasoning_ui import seed
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir();server=DeviceServer(root,serial,folder.parent,stream=True);seed(folder,server.server_port)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute('update providers set base_url=?,metadata_json=?',(f'http://127.0.0.1:{server.server_port}/v1',json.dumps({'api':'chat' if serial.endswith('113') else 'responses'})))
        for key in ['auto_title_enabled','auto_context_compression']:db.execute('insert or replace into app_settings values(?,?,1)',(key,'false'))
    return server,server,[(folder/'penchat.db',data+'/penchat.db')]

def run(c,server):
    state,tap,wait=c['state'],c['tap'],c['wait'];W,H=c['W'],c['H']
    def key(k):
        item=next(x for x in state()['editor']['keys'] if x['key']==k);tap(item['x'],item['y'])
    def quiescent():wait(lambda:not state()['reader_interacting'] and not state()['chat_render_pending'])
    def bottom():
        s=state();assert s['to_bottom_visible']
        tap(s['to_bottom_x']+22,s['to_bottom_y']+22)
        wait(lambda:state()['following_latest'] and state()['at_latest_bottom'] and not state()['to_bottom_visible'])
    def hold_drag():
        x,y=c['raw'](W//2,int(H*.14));ex,ey=c['raw'](W//2,int(H*.68))
        command=f'PENCHAT_TOUCH_DEVICE=/dev/input/{c["event"]} /tmp/penchat-display-tap --drag-hold {x} {y} {ex} {ey} 450 3500'
        p=subprocess.Popen(['adb','-s',c['SERIAL'],'shell',command],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        wait(lambda:state()['reader_interacting'] and not state()['following_latest'])
        return p
    tap(W//2,H-34);wait(lambda:state()['editor']['active'])
    if state()['editor']['layout']!='en26':key('language')
    key('q');b=state()['editor']['actions']['done'];tap((b[0]+b[2])//2,(b[1]+b[3])//2);wait(lambda:not state()['editor']['active'])
    tap(W-52,H-34);wait(lambda:state()['receiving_reply'] and state()['output_length']>100)
    quiescent();s=state();id=next(iter(s['reply_actions']['rows']))
    assert not s['generation_hint_present']
    assert not s['reply_actions']['rows'][id]['visible'] and s['following_latest'] and not s['to_bottom_visible']
    c['capture']('stream-following-hidden-actions')
    p=hold_drag()
    before=state();length=before['output_length'];maximum=before['scroll_y']+before['scroll_bottom']
    server.command('more');wait(lambda:state()['output_length']>length);time.sleep(.15)
    s=state();assert s['reader_interacting'] and s['chat_render_pending'] and s['scroll_y']+s['scroll_bottom']==maximum
    assert p.wait(timeout=6)==0;quiescent();s=state();assert s['to_bottom_visible'] and not s['following_latest']
    c['capture']('stream-user-reading')
    bottom();length=state()['output_length'];server.command('more');wait(lambda:state()['output_length']>length);quiescent()
    assert state()['at_latest_bottom'] and state()['following_latest'] and not state()['to_bottom_visible']
    c['capture']('stream-follow-resumed')
    p=hold_drag();server.command('finish');wait(lambda:not state()['generating'])
    assert state()['reader_interacting'] and state()['chat_render_pending']
    assert p.wait(timeout=6)==0;quiescent();s=state()
    assert s['reply_actions']['rows'][id]['visible'] and s['to_bottom_visible'] and not s['following_latest']
    assert not s['generation_hint_present']
    scroll=s['scroll_y'];time.sleep(.2);assert state()['scroll_y']==scroll
    c['capture']('stream-completed-reading')
    bottom();c['capture']('stream-completed-bottom')
    assert len(server.requests)==1
    c['result']['stream_reading']={'hidden_current_actions':True,'drag_not_interrupted':True,'coalesced_during_touch':True,'resume_follow':True,'completion_during_touch':True,'completed_return_to_bottom':True,'single_request':True}
