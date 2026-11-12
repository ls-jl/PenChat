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
            db.execute("insert into providers(id,name,type,base_url,api_key,metadata_json,created_at,updated_at) values(?,?,'openai-compatible',?,'FAKE-KEY-DO-NOT-LOG',?,1,1)",(vendor,'测试服务商 '+vendor,'https://'+host+'/v1',json.dumps(meta)))
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
    a.to_chat();d=a.diag();a.tap(48,48) if d['height']>d['width'] else a.tap(40,32)
    assert a.diag()['navigation_open']

def open_balances(a):
    open_drawer(a);b=a.diag()['balances']['shortcut'];assert b and b[2]-b[0]+1>=48 and b[3]-b[1]+1>=48
    d=a.diag();capture(a,OUT/f"{d['width']}x{d['height']}-drawer.png")
    a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    wait_for(lambda:a.diag()['view']=='balances' and a.diag()['balances']['pending']==0)
    wait_for(lambda:len(a.diag()['balances']['states'])==4)

for width,height in [(960,266),(640,260),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-balance-ui-') as temp:
        folder=pathlib.Path(temp);fixture,fixtures=setup(folder)
        os.environ['PENCHAT_TEST_BALANCE_FIXTURE']=str(fixture)
        a=Native(BINARY,folder,seed_model=False,screen={'width':width,'height':height,'direction':0,'xoffset':0,'yoffset':0})
        try:
            before=a.diag();time.sleep(.35);capture(a,OUT/f'{width}x{height}-before.png');open_balances(a)
            state=a.diag();assert state['balances']['states']=={'deepseek':'ready','kimi':'ready','siliconflow':'ready','302ai':'error'}
            capture(a,OUT/f'{width}x{height}-balances.png')
            a.page_action('balance-detail:deepseek');capture(a,OUT/f'{width}x{height}-details.png')
            # A failed refresh retains the last balance; diagnostic output never includes amounts or credentials.
            fixtures['deepseek']={'status':503,'body':'PRIVATE SERVER ERROR'};fixture.write_text(json.dumps(fixtures))
            a.page_action('balance-refresh:deepseek');wait_for(lambda:a.diag()['balances']['states']['deepseek']=='error')
            capture(a,OUT/f'{width}x{height}-stale.png')
            assert '0.000123' not in json.dumps(a.diag()['balances'])
            assert 'FAKE-KEY' not in json.dumps(a.diag())
            a.to_chat();assert a.diag()['navigation_open'] is False
            after=a.diag();assert after['session']==before['session'] and after['draft_length']==before['draft_length']
            assert abs(after['scroll_y']-before['scroll_y'])<=10
            # Reopening performs a new query even after a recent success/error.
            fixtures['deepseek']['status']=200;fixtures['deepseek']['body']='{"balance_infos":[{"currency":"CNY","total_balance":"91.23"}]}'
            fixture.write_text(json.dumps(fixtures));open_balances(a)
            assert a.diag()['balances']['states']['deepseek']=='ready'
            a.to_chat();open_drawer(a);d=a.diag();a.tap(d['width']*2//3-70,d['height']-30 if d['width']>d['height'] else d['height']-48);a.setting('models')
            a.page_action('provider-edit:deepseek');a.page_action('balance_vendor')
            capture(a,OUT/f'{width}x{height}-vendor-picker.png')
        finally:a.close()
os.environ.pop('PENCHAT_TEST_BALANCE_FIXTURE',None)
print('Balance UI fixtures passed in three layouts; no real API requests')
