#!/usr/bin/env python3
"""Headless balance UI: fake transport only, never uses real provider credentials."""
import json, os, pathlib, sqlite3, tempfile, time
from test_lvgl_app import Native, ROOT, wait_for
from image_gestures import capture
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
OUT=ROOT/'.work/balance-ui';OUT.mkdir(parents=True,exist_ok=True)

def setup(folder, lines=80):
    schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
    with sqlite3.connect(folder/'penchat.db') as db:
        db.executescript(schema)
        for i,(vendor,host) in enumerate([('deepseek','api.deepseek.com'),('kimi','api.moonshot.cn'),('siliconflow','api.siliconflow.cn'),('302ai','api.302.ai')]):
            meta={'api':'chat','balance_vendor':vendor}
            db.execute("insert into providers(id,name,type,base_url,api_key,metadata_json,created_at,updated_at) values(?,?,'openai-compatible',?,'FAKE-KEY-DO-NOT-LOG',?,1,1)",(vendor,'测试账户 '+str(i+1),'https://'+host+'/v1',json.dumps(meta)))
        db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('conversation','Reading position','active',1,1,1)")
        db.execute("insert into app_settings(key,value_json,updated_at) values('active_session_id','\"conversation\"',1)")
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('a','conversation',1,'assistant','complete','text',?,1,1)",('长回复用于保持阅读位置。\n\n'*lines,))
    (folder/'.updates').mkdir()
    identity=json.loads((ROOT/'package.json').read_text())
    (folder/'.updates/restore.json').write_text(json.dumps({'appid':identity['appid'],'target':identity['version'],'snapshot':{'session':'conversation','draft':'余额查询不应改变这段草稿','preferences':{},'scroll':0,'offset':0,'following':True}}))
    fixtures={
        'deepseek':{'status':200,'body':json.dumps({'balance_infos':[{'currency':'CNY','total_balance':'0.000123','granted_balance':'0','topped_up_balance':'0.000123'},{'currency':'USD','total_balance':'-0.12'}]})},
        'kimi':{'status':200,'body':'{"status":true,"data":{"available_balance":12.34567,"cash_balance":10,"voucher_balance":2.34567}}'},
        'siliconflow':{'status':200,'body':json.dumps({'code':20000,'data':{'status':'normal','totalBalance':'88.88','balance':'0.88','chargeBalance':'88'}})},
        '302ai':{'status':403,'body':'DO NOT EXPOSE THIS BODY'}
    }
    file=folder/'balances-fixture.json';file.write_text(json.dumps(fixtures))
    return file,fixtures

def open_drawer(a):
    a.open_navigation()
    assert a.diag()['navigation_open']

def open_balances(a):
    d=a.diag()
    if d['height']>d['width']:open_drawer(a)
    b=a.diag()['balances']['shortcut'];assert b and b[2]-b[0]+1==48 and b[3]-b[1]+1==48
    d=a.diag();capture(a,OUT/f"{d['width']}x{d['height']}-entry.png")
    if d['width']>d['height']:
        assert b[0]==16 and b[1]==d['height']-56
        hidden=d['composer_shortcut']['bounds'];assert hidden[3]<b[1] or hidden[0]>b[2]
    a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    wait_for(lambda:a.diag()['balances']['open'] and a.diag()['balances']['pending']==0)
    wait_for(lambda:len(a.diag()['balances']['states'])==4)
    assert a.diag()['view']=='chat' and a.diag()['navigation_open']==(d['height']>d['width'])
    if d['height']>d['width']:
        b=a.diag()['balances']['bounds'];assert b[0]<d['width']//4 and b[2]<d['width']*2//3
    b=a.diag()['balances']['bounds'];assert (b[2]-b[0]+1)*(b[3]-b[1]+1)<d['width']*d['height']*.75

def popup_action(a,key):
    for _ in range(50):
        d=a.diag()['balances'];b=d['actions'].get(key);v=d['list']
        if not b:time.sleep(.12);continue
        if key=='refresh' or (b[1]>=v[1] and b[3]<=v[3]):
            a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);return
        start=v[1]+(v[3]-v[1])*3//4 if b[3]>v[3] else v[1]+(v[3]-v[1])//4
        delta=min(90,(v[3]-v[1])//2)*(-1 if b[3]>v[3] else 1);x=(v[0]+v[2])//2
        for i in range(7):a.send('touch',x=x,y=start+delta*i//6,pressed=True);time.sleep(.025)
        time.sleep(.15);a.send('touch',x=x,y=start+delta,pressed=False);time.sleep(.15)
    raise AssertionError(('Balance control unreachable',key,b,v))

for width,height in [(960,266),(640,260),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-balance-ui-') as temp:
        folder=pathlib.Path(temp);fixture,fixtures=setup(folder)
        os.environ['PENCHAT_TEST_BALANCE_FIXTURE']=str(fixture)
        if height>width:os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
        a=Native(BINARY,folder,seed_model=False,screen={'width':width,'height':height,'direction':0,'xoffset':0,'yoffset':0})
        try:
            before=a.diag();time.sleep(.35);capture(a,OUT/f'{width}x{height}-before.png');open_balances(a)
            state=a.diag();assert state['balances']['states']=={'deepseek':'ready','kimi':'ready','siliconflow':'ready','302ai':'error'}
            capture(a,OUT/f'{width}x{height}-balances.png')
            popup_action(a,'detail:deepseek');capture(a,OUT/f'{width}x{height}-details.png')
            assert set(k for k in a.diag()['balances']['actions'] if not k.startswith('detail:'))=={'refresh'}
            # One Refresh submits all configured providers; both changed fixtures must update.
            fixtures['deepseek']={'status':503,'body':'PRIVATE SERVER ERROR'}
            fixtures['kimi']={'status':401,'body':'DO NOT EXPOSE'};fixture.write_text(json.dumps(fixtures))
            popup_action(a,'refresh');wait_for(lambda:a.diag()['balances']['pending']==0)
            wait_for(lambda:a.diag()['balances']['states']['deepseek']=='error' and a.diag()['balances']['states']['kimi']=='error')
            capture(a,OUT/f'{width}x{height}-stale.png')
            assert '0.000123' not in json.dumps(a.diag()['balances'])
            assert 'FAKE-KEY' not in json.dumps(a.diag())
            a.tap(2,height//2);wait_for(lambda:not a.diag()['balances']['open'])
            after=a.diag();assert after['navigation_open']==(height>width)
            assert after['session']==before['session'] and after['draft_length']==before['draft_length']
            assert abs(after['scroll_y']-before['scroll_y'])<=10 and a.diag()['view']=='chat'
            fixtures['deepseek']={'status':200,'body':'{"balance_infos":[{"currency":"CNY","total_balance":"91.23"}]}'}
            fixture.write_text(json.dumps(fixtures));open_balances(a)
            assert a.diag()['balances']['states']['deepseek']=='ready'
            a.tap(2,height//2);wait_for(lambda:not a.diag()['balances']['open'])
            print('Balance popup passed:',width,height,flush=True)
        finally:
            a.close();os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
os.environ.pop('PENCHAT_TEST_BALANCE_FIXTURE',None)
print('Balance popups in three layouts: placement, local scroll, all-provider refresh and chat preservation passed')
