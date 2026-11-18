"""Actual native compactor, local protocol fixtures, no model credentials or devices."""
import http.server
import json
import pathlib
import re
import struct
import zlib
import subprocess
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE = ROOT / 'build/host/penchat-compression-probe'


class Fixture(http.server.BaseHTTPRequestHandler):
    requests = []
    mode = 'ok'

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append(body)
        if self.mode == 'error' or (self.mode == 'overflow' and len(self.requests) == 1):
            code = 'context_length_exceeded' if self.mode == 'overflow' else 'invalid_api_key'
            data = json.dumps({'error': {'code': code, 'message': 'fixture rejected'}}).encode()
            self.send_response(400 if self.mode == 'overflow' else 401)
        else:
            text = 'Checkpoint facts, decisions, constraints and unfinished work.'
            if self.mode in ('adaptive', 'no_gain', 'ignore_budget'):
                limits = re.findall(r'Maximum (\d+) Unicode', json.dumps(body))
                count = min(480, int(limits[-1])) if limits else 480
                text = '事' * (80 if self.mode == 'no_gain' else 600 if self.mode == 'ignore_budget' else count)
            if self.mode == 'empty':
                text = ''
            if self.path.endswith('/messages'):
                response = {'type': 'message', 'role': 'assistant', 'content': [{'type': 'text', 'text': text}], 'stop_reason': 'end_turn', 'usage': {}}
            elif self.path.endswith('/chat/completions'):
                response = {'choices': [{'index': 0, 'message': {'role': 'assistant', 'content': text}, 'finish_reason': 'stop'}]}
            else:
                response = {'status': 'completed', 'output': [{'type': 'message', 'role': 'assistant', 'status': 'completed', 'content': [{'type': 'output_text', 'text': text}]}]}
            data = json.dumps(response).encode()
            self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def wire(text):
    return {'role': 'user', 'content': [{'type': 'input_text', 'text': text}]}


def rows(count=8, size=40):
    return [{'seq': i + 1, 'role': 'user' if i % 2 == 0 else 'assistant', 'status': 'complete',
             'output_text': f'original-{i + 1} ' + 'x' * size, 'parts': []} for i in range(count)]


