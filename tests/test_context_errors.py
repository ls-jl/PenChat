import hashlib
import http.server
import json
import pathlib
import subprocess
import threading
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[1]
ROOT = REPO/'.work/context-errors'
ROOT.mkdir(parents=True, exist_ok=True)


class Server(http.server.BaseHTTPRequestHandler):
    mode = 'success'
    requests = []

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append(body)
        if self.mode == 'http400':
            data = json.dumps({'error': {'message': 'fixture invalid argument'}}).encode()
            self.send_response(400)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.end_headers()
        text = '摘要' * 2000 if self.mode == 'overlong' else '事实摘要。'
        delta = {'reasoning_content': 'local thought only'} if self.mode == 'reasoning_only' else {'content': text}
        self.wfile.write(('data: ' + json.dumps({'choices': [{'index': 0, 'delta': delta}]}) + '\n\n').encode())
        finish = 'length' if self.mode == 'length' else 'stop'
        self.wfile.write(('data: ' + json.dumps({'choices': [{'index': 0, 'delta': {}, 'finish_reason': finish}]}) + '\n\n').encode())
        if self.mode != 'missing_done':
            self.wfile.write(b'data: [DONE]\n\n')
        self.wfile.flush()


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Server)
threading.Thread(target=server.serve_forever, daemon=True).start()
results = []
try:
    for mode, compatible in [('success', False), ('http400', False), ('length', False),
                             ('reasoning_only', False), ('missing_done', False),
                             ('missing_done', True), ('overlong', False)]:
        Server.mode = mode
        Server.requests = []
        database = ROOT / ('probe-' + mode + ('-compatible' if compatible else '') + '.db')
        process = subprocess.run([str(REPO / 'build/host/penchat-compression-probe'), str(database),
                                  'http://127.0.0.1:' + str(server.server_port) + '/v1',
                                  'compatible' if compatible else 'strict'],
                                 text=True, capture_output=True, timeout=20, check=True)
        result = json.loads(process.stdout.strip().splitlines()[-1])
        request = Server.requests[-1]
        result.update(mode=mode, compatible=compatible, requests=len(Server.requests),
                      request_keys=sorted(request),
                      source_roles=[m['role'] for m in request['messages']],
                      source_bytes=sum(len(json.dumps(m)) for m in request['messages']))
        if mode in ('success', 'overlong') or compatible:
            assert result['compression']['success'], result
        else:
            assert result['compression']['error'].startswith('压缩失败：'), result
        results.append(result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
finally:
    server.shutdown()
(ROOT / 'probe-results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
print('Seven compression error propagation cases passed.')
