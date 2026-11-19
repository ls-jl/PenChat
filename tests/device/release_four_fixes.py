"""Four-fix acceptance inside release_acceptance's complete private/public scope.

No work on import. Real requests require an explicit configured credential object;
Y02 can only enter the local fake-server path. Snapshots contain synthetic data
and, for real-request scopes, credentials; the owner must scrub them after restore.
"""
import http.server,json,pathlib,re,sqlite3,struct,tempfile,time,zlib,math,contextlib
from PIL import Image
from release_auxiliary import _server,_rows,_attachment,_idle
from release_lifecycle import pull_database
ROOT=pathlib.Path(__file__).resolve().parents[2]
SESSION='refresh-bottom'
CHEM=r'''化学式：$\ce{H2O}$、$\ce{SO4^2-}$、$\ce{Ca(OH)2}$。

反应：$\ce{2 H2 + O2 -> 2 H2O}$

平衡：$\ce{NH4+ <=> NH3 + H+}$

标注：$\ce{A ->[\Delta][H2O] B}$

同位素：$\ce{^{14}_{6}C}$，水合物：$\ce{CuSO4 · 5 H2O}$。

放气：$\ce{CaCO3 ->[\Delta] CaO + CO2 ^}$

沉淀：$\ce{Ag+ + Cl- -> AgCl v}$

分数：$\ce{H2 + 1/2 O2 -> H2O}$
'''

def owned(d):
  assert d.result.get('candidate_startup_verified')
  assert not d.had_data or d.sh('test -d '+d.saved+' && echo saved')=='saved'
  assert d.data!=d.saved

def replace(d,path):
  owned(d);d.close()
  d.sh('rm -f '+d.data+'/penchat.db '+d.data+'/penchat.db-wal '+d.data+'/penchat.db-shm '+d.data+'/penchat.db-journal')
  d.push(path,d.data+'/penchat.db');d.launch()

def base(d,path,text='',draft='',prefs=None):
  d.fixture(path)
  with sqlite3.connect(path) as db:
    db.execute('delete from chat_messages');db.execute('update chat_sessions set title=?,message_count=0,metadata_json=? where id=?',('Release035 fixture',json.dumps({'chat_preferences':prefs or {'model_id':'a','gauge':True}}),SESSION))
    db.execute("update model_configs set capabilities_json=? where id='a'",(json.dumps({'vision':True,'reasoning':{'levels':{'none':'none','low':'low','high':'high','max':'max'},'mode':'effort','summary':True}}),))
    db.execute("update providers set metadata_json=? where id='p'",(json.dumps({'api':'chat','model_adapter':'deepseek'}),))
    if text:
      db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values('body',?,1,'assistant','complete','text',1,1,?)",(SESSION,text))
      db.execute('update chat_sessions set message_count=1 where id=?',(SESSION,))
    if draft:db.execute('insert or replace into chat_drafts(draft_key,revision,text) values(?,1,?)',(SESSION,draft))

def real_model(db,credentials,provider_id='p',model_id='a'):
  p=credentials['provider'];m=credentials['model']
  db.execute('insert or replace into providers(id,name,type,base_url,api_key,secret_ref,encryption,headers_json,enabled,is_default,created_at,updated_at,metadata_json) values(?,?,?,?,?,?,?,?,1,1,1,1,?)',
      (provider_id,'Configured DeepSeek test',p['type'],p['base_url'],p.get('api_key'),p.get('secret_ref'),p.get('encryption'),p.get('headers_json','{}'),p.get('metadata_json','{}')))
  db.execute('insert or replace into model_configs(id,provider_id,name,display_name,model_type,capabilities_json,default_parameters_json,is_default,enabled,created_at,updated_at,metadata_json) values(?,?,?,?,?,?,?,1,1,1,1,?)',
      (model_id,provider_id,m['name'],'Configured DeepSeek test',m['model_type'],m.get('capabilities_json','{}'),m.get('default_parameters_json','{}'),m.get('metadata_json','{}')))

def last(d):
  rows=_rows(d,'select id,status,output_text,reasoning_text,metadata_json,error_json from chat_messages where session_id=? and role=? order by seq desc limit 1',(SESSION,'assistant'))
  return rows[0] if rows else None

