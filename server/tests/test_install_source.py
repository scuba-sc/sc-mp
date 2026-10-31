#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise source-copy group ownership checks without privileged operations."""
import unittest

if __package__:
    from . import test_install_full as full
else:
    import test_install_full as full


class SourcePrivateGroup(full.FullInstaller):
    def setUp(self):
        super().setUp()
        # Simulate a root-owned source writable by root's private group, gid 100.
        self.data['group'][0] = 'root:x:100:'
        self.data['gshadow'].append('root:!::')
        self.data['source_records'] = ['d 0 100 770 {src}']
        # Compatible with both the original and the enhanced parent harness.
        stub = self.bin / 'find'
        text = stub.read_text()
        old = "    print('d 0 0 700 ' + str(confined(args[0])))"
        new = "    print('\\n'.join(row.replace('{src}', str(confined(args[0]))) for row in data.get('source_records', ['d 0 0 700 {src}'])))"
        if old in text:
            stub.write_text(text.replace(old, new))

    def assert_source_refused(self):
        self.log.unlink(missing_ok=True)
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertIn('not the private group', result.stderr)
        log = self.log.read_text() if self.log.exists() else ''
        self.assertNotIn('cmake ', log)
        self.assertNotIn('install ', log)
        self.assertNotIn('apt-get ', log)

    def test_safe_private_group(self):
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_duplicate_group_gid_with_supplementary_member(self):
        self.data['group'].append('alias:x:100:intruder')
        self.data['numeric_group'] = ['root:x:100:', self.data['group'][1]]
        self.assert_source_refused()

    def test_duplicate_owner_uid(self):
        self.data['passwd'].append('alias:x:0:101::/nonexistent:/bin/false')
        self.data['numeric_passwd'] = self.data['passwd'][:2]
        self.assert_source_refused()

    def test_gshadow_member_or_admin(self):
        for row in ('root:!::intruder', 'root:!:intruder:', 'root:$6$hash::'):
            with self.subTest(row=row):
                self.data['gshadow'][-1] = row
                self.assert_source_refused()

    def test_local_view_hidden_aliases(self):
        for provider in ('effective', 'files'):
            for table, row in (('passwd', 'alias:x:0:101::/nonexistent:/bin/false'),
                               ('passwd', 'root:x:1001:100::/nonexistent:/bin/false'),
                               ('group', 'alias:x:100:intruder')):
                with self.subTest(provider=provider, table=table, row=row):
                    key = table if provider == 'effective' else 'files_' + table
                    original = self.data.get(key)
                    self.data[key] = self.data[table] + [row]
                    self.assert_source_refused()
                    if original is None:
                        del self.data[key]
                    else:
                        self.data[key] = original

    def test_inconsistent_local_gshadow(self):
        self.data['files_gshadow'] = ['sco-relay:!::', 'root:!::intruder']
        self.assert_source_refused()

    def test_missing_gshadow(self):
        self.data['gshadow'].pop()
        self.assert_source_refused()


# Keep the full-flow fixture without rerunning its unrelated inherited tests.
for _name in dir(full.FullInstaller):
    if _name.startswith('test_') and _name not in SourcePrivateGroup.__dict__:
        setattr(SourcePrivateGroup, _name, None)


if __name__ == '__main__':
    unittest.main()
