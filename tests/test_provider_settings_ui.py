#!/usr/bin/env python3
"""Provider-only index and transactional connection/model workspace in real LVGL."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as ui
from test_reasoning_ui import seed
from provider_ui import activate,enter,field,capture
from choice_ui import select,dismiss
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ui.ROOT/'build/host/penchat-lvgl'))
OUT=ui.ROOT/'.work/providers';OUT.mkdir(parents=True,exist_ok=True)
class Models(http.server.BaseHTTPRequestHandler):
    requests=[]
    delay=0
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def do_GET(self):
        type(self).requests.append((self.path,self.headers.get('Authorization')))
        time.sleep(type(self).delay)
        payload=json.dumps({'data':[{'id':n} for n in ['a','b','new-model','long-provider/model-with-a-very-long-version-and-capability-name-2026-11-10']]}).encode()
        try:
            self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
        except (BrokenPipeError,ConnectionResetError):pass

def back(a):
    d=a.diag()
    if d['height']>d['width']:
        for _ in range(30):
            if a.diag()['scroll_y']<=0:break
            a.settings_swipe(min(500,d['height']//2));time.sleep(.12)
        a.tap(48,48)
    else:a.tap(40,32)

def run(w,h,port):
    with tempfile.TemporaryDirectory(prefix='penchat-provider-workspace-') as folder:
        p=pathlib.Path(folder);seed(p,port);ui.port=port
        def rows(sql,args=()):
            with sqlite3.connect(p/'penchat.db') as db:return db.execute(sql,args).fetchall()
        def business():return [rows('select * from '+t+' order by id') for t in ['providers','model_configs']]
        provider='p';a=ui.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
        def models():return a.diag()['provider_workspace']['models']
        def mid(name):return next(m['id'] for m in models() if m['name']==name)
        try:
            a.open_settings();a.setting('models');d=a.diag()
            assert 'provider-open:p' in d['page']['actions']
            assert not any(k.startswith(('model-','provider-add:','provider-edit:')) for k in d['page']['actions'])
            capture(a,OUT,f'{w}-provider-index')
            activate(a,'provider-open:p');before=business();capture(a,OUT,f'{w}-workspace-top')
            # One final whole-page Save covers both connection and model drafts.
            actions=a.diag()['page']['actions']
            assert 'provider-save-bottom' in actions and '保存' not in actions
            assert not any(k.startswith('model-use:') for k in a.diag()['page']['actions'])
            base=f'http://127.0.0.1:{port}/edited/v1'
            field(a,'baseURL',base);field(a,'apiKey','changed-fake-only')
            activate(a,'provider-protocol');select(a,'chat')
            assert business()==before and a.diag()['provider_workspace']['dirty']
            activate(a,'provider-add-models');n=len(Models.requests);activate(a,'models-fetch')
            ui.wait_for(lambda:'model-option:new-model' in a.diag()['page']['actions'])
            assert len(Models.requests)==n+1 and Models.requests[-1]==('/edited/v1/models','Bearer changed-fake-only')
            capture(a,OUT,f'{w}-model-selector')
            activate(a,'model-option:new-model');activate(a,'models-manual');enter(a,'manual-added')
            activate(a,'加入草稿');assert a.diag()['view']=='provider' and len(models())==4
            assert business()==before
            capture(a,OUT,f'{w}-workspace-models')
            activate(a,'model-edit:a');activate(a,'model_identity');enter(a,'renamed-a')
            activate(a,'model_display_name');enter(a,'Friendly model')
            activate(a,'完成');assert mid('renamed-a')=='a' and business()==before
            activate(a,'model-edit:a');activate(a,'model_identity');enter(a,'cancelled-rename');back(a)
            assert mid('renamed-a')=='a' and all(m['name']!='cancelled-rename' for m in models())
            activate(a,'model-delete:b');select(a,'delete');assert a.diag()['provider_workspace']['deleted_count']==1
            assert business()==before;capture(a,OUT,f'{w}-pending-delete')
            activate(a,'model-delete:b');assert a.diag()['provider_workspace']['deleted_count']==0
            activate(a,'model-delete:b');select(a,'delete')
            activate(a,'provider-save-bottom');assert a.diag()['view']=='provider' and not a.diag()['provider_workspace']['dirty']
            assert rows("select base_url,api_key from providers where id='p'")[0]==(base,'changed-fake-only')
            assert rows("select name,display_name from model_configs where id='a'")[0]==('renamed-a','Friendly model')
            assert rows("select enabled from model_configs where id='b'")[0][0]==0
            assert rows('select id from model_configs where is_default=1')[0][0]=='a'
            assert len(rows('select id from model_configs where enabled=1'))==3
            # Restore the soft-deleted model through a draft, retaining its stable ID/caps.
            original=rows("select capabilities_json from model_configs where id='b'")[0][0]
            activate(a,'provider-add-models');activate(a,'models-manual');enter(a,'b');activate(a,'加入草稿')
            assert mid('b')=='b' and rows("select enabled from model_configs where id='b'")[0][0]==0
            activate(a,'provider-save-bottom')
            assert rows("select enabled,capabilities_json from model_configs where id='b'")[0]==(1,original)
            saved=business();field(a,'name','Discarded name');back(a)
            assert a.diag()['context_menu']['kind']=='choice:provider_exit';capture(a,OUT,f'{w}-discard-menu')
            select(a,'edit');assert a.diag()['provider_workspace']['active']
            back(a);select(a,'discard');assert not a.diag()['provider_workspace']['active'] and business()==saved
            # New Provider: models can be added and edited before any connection save.
            activate(a,'添加 Provider');activate(a,'provider-add-models');activate(a,'models-manual');enter(a,'fresh-model');activate(a,'加入草稿')
            assert len(models())==1 and len(rows('select id from providers'))==1
            fresh=mid('fresh-model');activate(a,'model-edit:'+fresh);activate(a,'model_display_name');enter(a,'Fresh display');activate(a,'完成')
            field(a,'name','Another provider');field(a,'baseURL',base);field(a,'apiKey','fake-new')
            activate(a,'provider-protocol');select(a,'anthropic');capture(a,OUT,f'{w}-new-provider')
            # The pre-existing manually configured model is not silently remapped.
            activate(a,'provider-save-bottom');assert len(rows('select id from providers'))==2
            assert a.diag()['provider_workspace']['active'] and not a.diag()['provider_workspace']['dirty']
            assert rows('select name,display_name from model_configs where id=?',(fresh,))[0]==('fresh-model','Fresh display')
            # Empty key blocks the single commit but keeps the whole draft.
            saved=business();field(a,'apiKey','');activate(a,'provider-save-bottom');assert a.diag()['modal']
            capture(a,OUT,f'{w}-failed-save');a.tap(w//2 if h>w else w-70,h-44)
            ui.wait_for(lambda:not a.diag()['modal'])
            assert business()==saved and a.diag()['provider_workspace']['dirty']
            back(a);select(a,'discard')
            activate(a,'provider-open:p');field(a,'name','Saved from exit menu');back(a);select(a,'save')
            assert not a.diag()['provider_workspace']['active'] and rows("select name from providers where id='p'")[0][0]=='Saved from exit menu'
            # A cancelled/late list cannot repopulate another workspace.
            if w==640:
                activate(a,'provider-open:p');activate(a,'provider-add-models');Models.delay=.5
                n=len(Models.requests);activate(a,'models-fetch');ui.wait_for(lambda:len(Models.requests)>n)
                back(a);back(a);assert not a.diag()['provider_workspace']['active']
                activate(a,'添加 Provider');time.sleep(.7);Models.delay=0
                assert len(models())==0 and a.diag()['view']=='provider'
            print('Provider draft workspace passed:',w,h,flush=True)
        finally:Models.delay=0;a.close()
if __name__=='__main__':
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Models) as server:
        threading.Thread(target=server.serve_forever,daemon=True).start()
        sizes=[(640,260),(960,266),(568,1210)]
        if os.environ.get('PENCHAT_PROVIDER_SIZE'):sizes=[tuple(map(int,os.environ['PENCHAT_PROVIDER_SIZE'].split('x')))]
        for size in sizes:run(*size,server.server_port)