class Compaction(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def run_case(self, protocol='chat', data=None, budget=16384, chat_budget=32768, mode='ok', **extra):
        Fixture.requests = []
        Fixture.mode = mode
        model = {'id': 'fixture', 'provider_id': 'fixture', 'name': 'fixture', 'api_key': 'local-fake',
                 'base_url': f'http://127.0.0.1:{self.server.server_port}/v1',
                 'provider_metadata': {'api': protocol, 'compatibility': {'enabled': True, 'streaming': False}},
                 'capabilities_json': {'context_budget': budget}}
        chat = dict(model, capabilities_json={'context_budget': chat_budget})
        request = {'model': model, 'chat': chat, 'snapshot': {'rows': data or rows()}}
        request.update(extra)
        with tempfile.TemporaryDirectory() as temp:
            result = subprocess.check_output([str(PROBE), temp + '/test.db'], input=json.dumps(request).encode())
        return json.loads(result), list(Fixture.requests)

    def test_three_protocols_one_request(self):
        for protocol in ['responses', 'chat', 'anthropic']:
            with self.subTest(protocol=protocol):
                result, requests = self.run_case(protocol)
                self.assertNotIn('error', result)
                self.assertTrue(result['valid'])
                self.assertEqual(len(requests), 1)
                self.assertNotIn('tools', requests[0])
                cp = result['checkpoint']
                self.assertEqual(cp['version'], 2)
                self.assertEqual(cp['through'], 8)
                self.assertTrue(cp['retained'])
                seqs = [m['seq'] for m in cp['retained']]
                self.assertEqual(seqs, sorted(set(seqs)))
                self.assertEqual(seqs[-1], 7)
                self.assertNotIn('assistant', [m['role'] for m in result['input']])
                text = json.dumps(result['input'], ensure_ascii=False)
                self.assertEqual(text.count('original-7'), 1)
                self.assertIn('历史参考摘要', text)

    def test_small_compactor_retains_recent_whole_turns(self):
        result, requests = self.run_case(data=rows(16, 2400), budget=4096)
        self.assertNotIn('error', result)
        self.assertEqual(len(requests), 1)
        through = result['checkpoint']['through']
        self.assertLess(through, 16)
        self.assertEqual(through % 2, 0)
        self.assertNotIn('original-16', json.dumps(requests[0]))
        self.assertIn('original-16', json.dumps(result['input']))
        self.assertIn('x' * 2400, json.dumps(result['input']))

    def test_overflow_shrinks_recent_end_only(self):
        result, requests = self.run_case(mode='overflow')
        self.assertNotIn('error', result)
        self.assertEqual(len(requests), 2)
        self.assertIn('original-8', json.dumps(requests[0]))
        self.assertNotIn('original-8', json.dumps(requests[1]))
        self.assertIn('original-8', json.dumps(result['input']))

    def test_failure_does_not_retry_or_return_checkpoint(self):
        for mode in ['error', 'empty']:
            result, requests = self.run_case(mode=mode)
            self.assertIn('error', result)
            self.assertNotIn('checkpoint', result)
            self.assertEqual(len(requests), 1)

    def test_advancing_compaction_for_smaller_chat_window(self):
        result, requests = self.run_case(data=rows(16, 1800), budget=4096, chat_budget=4096)
        self.assertNotIn('error', result)
        self.assertGreater(len(requests), 1)
        self.assertGreater(result['checkpoint']['through'], 2)

    def test_legacy_checkpoint_upgrades(self):
        # Build a real checksum through the native path, then reuse its v1-compatible subset.
        first, _ = self.run_case(data=rows(2, 500))
        old = dict(first['checkpoint'], version=1)
        old.pop('retained')
        result, requests = self.run_case(data=rows(6, 500), snapshot={'rows': rows(6, 500), 'checkpoint': old})
        self.assertNotIn('error', result)
        self.assertEqual(result['checkpoint']['version'], 2)
        self.assertIn('Checkpoint facts', json.dumps(requests))
        self.assertNotIn('original-1', json.dumps(requests))

    def test_indivisible_turn_failure_keeps_full_source(self):
        source = rows(2, 18000)
        result, requests = self.run_case(data=source, budget=4096)
        self.assertIn('完整对话', result['error'])
        self.assertEqual(requests, [])
        self.assertEqual(len(source[0]['output_text']), len('original-1 ') + 18000)

    def test_user_copy_budget_can_truncate_without_changing_source(self):
        source = rows(2, 8000)
        result, _ = self.run_case(data=source, budget=16384, chat_budget=1024)
        self.assertNotIn('error', result)
        retained = result['checkpoint']['retained'][0]['text']
        self.assertIn('原话中段省略', retained)
        self.assertTrue(retained.startswith('original-1'))
        self.assertGreater(len(source[0]['output_text']), len(retained))

    def test_no_benefit_keeps_original_context(self):
        source = rows(2, 10)
        result, requests = self.run_case(data=source, mode='no_gain')
        self.assertNotIn('error', result)
        self.assertTrue(result['unchanged'])
        self.assertEqual(result['checkpoint'], {})
        self.assertIn(source[-1]['output_text'], json.dumps(result['input']))
        self.assertEqual(len(requests), 1)

    def test_no_benefit_keeps_original_image(self):
        with tempfile.TemporaryDirectory() as directory:
            image = pathlib.Path(directory) / 'source.png'
            def chunk(kind, data):
                return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
            image.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', 12, 12, 8, 2, 0, 0, 0)) +
                              chunk(b'IDAT', zlib.compress((b'\0' + bytes([80, 40, 20]) * 12) * 12)) + chunk(b'IEND', b''))
            source = rows(2, 10)
            source[0]['parts'] = [{'type': 'image', 'path': str(image), 'signature': 'fixture'}]
            result, _ = self.run_case(data=source, mode='no_gain')
            self.assertTrue(result['unchanged'])
            self.assertIn(str(image), json.dumps(result['input']))
            self.assertTrue(image.exists())

    def test_small_window_refines_summary_then_advances(self):
        source = rows(6, 400)
        for row in source[-2:]:
            row['output_text'] += 'x' * 2600
        result, requests = self.run_case(data=source, mode='adaptive', budget=4096, chat_budget=2048)
        self.assertNotIn('error', result)
        self.assertFalse(result['unchanged'])
        self.assertEqual(result['checkpoint']['through'], 6)
        self.assertGreaterEqual(len(requests), 3)
        self.assertLessEqual(len(requests), 4)
        self.assertTrue(any('original-' not in json.dumps(request) for request in requests))

    def test_summary_budget_repair_is_bounded(self):
        result, requests = self.run_case(data=rows(8, 200), mode='ignore_budget', budget=4096)
        self.assertIn('精简预算', result['error'])
        self.assertEqual(len(requests), 2)

    def test_multi_tool_checkpoints_progress(self):
        exchange = lambda n: [wire(f'completed-tool-{n} ' + 't' * 3500)]
        exchanges = [exchange(n) for n in range(5)]
        question = rows(1)
        states = [{'input': [wire('large-history ' + 'h' * 19000)], 'exchanges': exchanges[:3]},
                  {'input': [wire('large-history ' + 'h' * 19000)], 'exchanges': exchanges}]
        result, requests = self.run_case(data=question, budget=8192, chat_budget=8192, states=states)
        self.assertNotIn('error', result)
        first, second = result['states']
        self.assertEqual(first['checkpoint']['tool_through'], 2)
        self.assertEqual(second['checkpoint']['tool_through'], 4)
        self.assertIn('completed-tool-4', json.dumps(second['input']))
        self.assertNotIn('completed-tool-0', json.dumps(second['input']))
        self.assertNotIn('completed-tool-0', json.dumps(requests[-1]))
        self.assertIn('original-1', json.dumps(second['input']))


if __name__ == '__main__':
    unittest.main()