def send(d,text):
  owned(d);d.chat();d.wait(lambda:not d.state()['surface_motion'])
  previous=last(d);d.four_previous_assistant=previous[0] if previous else None
  s=d.state();assert not s['generating'] and not s['compressing']
  if text:
    assert s['draft_length']==0
    d.tap(s['composer_text_bounds']);d.wait(lambda:d.state()['editor']['active'])
    if d.state()['editor']['layout']!='en26':d.editor_key('language')
    assert d.state()['editor']['layout']=='en26'
    for char in text:d.editor_key('space' if char==' ' else char)
    d.editor_action('done');d.wait(lambda:not d.state()['editor']['active'] and not d.state()['surface_motion'])
  s=d.state();assert s['draft_length']>0 or s['chat_images']
  d.touch(s['send_center_x2']/2,s['send_center_y2']/2)
  d.four_last_activity=time.monotonic()

def reader_activity(d,s):
  # Keep a genuine reading workload during long fault/request waits, without
  # changing system sleep settings or touching Send/Stop/keyboard controls.
  if time.monotonic()-getattr(d,'four_last_activity',0)<6:return
  if not (s['visible'] and s['window_focused'] and s['touch_input']['active']):return
  b=s['chat_header']['reader_bounds'];d.touch((b[0]+b[2])/2,(b[1]+b[3])/2)
  d.four_last_activity=time.monotonic()

def finished(d,expected='complete',seconds=650):
  deadline=time.monotonic()+seconds
  while time.monotonic()<deadline:
    s=d.state()
    if s['generating']:reader_activity(d,s)
    if not s['generating'] and not s['storage']['unsaved']:
      row=last(d)
      if row and row[0]!=getattr(d,'four_previous_assistant',None):
        assert row[1]==expected,'Unexpected terminal status: '+str(row[1])
        return row
    time.sleep(.25)
  raise AssertionError('Request did not reach a saved terminal state')

def page_action(d,name):
  def b(s):return {**s['page'].get('controls',{}),**s['page']['actions']}[name]
  # Header actions intentionally sit within 8dp of the reader's top edge.
  # They share its scroll container but do not need a synthetic extra margin.
  for _ in range(50):
    s=d.state();bounds=b(s);v=s['page']['viewport'];top=max(v[1],s['safe_insets'][1] if d.h>d.w else v[1])
    if not s['page'].get('scrolling') and bounds[1]>=top and bounds[3]<=v[3]:
      d.tap(bounds);return
    direction=-1 if bounds[3]>v[3] else 1;x=(v[0]+v[2])/2;y=v[1]+(v[3]-v[1])*(.75 if direction<0 else .25)
    d.drag(x,y,x,y+direction*min(100,(v[3]-v[1])*.5))
  raise AssertionError('Page action unreachable: '+name)

def theme(d,mode):
  d.chat();d.settings();d.tap(d.reveal(lambda s:s['settings']['rows']['theme']['bounds']))
  d.wait(lambda:d.state()['context_menu']['kind']=='choice:theme');d.tap(d.state()['context_menu']['choices'][mode]['bounds'])
  d.wait(lambda:d.state()['appearance']['theme']==mode and not d.state()['appearance']['pending']);d.chat()

def scale(d,percent):
  d.chat();d.settings();d.tap(d.reveal(lambda s:s['settings']['rows']['ui_scale']['bounds']));d.wait(lambda:d.state()['view']=='ui_scale')
  d.reveal(lambda s:s['ui_scale']['slider']);b=d.state()['ui_scale']['slider'];d.touch(b[0]+round((percent-75)*(b[2]-b[0])/75),(b[1]+b[3])/2)
  for _ in range(5):
    value=d.state()['ui_scale']['draft']
    if value==percent:break
    page_action(d,'增大' if value<percent else '减小')
  assert d.state()['ui_scale']['draft']==percent
  page_action(d,'保存');d.wait(lambda:d.state()['view']=='settings' and d.state()['ui_scale']['applied']==percent);d.chat()

