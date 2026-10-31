#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the real installer with a closed PATH: no host identity/service commands."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

INSTALLER = Path(__file__).resolve().parents[1] / 'deploy' / 'install.sh'


def relocate_paths(script, base, prefixes):
    # Rewrite original source paths in one pass: the sandbox root can itself
    # contain /run or /etc (for example, a /runner workspace).
    pattern = '|'.join(re.escape(p) for p in sorted(prefixes, key=len, reverse=True))
    return re.sub(r'(?<![A-Za-z0-9_./-])(?:' + pattern + r')(?![A-Za-z0-9_.-])',
                  lambda match: str(base) + match.group(), script)


STUB = r'''
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
data = json.loads(pathlib.Path(os.environ['IDENTITY_DATA']).read_text())
if name == 'uname':
    print('Linux')
elif name == 'dirname':
    print(str(pathlib.Path(args[0]).parent))
elif name == 'getent':
    provider = None
    if args[:1] == ['-s']:
        provider, args = args[1], args[2:]
    table = args[0]
    if data.get('lookup_error') == table:
        sys.exit(3)
    rows = data.get('keyed_' + table, data[table]) if len(args) > 1 else data[table]
    if provider:
        rows = data.get(provider + '_' + table, data[table])
    if len(args) > 1:
        index = 2 if args[1].isdigit() and table in ('passwd', 'group') else 0
        if index == 2 and not provider:
            rows = data.get('numeric_' + table, rows)
        rows = [r for r in rows if r.split(':')[index] == args[1]]
        if not rows:
            sys.exit(2)
    print('\n'.join(rows))
elif name == 'id':
    if args == ['-u']:
        print('0')
    elif 'sco-relay' not in '\n'.join(data['passwd']):
        sys.exit(1)
    elif args == ['-G', 'sco-relay']:
        print(data.get('groups', '450'))
    else:
        print('uid=450(sco-relay) gid=450(sco-relay) groups=450(sco-relay)')
elif name == 'stat':
    print('0:1:755')
elif name == 'getfacl':
    print('user::rwx\ngroup::r-x\nother::r-x')
elif name in ('awk', 'cut', 'sed', 'head', 'tr'):
    args = [os.environ['IDENTITY_NSS'] if a == '/etc/nsswitch.conf' else
            os.environ['IDENTITY_RANGES'] if a == '/etc/login.defs' else a for a in args]
    os.execv(data['read_tools'][name], [name] + args)
else:
    # All potentially mutating commands are traps, never real host commands.
    with open(os.environ['IDENTITY_LOG'], 'a') as log:
        log.write(name + ' ' + ' '.join(args) + '\n')
    pathlib.Path(os.environ['IDENTITY_SENTINEL']).write_text('MUTATED')
    print('sandbox mutation boundary: ' + name, file=sys.stderr)
    sys.exit(97)
'''


class IdentitySandbox(unittest.TestCase):
    def setUp(self):
        import shutil
        self.temp = tempfile.TemporaryDirectory(prefix='.relay-identity-', dir=INSTALLER.parents[2])
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        self.data = {
            'passwd': ['root:x:0:0:root:/root:/bin/sh',
                       'sco-relay:x:450:450::/nonexistent:/usr/sbin/nologin'],
            'group': ['root:x:0:', 'sco-relay:x:450:'],
            'shadow': ['sco-relay:!:20000:0:99999:7:::'],
            'gshadow': ['sco-relay:!::'],
            'read_tools': {n: shutil.which(n) for n in ('awk', 'cut', 'sed', 'head', 'tr')},
        }
        self.log = self.base / 'mutations'
        self.sentinel = self.base / 'accounts-files-service'
        self.sentinel.write_text('UNCHANGED')
        commands = ('uname dirname getent id awk cut sed head tr tar mktemp mkdir rmdir rm '
                    'cmake g++ make apt-get nproc groupadd useradd usermod groupmod '
                    'install od grep chown chmod mv systemctl sleep setpriv su nologin find stat getfacl')
        for name in commands.split():
            stub = self.bin / name
            stub.write_text('#!' + sys.executable + '\n' + STUB)
            stub.chmod(0o755)

    def run_installer(self):
        fixture = self.base / 'identities.json'
        fixture.write_text(json.dumps(self.data))
        nss = self.base / 'nsswitch.conf'
        nss.write_text(self.data.get('nss', 'passwd: files\ngroup: files\nshadow: files\ngshadow: files\n'))
        ranges = self.base / 'login.defs'
        ranges.write_text('')
        env = {'PATH': str(self.bin), 'IDENTITY_RANGES': str(ranges), 'IDENTITY_NSS': str(nss), 'IDENTITY_DATA': str(fixture),
               'IDENTITY_LOG': str(self.log), 'IDENTITY_SENTINEL': str(self.sentinel),
               'LC_ALL': 'C'}
        etc = self.base / 'etc'
        etc.mkdir(exist_ok=True)
        run = self.base / 'run'
        run.mkdir(exist_ok=True)
        (etc / 'nsswitch.conf').write_text(nss.read_text())
        (etc / 'login.defs').write_text('')
        # macOS /etc is itself a symlink; all filesystem preflight belongs in the sandbox.
        script = self.base / 'install.sh'
        text = relocate_paths(INSTALLER.read_text(), self.base, ('/etc', '/run'))
        text = text.replace('root=$(cd "$here/../.." && pwd)', 'root=' + str(INSTALLER.parents[2]))
        text = text.replace('here=$(cd "$(dirname "$0")" && pwd)', 'here=' + str(INSTALLER.parent))
        script.write_text(text)
        result = subprocess.run(['/bin/sh', str(script)], env=env,
                                text=True, capture_output=True, timeout=15)
        return result

    def assert_refused(self):
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe existing sco-relay identity', result.stderr)
        self.assertFalse(self.log.exists(), result.stderr)
        self.assertEqual(self.sentinel.read_text(), 'UNCHANGED')

