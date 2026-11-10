#!/usr/bin/env python3
"""Provider changes and additive model CRUD with the actual LVGL process."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as ui
from test_reasoning_ui import seed
from provider_ui import activate,enter,field,back,capture
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ui.ROOT/'build/host/penchat-lvgl'))
OUT=ui.ROOT/'.work/providers';OUT.mkdir(parents=True,exist_ok=True)
class Models(http.server.BaseHTTPRequestHandler):
    requests=[]
    ports=[]
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def do_GET(self):
        type(self).requests.append(self.path)
        type(self).ports.append(self.client_address[1])
        payload=json.dumps({'data':[{'id':n} for n in ['a','b','new-model','long-provider/model-with-a-very-long-version-and-capability-name-2026-11-10']]}).encode()
        self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)


def run(width,height,port):
    with tempfile.TemporaryDirectory(prefix='penchat-provider-ui-') as folder:
        p=pathlib.Path(folder);seed(p,port);ui.port=port
        def rows(sql,args=()):
            with sqlite3.connect(p/'penchat.db') as db:return db.execute(sql,args).fetchall()
        provider=rows('select provider_id from model_configs where id=?',('a',))[0][0]
        before=rows('select id,name,capabilities_json from model_configs order by id')
        app=ui.Native(BINARY,p,screen={'width':width,'height':height,'direction':0})
        try:
            app.open_settings();app.setting('models');capture(app,OUT,f'{width}-list-after')
            activate(app,'provider-edit:'+provider);capture(app,OUT,f'{width}-connection-after')
            b=app.diag()['page']['actions']['field:baseURL'];assert b[2]-b[0]+1>=44
            if height>width or width<height*3:assert b[2]-b[0]+1>=width//2
            base=f'http://127.0.0.1:{port}/custom/v1'
            field(app,'baseURL',base);field(app,'apiKey','changed-fake-only')
            activate(app,'provider_advanced');activate(app,'api_compatibility')
            capture(app,OUT,f'{width}-compatibility')
            back(app)
            activate(app,'保存');assert app.diag()['view']=='provider'
            assert rows('select id,name,capabilities_json from model_configs order by id')==before
            assert rows('select base_url,api_key from providers where id=?',(provider,))[0]==(base,'changed-fake-only')
            activate(app,'provider-add:'+provider);assert app.diag()['view']=='provider_models'
            requests=len(Models.requests);activate(app,'models-fetch')
            ui.wait_for(lambda:'model-option:new-model' in app.diag()['page']['actions'])
            assert len(Models.requests)==requests+1 and Models.requests[-1]=='/custom/v1/models'
            capture(app,OUT,f'{width}-choose-models')
            activate(app,'model-option:new-model')
            activate(app,'models-manual');enter(app,'manual-added')
            activate(app,'models-fetch')
            ui.wait_for(lambda:'model-option:manual-added' in app.diag()['page']['actions'])
            assert rows('select count(*) from model_configs')[0][0]==2
            activate(app,'models-filter');enter(app,'new-')
            assert 'model-option:new-model' in app.diag()['page']['actions']
            assert 'model-option:a' not in app.diag()['page']['actions']
            activate(app,'添加选中')
            assert rows('select name from model_configs where enabled=1 order by name')==[('a',),('b',),('manual-added',),('new-model',)]
            assert rows('select id from model_configs where is_default=1')[0][0]=='a'
            # Adding only models must keep this Provider's existing HTTP pool.
            previous_port=Models.ports[-1]
            activate(app,'provider-add:'+provider);activate(app,'models-fetch')
            ui.wait_for(lambda:'model-option:new-model' in app.diag()['page']['actions'])
            assert Models.ports[-1]==previous_port,(previous_port,Models.ports)
            back(app)
            mid=rows("select id from model_configs where name='new-model'")[0][0]
            original=json.loads(rows('select capabilities_json from model_configs where id=?',(mid,))[0][0])
            activate(app,'model-edit:'+mid);capture(app,OUT,f'{width}-model-edit')
            fields=app.diag()['settings']['rows'];a=fields['model_identity']['bounds'];b=fields['model_display_name']['bounds']
            if height>width or width<height*3:assert a[0]==b[0] and b[1]>a[3],(a,b)
            else:assert a[1]==b[1] and b[0]>a[2],(a,b)
            activate(app,'model_identity');enter(app,'discarded-change');back(app)
            assert rows('select name from model_configs where id=?',(mid,))[0][0]=='new-model'
            activate(app,'model-edit:'+mid);activate(app,'model_identity');enter(app,'new-renamed')
            activate(app,'model_display_name');enter(app,'Friendly name');activate(app,'保存')
            assert rows('select name,display_name from model_configs where id=?',(mid,))[0]==('new-renamed','Friendly name')
            saved=json.loads(rows('select capabilities_json from model_configs where id=?',(mid,))[0][0]);assert saved['reasoning']==original['reasoning']
            activate(app,'model-use:'+mid);assert rows('select id from model_configs where is_default=1')[0][0]==mid
            activate(app,'model-delete:'+mid);assert app.diag()['modal'];capture(app,OUT,f'{width}-delete-confirm')
            app.tap(width-74,height-44);assert rows('select enabled from model_configs where id=?',(mid,))[0][0]==1
            activate(app,'model-delete:'+mid);app.tap((12 if height>width else 84)+62,height-(96 if height>width else 44))
            assert rows('select enabled from model_configs where id=?',(mid,))[0][0]==0
            activate(app,'provider-add:'+provider);activate(app,'models-manual');enter(app,'new-renamed');activate(app,'添加选中')
            restored=rows("select id,display_name,capabilities_json from model_configs where name='new-renamed'")[0]
            assert restored[:2]==(mid,'Friendly name') and json.loads(restored[2])==saved
            activate(app,'添加 Provider');field(app,'name','Another provider');field(app,'apiKey','fixture-key');field(app,'baseURL',base)
            activate(app,'provider-protocol');capture(app,OUT,f'{width}-protocols')
            app.tap(220,160);assert app.diag()['provider_protocol']=='anthropic'
            activate(app,'provider_advanced');activate(app,'anthropic_connection');capture(app,OUT,f'{width}-messages-connection');back(app)
            activate(app,'保存');assert app.diag()['view']=='provider_models';capture(app,OUT,f'{width}-empty-provider')
            other=rows("select id from providers where name='Another provider'")[0][0]
            assert rows('select count(*) from model_configs where provider_id=?',(other,))[0][0]==0
            back(app);assert app.diag()['view']=='provider';assert 'provider-add:'+other in app.diag()['page']['actions']
            print('Provider connection/model CRUD and layout:',width,height,'passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Models) as server:
        threading.Thread(target=server.serve_forever,daemon=True).start()
        for size in [(640,360),(960,266),(568,1210)]:run(*size,server.server_port)
