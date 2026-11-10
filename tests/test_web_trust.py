#!/usr/bin/env python3
"""Focused trust resource/path validation; no requests or user data."""
import hashlib
import json
import pathlib
import ssl
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRUST = ROOT / 'native/web-search/trust'
source = json.loads((TRUST / 'source.json').read_text())
data = (TRUST / 'cacert.pem').read_bytes()
assert len(data) == source['bytes']
assert hashlib.sha256(data).hexdigest() == source['sha256']
assert hashlib.sha256((TRUST / 'MPL-2.0.txt').read_bytes()).hexdigest() == source['license_sha256']
context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
context.load_verify_locations(cafile=str(TRUST / 'cacert.pem'))
assert context.cert_store_stats()['x509_ca'] == source['certificates']

with tempfile.TemporaryDirectory(prefix='penchat-web-trust-') as tmp:
    tmp = pathlib.Path(tmp)
    (tmp / 'test.cpp').write_text('''
#include "trust.hpp"
#include <cassert>
int main(int argc, char **argv) {
  assert(penchat::webTrustPath("/package/assets/bin/worker") ==
         "/package/assets/bin/../web-search/cacert.pem");
  bool invalid = false;
  try { penchat::webTrustPath("relative/worker"); }
  catch (const std::runtime_error &) { invalid = true; }
  assert(invalid);
  penchat::validateWebTrust(argv[1]);
  for (int i = 2; i < argc; ++i) {
    invalid = false;
    try { penchat::validateWebTrust(argv[i]); }
    catch (const std::runtime_error &) { invalid = true; }
    assert(invalid);
  }
}
''')
    (tmp / 'empty').touch()
    (tmp / 'html').write_text('<html>Not a certificate</html>')
    (tmp / 'oversized').write_bytes(b'x' * (512 * 1024 + 1))
    subprocess.run(['c++', '-std=c++14', '-I' + str(ROOT / 'native/web-search'),
                    str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test'), str(TRUST / 'cacert.pem'),
                    *[str(tmp / name) for name in ['missing', 'empty', 'html', 'oversized']],
                    str(tmp)], check=True)
print('Bundled Mozilla CA hash, license, 118 parsed roots, relocation and invalid-resource checks passed.')