class InstallerIdentity(IdentitySandbox):
    def test_path_relocation_with_runner_workspace(self):
        base = Path('/runner/workspace/fixture')
        original = ('config=/etc/nsswitch.conf\nlock=/run/sco-relay.lock\n'
                    'unchanged=/runner/data /var/run/socket x/run/file /etc2/x\n')
        expected = ('config=/runner/workspace/fixture/etc/nsswitch.conf\n'
                    'lock=/runner/workspace/fixture/run/sco-relay.lock\n'
                    'unchanged=/runner/data /var/run/socket x/run/file /etc2/x\n')
        self.assertEqual(relocate_paths(original, base, ('/etc', '/run')), expected)

    def test_shared_explicit_group_member(self):
        self.data['group'][1] += 'intruder'
        self.assert_refused()

    def test_other_primary_group_member(self):
        self.data['passwd'].append('intruder:x:1001:450::/home/intruder:/bin/sh')
        self.assert_refused()

    def test_missing_user_shared_group(self):
        self.data['passwd'].pop()
        self.data['group'][1] += 'intruder'
        self.assert_refused()

    def test_privileged_uid(self):
        self.data['passwd'][1] = self.data['passwd'][1].replace(':450:450:', ':0:450:')
        self.assert_refused()

    def test_root_group(self):
        self.data['group'][1] = 'sco-relay:x:0:'
        self.assert_refused()

    def test_supplementary_privilege(self):
        self.data['groups'] = '450 27'
        self.assert_refused()

    def test_login_shell(self):
        self.data['passwd'][1] = self.data['passwd'][1].replace('/usr/sbin/nologin', '/bin/sh')
        self.assert_refused()

    def test_legacy_passwd_hash_is_rejected_even_when_shadow_locked(self):
        self.data['passwd'][1] = self.data['passwd'][1].replace('sco-relay:x:', 'sco-relay:$6$hash:')
        self.assert_refused()

    def test_unlocked_password(self):
        self.data['shadow'][0] = self.data['shadow'][0].replace(':!:', ':$6$hash:')
        self.assert_refused()

    def test_non_enumerable_nss_source(self):
        self.data['nss'] = 'passwd: files sss\ngroup: files sss\nshadow: files\ngshadow: files\n'
        self.assert_refused()

    def test_non_enumerated_account_collision(self):
        self.data['keyed_passwd'] = [self.data['passwd'].pop()]
        self.data['keyed_group'] = [self.data['group'].pop()]
        self.assert_refused()

    def test_additional_unsafe_identity_shapes(self):
        import copy
        original = copy.deepcopy(self.data)
        cases = {
            'ordinary_uid': ('passwd', 1, 'sco-relay:x:1000:450::/nonexistent:/usr/sbin/nologin'),
            'wrong_primary_gid': ('passwd', 1, 'sco-relay:x:450:27::/nonexistent:/usr/sbin/nologin'),
            'real_home': ('passwd', 1, 'sco-relay:x:450:450::/home/sco-relay:/usr/sbin/nologin'),
            'empty_password': ('shadow', 0, 'sco-relay::20000:0:99999:7:::'),
            'group_admin': ('gshadow', 0, 'sco-relay:!:intruder:'),
            'group_password': ('gshadow', 0, 'sco-relay:$6$hash::'),
            'hidden_group_member': ('gshadow', 0, 'sco-relay:!::intruder'),
            'ordinary_gid': ('group', 1, 'sco-relay:x:1000:'),
        }
        for name, (table, index, row) in cases.items():
            with self.subTest(name=name):
                self.data = copy.deepcopy(original)
                self.data[table][index] = row
                self.assert_refused()
        for table in ('passwd', 'group', 'shadow', 'gshadow'):
            with self.subTest(lookup_error=table):
                self.data = copy.deepcopy(original)
                self.data['lookup_error'] = table
                self.assert_refused()
        for table, row in (
                ('passwd', 'alias:x:450:451::/nonexistent:/bin/false'),
                ('group', 'alias:x:450:')):
            with self.subTest(alias=table):
                self.data = copy.deepcopy(original)
                self.data[table].append(row)
                self.assert_refused()

    def test_fresh_identity_reaches_lock_boundary(self):
        self.data['passwd'].pop()
        self.data['group'].pop()
        result = self.run_installer()
        self.assertIn('cannot acquire installation lock', result.stderr)
        self.assertEqual(self.log.read_text().split()[0], 'mkdir')

    def test_safe_existing_identity_reaches_lock_boundary(self):
        result = self.run_installer()
        self.assertIn('cannot acquire installation lock', result.stderr)
        self.assertEqual(self.log.read_text().split()[0], 'mkdir')


if __name__ == '__main__':
    unittest.main(verbosity=2)