def gauge_bounds(s):
  percent=s['ui_scale']['applied'];dp=lambda n:(n*percent+50)//100
  c=max(44,dp(44));pad=dp(25 if s['height']>s['width'] else 2);b=s['composer_bounds']
  x=b[2]+1-pad-(2*c+dp(8));y=b[3]+1-pad-c
  return [x,y,x+c-1,y+c-1]

def gauge(d,index):
  s=d.state();b=gauge_bounds(s);d.tap(b);d.wait(lambda:d.state()['popup']=='effort');time.sleep(.18)
  s=d.state();options=s['effort_choices'];percent=s['ui_scale']['applied'];dp=lambda n:(n*percent+50)//100
  b=s['popup_bounds'];radius=dp(44)//2;first=b[0]+dp(24)+radius;lastx=b[2]-dp(24)-radius
  x=(first+lastx)//2 if len(options)==1 else first+round((lastx-first)*index/(len(options)-1));d.touch(x,b[1]+dp(68)+(dp(44)-1)/2)
  d.wait(lambda:d.state()['session_preferences'].get('effort')==options[index])
  s=d.state();b=s['popup_bounds'];d.touch(d.w/2,max(s['safe_insets'][1]+5,b[1]-12));d.wait(lambda:not d.state()['attachments'] and not d.state()['surface_motion']);time.sleep(.2);return options[index]

def gauge_pixels(d,image):
  s=d.state();b=gauge_bounds(s);x=math.floor((b[0]+b[2])/2+.5);y=math.floor((b[1]+b[3])/2+.5)
  im=Image.open(image).convert('RGB');bg=im.getpixel((max(0,b[0]+2),max(0,b[1]+2)))
  center=im.getpixel((x,y));assert sum(abs(a-b) for a,b in zip(center,bg))>45,('Gauge center missing',d.serial,s['ui_scale']['applied'],b,center,bg)
  return {'bounds':b,'center':[x,y],'center_rgb':list(center),'background_rgb':list(bg)}

def gauge_invariance(records,percent):
  center=records[0]['center'];radius=max(4,round(4*percent/100));masks=[]
  for record in records:
    im=Image.open(record['screenshot']).convert('RGB');bg=record['background_rgb'];points=set()
    for y in range(center[1]-radius,center[1]+radius+1):
      for x in range(center[0]-radius,center[0]+radius+1):
        if sum(abs(a-b) for a,b in zip(im.getpixel((x,y)),bg))>45:points.add((x,y))
    masks.append(points)
  common=set.intersection(*masks);assert common,'No fixed painted hub across needle directions'
  observed=[sum(p[i] for p in common)/len(common) for i in (0,1)]
  assert all(abs(a-b)<=.75 for a,b in zip(observed,center)),('Painted hub left control center',observed,center)
  return {'painted_hub_centroid':observed,'common_hub_pixels':len(common),'expected_center':center}

class Server(http.server.BaseHTTPRequestHandler):
  protocol_version='HTTP/1.1';mode='title';title='图片';requests=[];title_requests=0
  def log_message(self,*args):pass
  def do_POST(self):
    request=json.loads(self.rfile.read(int(self.headers['Content-Length'])));naming='[penchat-title]' in json.dumps(request);type(self).requests.append(naming)
    if naming:type(self).title_requests+=1
    self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
    chat=self.path.endswith('/chat/completions')
    def emit(value):self.wfile.write(('data: '+json.dumps(value,ensure_ascii=False)+'\n\n').encode());self.wfile.flush()
    try:
      if self.mode=='stall':
        emit({'choices':[{'index':0,'delta':{'reasoning_content':'Synthetic thinking must survive a stalled stream.'},'finish_reason':None}]})
        until=time.monotonic()+130
        while time.monotonic()<until:self.wfile.write(b': keep-alive\n\n');self.wfile.flush();time.sleep(.2)
        return
      text=self.title if naming else '识别内容如下。图中展示水的电解实验，生成氢气和氧气，比例为二比一。'
      if chat:
        emit({'choices':[{'index':0,'delta':{'content':text},'finish_reason':'stop'}]});self.wfile.write(b'data: [DONE]\n\n');self.wfile.flush()
      else:emit({'type':'response.output_text.delta','delta':text});emit({'type':'response.completed'})
    except (BrokenPipeError,ConnectionResetError):pass
    finally:self.close_connection=True

