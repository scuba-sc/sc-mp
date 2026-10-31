import json
import subprocess
import sys
import unittest

EXE = sys.argv.pop(1)

class CliReceiptTests(unittest.TestCase):
    def run_mode(self, mode):
        p = subprocess.run([EXE, mode], capture_output=True, text=True)
        return p, json.loads(p.stdout) if p.stdout else None

    def test_synthetic_modes_require_applied_receipts(self):
        for mode in ('solo', 'listen', 'dedicated'):
            with self.subTest(mode=mode):
                p, result = self.run_mode(mode)
                self.assertEqual(p.returncode, 0, p.stderr)
                self.assertEqual(result['outcome'], 'applied')
                self.assertEqual(result['value'], 12)
                self.assertEqual(result['provider'], 'synthetic-only')
                self.assertFalse(result['real_topology_available'])

    def test_synthetic_modes_have_distinct_command_paths(self):
        for mode, paths in (('solo', ['local', 'local']),
                            ('listen', ['local', 'remote']),
                            ('dedicated', ['remote', 'remote'])):
            with self.subTest(mode=mode):
                p, result = self.run_mode(mode)
                self.assertEqual(p.returncode, 0, p.stderr)
                self.assertEqual(result.get('command_paths'), paths)
                self.assertEqual(result['outcome'], 'applied')
                self.assertEqual(result['value'], 12)

    def test_permanent_rejection_is_nonzero(self):
        p, result = self.run_mode('rejected')
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(result['outcome'], 'refused')
        self.assertEqual(result['reason'], 'invalid')
        self.assertEqual(result['value'], 0)

    def test_evicted_receipt_is_unknown_not_refused(self):
        p, result = self.run_mode('old')
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(result['outcome'], 'unknown')
        self.assertEqual(result['reason'], 'old')
        self.assertEqual(result['value'], 9)

    def test_world_refuses(self):
        p, _ = self.run_mode('sc-world')
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('synthetic', p.stderr)

if __name__ == '__main__':
    unittest.main()
