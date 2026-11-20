"""Exercise diagnostic polling without connecting to a device or importing its runner."""
import ast
import json
import pathlib
import tempfile
import unittest


class DiagnosticPollingTest(unittest.TestCase):
    def scenario(self, *, image=False, fresh_at=None, changed_owner=False):
        source = pathlib.Path(__file__).with_name('display_config.py')
        tree = ast.parse(source.read_text())
        function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'state')
        clock = [0.0]
        commands = []
        reads = [0]

        def shell(command):
            commands.append(command)
            if command.startswith('awk '):
                return 'a00'  # USR1 and USR2 handlers are installed.
            if command.startswith('cat '):
                reads[0] += 1
                return json.dumps({'pid': '7', 'diagnostic_time':
                                  2 if fresh_at is not None and clock[0] >= fresh_at else 1})
            return ''

        def wait(fn, timeout=8):
            end = clock[0] + timeout
            while clock[0] < end:
                result = fn()
                if result:
                    return result
                clock[0] += .1
            raise AssertionError('Device transition timed out')

        with tempfile.TemporaryDirectory() as directory:
            env = {'json': json, 'shell': shell, 'wait': wait, 'DATA': '/owned/data',
                   'OUT': pathlib.Path(directory),
                   'pid': lambda: '8' if changed_owner and reads[0] else '7',
                   'time': type('Clock', (), {'monotonic': staticmethod(lambda: clock[0])})}
            exec(compile(ast.Module(body=[function], type_ignores=[]), str(source), 'exec'), env)
            error = None
            try:
                result = env['state'](image)
            except (AssertionError, RuntimeError) as exc:
                error = exc
                result = None
            evidence = pathlib.Path(directory) / 'diagnostic-timeout.json'
            return commands, clock[0], result, error, json.loads(evidence.read_text()) if evidence.exists() else None

    def test_numeric_request_can_be_reissued_without_resetting_deadline(self):
        commands, elapsed, result, error, _ = self.scenario(fresh_at=.5)
        self.assertIsNone(error)
        self.assertEqual(result['diagnostic_time'], 2)
        self.assertGreaterEqual(commands.count('kill -USR2 7'), 2)
        self.assertLess(elapsed, 8)

    def test_persistent_staleness_fails_and_keeps_bounded_evidence(self):
        _, elapsed, _, error, evidence = self.scenario()
        self.assertIsInstance(error, AssertionError)
        self.assertLess(elapsed, 8.2)
        self.assertEqual(evidence['owner_pid'], '7')
        self.assertLessEqual(len(evidence['observed']), 8)

    def test_image_request_is_never_reissued(self):
        commands, _, _, error, _ = self.scenario(image=True)
        self.assertIsInstance(error, AssertionError)
        self.assertEqual(commands.count('kill -USR1 7'), 1)
        self.assertNotIn('kill -USR2 7', commands)

    def test_owner_replacement_fails_without_signalling_replacement(self):
        commands, _, _, error, evidence = self.scenario(changed_owner=True)
        self.assertIsInstance(error, RuntimeError)
        self.assertEqual(evidence['owner_pid'], '7')
        self.assertFalse(any(command.startswith('kill ') and command.endswith(' 8') for command in commands))


if __name__ == '__main__':
    unittest.main()