def picture(d,folder,db):
  def chunk(k,data):return struct.pack('!I',len(data))+k+data+struct.pack('!I',zlib.crc32(k+data))
  image=folder/'synthetic.png';image.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',10,10,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([60,120,200])*10)*10))+chunk(b'IEND',b''))
  path=d.data+'/assets/release035-title.png';d.sh('mkdir -p '+d.data+'/assets');d.push(image,path)
  db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('photo',?,1,'user','complete','image','',1,1)",(SESSION,))
  db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,stored_path,original_path,width,height,created_at) values('photo',?,'photo','image','album',?,?,10,10,1)",(SESSION,path,path))
  db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('photo',?,'photo',0,'image','photo')",(SESSION,))


def run_local(d,credential=None,scales=(75,100,125,150),title_stall=True):
  results={'visuals':[]}
  with tempfile.TemporaryDirectory(prefix='penchat-four-local-') as tmp:
    folder=pathlib.Path(tmp);fixture=folder/'visual.db';base(d,fixture,CHEM)
    replace(d,fixture)
    for mode in ('dark','light'):
      theme(d,mode)
      for percent in scales:
        scale(d,percent);d.capture(f'chem-{mode}-{percent}-bottom')
        for page in range(16):
          s=d.state()
          if s['scroll_y']<=0:break
          v=s['chat_header']['reader_bounds'];d.drag((v[0]+v[2])/2,v[1]+25,(v[0]+v[2])/2,v[3]-8)
          d.capture(f'chem-{mode}-{percent}-reader-{page}')
        assert d.state()['scroll_y']<=0,'Chemical first line must be reachable'
        d.capture(f'chem-{mode}-{percent}-top')
        # Return to real bottom before using composer actions.
        s=d.state()
        if s['to_bottom_visible']:d.touch(s['to_bottom_x']+max(44,round(44*percent/100))/2,s['to_bottom_y']+max(44,round(44*percent/100))/2)
        d.wait(lambda:not d.state()['composer_hidden']);choices=d.state()['effort_choices'];angles=[]
        for index in sorted({0,len(choices)//2,len(choices)-1}):
          key=gauge(d,index);name=f'gauge-{mode}-{percent}-{index}';d.capture(name);e=gauge_pixels(d,d.out/(name+'.png'));e.update(effort=key,index=index,screenshot=str(d.out/(name+'.png')));angles.append(e)
        _attachment(d,'effort');d.wait(lambda:not d.state()['effort_gauge'] and not d.state()['surface_motion']);time.sleep(.2)
        _attachment(d,'effort');d.wait(lambda:d.state()['effort_gauge'] and not d.state()['surface_motion']);time.sleep(.2)
        assert not d.state()['session_preferences'].get('effort')
        name=f'gauge-{mode}-{percent}-neutral';d.capture(name);e=gauge_pixels(d,d.out/(name+'.png'));e.update(index=-1,effort='inherited',screenshot=str(d.out/(name+'.png')));angles.append(e)
        assert len({tuple(a['center']) for a in angles})==1
        results['visuals'].append({'theme':mode,'scale':percent,'gauge':angles,'pivot':gauge_invariance(angles,percent)})
        (d.out/'four-local-progress.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
    if not title_stall:return results
    with _server(d,Server) as port:
      Server.requests=[];Server.title_requests=0;Server.mode='title';Server.title='图片';fixture=folder/'title.db';base(d,fixture)
      with sqlite3.connect(fixture) as db:
        db.execute("update providers set base_url=?,metadata_json=? where id='p'",(d.auxiliary_fixture_url,json.dumps({'api':'responses'})))
        db.execute("insert or replace into app_settings values('auto_title_enabled','true',1)")
        picture(d,folder,db)
      replace(d,fixture);send(d,'');finished(d,seconds=60)
      def titled():
        row=_rows(d,'select title,metadata_json from chat_sessions where id=?',(SESSION,))[0];return row if json.loads(row[1]).get('title_state')=='complete' else None
      title=d.wait(titled,40)[0];assert title=='识别内容如下' and Server.title_requests==1
      d.capture('image-title-generic-fallback');results['image_title']={'generic_response_fallback':True,'stored_title':title,'requests':len(Server.requests)}
      Server.mode='stall';fixture=folder/'stall.db';base(d,fixture,draft='Synthetic stall fixture',prefs={'model_id':'a','effort':'high','gauge':True})
      with sqlite3.connect(fixture) as db:db.execute("update providers set base_url=?,metadata_json=? where id='p'",(d.auxiliary_fixture_url,json.dumps({'api':'chat','model_adapter':'deepseek'})))
      replace(d,fixture);before=len(Server.requests);start=time.monotonic();send(d,'')
      d.wait(lambda:d.state()['reasoning_header_count']==1,30)
      # Persisted stream metadata provides the state contract; the screenshot verifies its actual label.
      def waiting():
        reader_activity(d,d.state())
        r=last(d);return r if r and json.loads(r[4]).get('stream_waiting') else None
      d.wait(waiting,30);d.capture('stream-waiting-after15s')
      row=finished(d,'error',seconds=140);assert 'Synthetic thinking' in row[3] and '120' in row[5];assert len(Server.requests)==before+1
      d.capture('stream-stall-explicit-error');results['stream_stall']={'seconds':round(time.monotonic()-start,2),'reasoning_retained':True,'single_request':True,'error':row[5]}
  return results


def run_chat(d,credential):
  assert d.serial!='MEA2300009007113' and credential
  prompts=[('Compute 17*23.','391'),('compute thirty seven times twenty nine','1073'),('compute forty three times forty seven','2021')];results=[]
  with tempfile.TemporaryDirectory(prefix='penchat-real-ds-') as tmp:
    path=pathlib.Path(tmp)/'chat.db';base(d,path,draft=prompts[0][0],prefs={'model_id':'a','effort':'high','gauge':True,'system_prompt':''})
    with sqlite3.connect(path) as db:real_model(db,credential)
    replace(d,path)
    for index,(prompt,answer) in enumerate(prompts):
      start=time.monotonic();send(d,'' if index==0 else prompt);row=finished(d)
      meta=json.loads(row[4]);assert answer in row[2] and row[3] and meta.get('reasoning_observed')
      results.append({'prompt':prompt,'answer_present':True,'reasoning_chars':len(row[3]),'output_chars':len(row[2]),'seconds':round(time.monotonic()-start,2),'http_status':meta.get('http',{}).get('status')});d.capture('real-deepseek-'+str(index))
      (d.out/'real-ds-chat-progress.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
    assert _rows(d,'select count(*) from chat_messages where role=? and session_id=?',('user',SESSION))[0][0]==3
  return {'credential_source':credential['source_device'],'model':credential['model']['name'],'synthetic_only':True,'requests':results}

def run_disabled(d,credential):
  assert d.serial!='MEA2300009007113' and credential
  with tempfile.TemporaryDirectory(prefix='penchat-real-ds-disabled-') as tmp:
    path=pathlib.Path(tmp)/'chat.db';base(d,path,draft='Compute 13*19.',prefs={'model_id':'a','effort':'none','gauge':True,'system_prompt':''})
    with sqlite3.connect(path) as db:real_model(db,credential)
    replace(d,path);send(d,'');row=finished(d);meta=json.loads(row[4])
    assert '247' in row[2] and not row[3] and not meta.get('reasoning_observed')
    assert d.state()['reasoning_header_count']==0
    d.capture('real-deepseek-disabled')
    return {'model':credential['model']['name'],'credential_source':credential['source_device'],'synthetic_only':True,'thinking_disabled':True,'reasoning_header_absent':True,'http_status':meta.get('http',{}).get('status')}


def run_title(d,credential):
  assert d.serial!='MEA2300009007113' and credential
  with tempfile.TemporaryDirectory(prefix='penchat-real-title-') as tmp,_server(d,Server):
    folder=pathlib.Path(tmp);path=folder/'title.db';base(d,path,prefs={'model_id':'a','gauge':False})
    Server.mode='title';Server.requests=[];Server.title_requests=0
    with sqlite3.connect(path) as db:
      db.execute("update providers set base_url=?,metadata_json=? where id='p'",(d.auxiliary_fixture_url,json.dumps({'api':'responses'})))
      real_model(db,credential,'real-title-provider','real-title')
      db.execute("update model_configs set is_default=0 where id='real-title'")
      db.execute("insert or replace into app_settings values('title_model_id','\"real-title\"',1)");db.execute("insert or replace into app_settings values('auto_title_enabled','true',1)")
      picture(d,folder,db)
    replace(d,path);start=time.monotonic();send(d,'');finished(d,seconds=60)
    def titled():
      row=_rows(d,'select title,metadata_json from chat_sessions where id=?',(SESSION,))[0];return row if json.loads(row[1]).get('title_state') in ('complete','failed') else None
    title,metadata=d.wait(titled,650);assert json.loads(metadata)['title_state']=='complete';assert any(x in title for x in ('电解','氢','氧')) and title!='识别内容如下'
    assert Server.requests==[False] and Server.title_requests==0
    d.capture('real-deepseek-image-title')
    return {'model':credential['model']['name'],'credential_source':credential['source_device'],'title':title,'seconds':round(time.monotonic()-start,2),'real_image_sent':False,'real_title_request':True,'local_fallback_not_used':True}


def run_compression(d,credential):
  assert d.serial!='MEA2300009007113' and credential
  with tempfile.TemporaryDirectory(prefix='penchat-real-compaction-') as tmp:
    folder=pathlib.Path(tmp);path=folder/'compress.db';base(d,path,draft='unsent-test-draft',prefs={'model_id':'a','compression_model_id':'','has_sent':True,'effort':'api_default','system_prompt':''})
    with sqlite3.connect(path) as db:
      real_model(db,credential);caps=json.loads(credential['model'].get('capabilities_json') or '{}');caps.update(context_budget=8192,max_input_tokens=8192);db.execute("update model_configs set capabilities_json=? where id='a'",(json.dumps(caps),))
      for i in range(20):
        text=f'Test milestone TEST-{i//2}: preserve the approved choice and remaining check.' if i%2==0 else ('Synthetic result: build and interface checks passed; offline review remains. '*12)
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at,provider_id,model_config_id) values(?,?,?,?,'complete','text',?,1,1,'p','a')",('test-'+str(i),SESSION,i+1,'user' if i%2==0 else 'assistant',text))
      db.execute('update chat_sessions set message_count=20 where id=?',(SESSION,))
    replace(d,path);start=time.monotonic();_attachment(d,'compress')
    def compressed():
      if d.state()['compressing']:return None
      value=json.loads(_rows(d,'select metadata_json from chat_sessions where id=?',(SESSION,))[0][0]).get('context_compression',{})
      if d.state()['modal'] and not value.get('summary'):raise AssertionError('Real compression failed; preserved private fixture')
      return value if value.get('summary') else None
    cp=d.wait(compressed,650);assert cp.get('archive_id');assert d.state()['draft_length']==len('unsent-test-draft')
    assert _rows(d,'select count(*) from chat_messages where session_id=?',(SESSION,))[0][0]==20
    assert _rows(d,'select count(*) from chat_messages where session_id=?',(cp['archive_id'],))[0][0]==20
    d.capture('real-deepseek-compression');d.close();d.launch();after=compressed();assert after['summary']==cp['summary'] and after['revision']==cp['revision']
    return {'model':credential['model']['name'],'credential_source':credential['source_device'],'seconds':round(time.monotonic()-start,2),'summary_chars':len(cp['summary']),'through':cp['through'],'revision':cp['revision'],'archive_messages':20,'draft_preserved':True,'checkpoint_survives_restart':True}


def run_final_visual(d,credential=None):
  """Final payload delta: expanded chemistry and pivot/menu at 100/150%, both themes.

  The complete 75/100/125/150 matrix and image-title/stall paths have separate
  immutable package evidence. Recheck the changed rendering on the final binary.
  """
  return run_local(d,credential,scales=(100,150),title_stall=False)
