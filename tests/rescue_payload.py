"""Strict exception to single-target ELF isolation: only two rescue SOs."""
import hashlib,json,re,struct
from runtime_policy import check_native_payload

def check_rescue(archive,target):
    names=archive.namelist()
    modules=[n for n in names if n.startswith('libs/') and n.endswith('.so')]
    assert len(modules)==2,modules
    for abi,bits,machine in [('arm',1,40),('arm64',2,183)]:
        found=[n for n in modules if re.fullmatch(r'libs/'+abi+r'/libjsapi_penchat_rescue_[0-9a-f]{16}\.so',n)]
        assert len(found)==1,(abi,modules)
        data=archive.read(found[0]);assert data[:6]==b'\x7fELF'+bytes([bits,1])
        assert struct.unpack_from('<H',data,18)[0]==machine
        assert hashlib.sha256(data).hexdigest()[:16] in found[0]
        check_native_payload(data,found[0])
        for forbidden in [b'libcairo',b'libfreetype',b'libpinyinime',b'librknnrt',b'libcviruntime',b'libopencv']:
            assert forbidden not in data,(found[0],forbidden)
    descriptor=json.loads(archive.read('assets/rescue/payload.json'))
    assert descriptor['format']==1 and descriptor['platform']==target
    assert descriptor['abi_bits']==(64 if target=='rk3562' else 32)
    runtime=descriptor['runtime'];assert re.fullmatch(r'assets/runtime/libpenchat_runtime_[0-9a-f]{16}\.so',runtime)
    data=archive.read(runtime);assert hashlib.sha256(data).hexdigest()==descriptor['runtime_sha256']
    assert b'penchat_create_runtime_v1' in data
    assert data[4]==(2 if target=='rk3562' else 1) and struct.unpack_from('<H',data,18)[0]==(183 if target=='rk3562' else 40)
    assert [n for n in names if n.startswith('assets/runtime/') and n.endswith('.so')]==[runtime]
    check_native_payload(data,'runtime')
    return data
