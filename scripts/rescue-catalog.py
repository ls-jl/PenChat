#!/usr/bin/env python3
"""Publish only the four traceable historical deliveries to the rescue source.

No ordinary update directory is touched. release.json is the commit marker and
is uploaded last. A conflicting same-version hash is never overwritten.
"""
import argparse,hashlib,json,pathlib,struct,sys,time,urllib.request,urllib.error,zipfile,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tests'))
from runtime_policy import check_native_payload
APP=json.loads((ROOT/'package.json').read_text())
SOURCES={
 ('0.3.2','rk3562'):('.work/release-0.3.2/packages/candidate-3','c07a85f87e10e872b395ff0ca5c599bb4d96b2f19fabd3d038e9dd47ffb83041'),
 ('0.3.2','cv1826-s'):('.work/release-0.3.2/packages/candidate-3','d07b24fbb84a3399d349294a23b647cd294860bcf4f20fa934339f159ef7467d'),
 ('0.3.3','rk3562'):('.work/release-0.3.3/deliverable','350cdd30947d1023d7c1bd47f529deaad4427a4738e42365f6fe535ce274af07'),
 ('0.3.3','cv1826-s'):('.work/release-0.3.3/deliverable','9372900761783fa4bf445e16aec8f4d0b256c34b5b24c26fcc90972ccc7fd6ad')}
def request(path,method='GET',data=None):
 try:
  with urllib.request.urlopen(urllib.request.Request('http://localhost:17865/'+path,data=data,method=method),timeout=180) as r:return r.read()
 except urllib.error.HTTPError as e:
  if e.code==404:return None
  if method=='MKCOL' and e.code in (405,409):return b''
  raise

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--publish',action='store_true');a=p.parse_args()
 out=ROOT/'.work/rescue/catalog';out.mkdir(parents=True,exist_ok=True);records=[]
 if (out/'records.json').exists():
  history=out/'history'/str(time.time_ns());history.mkdir(parents=True);shutil.copy2(out/'records.json',history/'records.json')
 for (version,target),(directory,sha) in SOURCES.items():
  filename=APP['appid']+'.'+version.replace('.','_')+'-'+target+'.amr';source=ROOT/directory/filename;data=source.read_bytes()
  assert hashlib.sha256(data).hexdigest()==sha,'Historical hash mismatch: '+str(source)
  elfs={}
  with zipfile.ZipFile(source) as z:
   assert len(set(z.namelist()))==len(z.namelist()) and z.testzip() is None
   manifest=json.loads(z.read('manifest.json'));assert (manifest['appid'],manifest['version'])==(APP['appid'],version)
   for name in z.namelist():
    assert not name.startswith('/') and '..' not in pathlib.PurePosixPath(name).parts and not name.endswith(('.sh','.bash'))
    payload=z.read(name)
    if payload.startswith(b'\x7fELF'):
     assert payload[4]==(2 if target=='rk3562' else 1) and struct.unpack_from('<H',payload,18)[0]==(183 if target=='rk3562' else 40),name
     check_native_payload(payload,name);elfs[name]=hashlib.sha256(payload).hexdigest()
   assert 'assets/bin/penchat-lvgl' in elfs and 'assets/bin/penchat-scanner' in elfs
   assert b'reply_groups' in z.read('assets/bin/penchat-lvgl'),'Schema v3 provenance missing'
  metadata=dict(format=1,version=version,platform=target,appid=APP['appid'],file=filename,size=len(data),sha256=sha,
    build='candidate-3; native tree 4483f9ae1e3c974e7844012426944a92a194b95e' if version=='0.3.2' else 'candidate-6; runtime 7384173; native f5a8308',
    channel='candidate',schema=dict(min=3,max=3),
    provenance='docs/RELEASE-'+version+'.md')
  copy=json.loads((ROOT/'scripts/rescue-release-notes.json').read_text())[version]
  metadata['release_notes']=copy['notes'];metadata['known_issues']=copy['notes']
  metadata['install_warning']=copy.get('cvi_warning' if target=='cv1826-s' else 'warning',copy.get('warning',''))
  folder=out/('penchat-'+target)/version;folder.mkdir(parents=True,exist_ok=True)
  (folder/'release.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n')
  (folder/(filename+'.sha256')).write_text(sha+'  '+filename+'\n')
  record=dict(version=version,platform=target,source=str(source),sha256=sha,bytes=len(data),elfs=elfs,published=False,synced=False)
  if a.publish:
   base='app/downgrade/penchat-'+target+'/'+version+'/'
   old=request(base+'release.json')
   if old is not None:assert json.loads(old)['sha256']==sha,'Refuse conflicting same-version release'
   request(base,'MKCOL')
   existing=request(base+filename)
   if existing is None:request(base+filename,'PUT',data)
   else:assert hashlib.sha256(existing).hexdigest()==sha,'Refuse conflicting same-version AMR'
   for name in [filename+'.sha256','release.json']:request(base+name,'PUT',(folder/name).read_bytes())
   record['published']=True
   for attempt in range(30):
    standby='standby/'+base
    remote=request(standby+'release.json');package=request(standby+filename);checksum=request(standby+filename+'.sha256')
    if remote and package and checksum and json.loads(remote)==metadata and hashlib.sha256(package).hexdigest()==sha and checksum==(folder/(filename+'.sha256')).read_bytes():record['synced']=True;break
    time.sleep(2)
   assert record['synced'],'Standby synchronization incomplete'
  records.append(record);(out/'records.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n')
  print(version,target,sha,'published/synced' if record['synced'] else 'verified local source',flush=True)
if __name__=='__main__':main()
