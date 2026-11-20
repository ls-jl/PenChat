#!/usr/bin/env python3
"""Static component/package checks only; never executes the worker or a request."""
import argparse,hashlib,json,pathlib,re,struct,subprocess,tempfile,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('package',type=pathlib.Path);p.add_argument('target',choices=['rk3562','cv1826-s']);a=p.parse_args()
expected=(2,183) if a.target=='rk3562' else (1,40)
with zipfile.ZipFile(a.package) as z,tempfile.TemporaryDirectory() as tmp:
    names=z.namelist();lib='assets/web-search/lib/libedge-curl-impersonate.so.4';worker='assets/bin/penchat-web-search'
    assert [n for n in names if n.startswith('assets/web-search/lib/') and not n.endswith('/')]==[lib]
    payload=ROOT/('build/payload-'+a.target)/'assets'
    for name,local in [(lib,payload/'web-search/lib/libedge-curl-impersonate.so.4'),(worker,payload/'bin/penchat-web-search')]:
        data=z.read(name);assert data[4]==expected[0] and struct.unpack_from('<H',data,18)[0]==expected[1],name
        assert hashlib.sha256(data).digest()==hashlib.sha256(local.read_bytes()).digest(),name
        binary=pathlib.Path(tmp)/pathlib.Path(name).name;binary.write_bytes(data)
        dynamic=subprocess.check_output(['readelf','-dW',str(binary)],text=True)
        assert '/persistent/' not in dynamic and '/home/' not in dynamic,'Build-only RPATH escaped'
        if name==worker:assert '$ORIGIN/../web-search/lib' in dynamic
        versions=subprocess.check_output(['readelf','-VW',str(binary)],text=True)
        glibc=[tuple(map(int,v.split('.'))) for v in re.findall(r'Name: GLIBC_([0-9.]+)',versions)]
        assert not glibc or max(glibc)<=(2,17),versions
        symbols=subprocess.check_output(['readelf','-Ws',str(binary)],text=True)
        assert not re.search(r'\bUND\b[^\n]*\b(system|popen|popen64|execvp|execlp|wordexp)\b',symbols),name
    assert z.read('assets/web-search/source-lock.json')==(ROOT/'native/web-search/source-lock.json').read_bytes()
    trust=ROOT/'native/web-search/trust'
    ca=z.read('assets/web-search/cacert.pem');provenance=json.loads((trust/'source.json').read_text())
    assert ca==(trust/'cacert.pem').read_bytes() and len(ca)==provenance['bytes']
    assert hashlib.sha256(ca).hexdigest()==provenance['sha256']
    assert ca.count(b'-----BEGIN CERTIFICATE-----')==provenance['certificates']
    assert z.read('assets/web-search/ca-source.json')==(trust/'source.json').read_bytes()
    assert z.read('assets/web-search/licenses/Mozilla-CA-MPL-2.0.txt')==(trust/'MPL-2.0.txt').read_bytes()
    for notice in ['Edge-Search-MIT.txt','DDGS-MIT.txt','webserp-MIT.txt','libxml2.txt','LICENSE_BORINGSSL','LICENSE_CURL','LICENSE_CARES','LICENSE_NGHTTP2','LICENSE_NGHTTP3','LICENSE_NGTCP2','LICENSE_ZLIB','LICENSE_ZSTD','LICENSE_BROTLI']:
        assert z.read('assets/web-search/licenses/'+notice),notice
print('Web component: target architecture, exact payload, source lock, bundled CA roots, private loader path, SDK symbol baseline and notices checked.')
