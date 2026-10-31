#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the whole shell installer; all writes live in a temporary root.

No root, containers, host accounts, network, compiler, or services are used.
The script's literal absolute paths are relocated, not its control flow.
Every external command is either an explicit read-only allowlist or a fake.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
if __package__:
    from .test_install_identity import IdentitySandbox, INSTALLER, STUB
else:
    from test_install_identity import IdentitySandbox, INSTALLER, STUB

FULL = r'''
base = pathlib.Path(os.environ['SANDBOX'])
def confined(value):
    p = pathlib.Path(value)
    assert p.is_absolute() and p.is_relative_to(base), value
    assert p.resolve().is_relative_to(base.resolve()), value
    return p
def log():
    with open(os.environ['IDENTITY_LOG'], 'a') as f:
        f.write(name + ' ' + ' '.join(args) + '\n')
def save():
    pathlib.Path(os.environ['IDENTITY_DATA']).write_text(json.dumps(data))
if name in ('awk', 'cut', 'sed', 'head', 'tr', 'grep'):
    os.execv(data['read_tools'][name], [name] + args)
elif name == 'mktemp':
    import tempfile
    template = args[-1]
    parent = confined(str(pathlib.Path(template).parent))
    parent.mkdir(parents=True, exist_ok=True)
    if '-d' in args:
        print(tempfile.mkdtemp(prefix='build-', dir=parent))
    else:
        fd, path = tempfile.mkstemp(prefix='.config-', dir=parent)
        os.close(fd)
        print(path)
elif name == 'mkdir':
    confined(args[-1]).mkdir()
elif name == 'tar':
    log()
    if '-cf' in args:
        confined(args[args.index('-cf') + 1]).touch()
    else:
        dest = confined(args[args.index('-C') + 1])
        shutil.copytree(base / 'source', dest, dirs_exist_ok=True)
elif name == 'stat':
    p = confined(args[-1])
    print('0:' + str(p.stat().st_nlink) + ':' + format(p.stat().st_mode & 0o777, 'o'))
elif name == 'getfacl':
    p = confined(args[-1])
    print(data.get('acl', {}).get(str(p.relative_to(base)), 'user::rwx\ngroup::r-x\nother::---'))
elif name == 'find':
    print('d 0 0 700 ' + str(confined(args[0])))
elif name == 'rm':
    for arg in args:
        if arg.startswith('-'): continue
        p = confined(arg)
        if p.is_dir(): shutil.rmtree(p)
        else: p.unlink(missing_ok=True)
elif name == 'cmake':
    log()
    if args == ['--version']: print('cmake version 3.25.1')
    if '--build' in args:
        out = confined(args[1]) / 'server' / 'sco-relay'
        out.parent.mkdir(parents=True)
        out.write_text('fake relay binary\n')
        out.chmod(0o755)
        if data.get('config_symlink_after_build'):
            config = base / 'etc/sco-relay/sco-relay.conf'
            config.unlink()
            config.symlink_to(base / 'unrelated-file')
        data.update(data.get('after_build', {}))
        save()
elif name == 'nproc': print('2')
elif name == 'nologin': sys.exit(0)
elif name in ('groupadd', 'useradd'):
    log()
    if name == 'groupadd':
        data['group'].append('sco-relay:x:450:')
        data['gshadow'] = ['sco-relay:!::']
    else:
        data['passwd'].append('sco-relay:x:450:450::/nonexistent:/usr/sbin/nologin')
        data['shadow'] = ['sco-relay:!:20000:0:99999:7:::']
        data.update(data.get('after_create', {}))
    save()
elif name == 'install':
    log()
    dest = confined(args[-1])
    if '-d' in args: dest.mkdir(parents=True, exist_ok=True)
    else:
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(confined(args[-2]), dest)
    dest.chmod(int(args[args.index('-m') + 1], 8))
elif name == 'od': print('ab' * 32)
elif name in ('chown', 'chmod'):
    log()
    p = confined(args[-1])
    if name == 'chmod': p.chmod(int(args[0], 8))
elif name == 'mv':
    confined(args[0]).rename(confined(args[1]))
elif name in ('systemctl', 'sleep', 'setpriv', 'su'):
    log()
else:
    raise AssertionError('unexpected command: ' + name + ' ' + repr(args))
'''


class FullInstaller(IdentitySandbox):
    def setUp(self):
        super().setUp()
        self.source = self.base / 'source'
        (self.source / 'server' / 'deploy').mkdir(parents=True)
        for relative in ('CMakeLists.txt', 'server/main.cpp', 'server/sco-relay.example.conf',
                         'server/deploy/sco-relay.service'):
            origin = INSTALLER.parents[2] / relative
            shutil.copyfile(origin, self.source / relative)
        script = INSTALLER.read_text()
        # Relocate literal filesystem accesses, retaining the real shell flow.
        for prefix in ('/etc', '/usr/local', '/usr/lib/userdb', '/run', '/var/tmp'):
            script = script.replace(prefix, str(self.base) + prefix)
        self.script = self.source / 'server' / 'deploy' / 'install.sh'
        self.script.write_text(script)
        for path in ('etc/ssl/certs', 'run/systemd/system', 'var/tmp'):
            (self.base / path).mkdir(parents=True)
        (self.base / 'etc/ssl/certs/ca-certificates.crt').write_text('fixture')
        self.data['read_tools']['grep'] = shutil.which('grep')
        # Replace only the mutation trap/read tools; preserve identity fakes.
        prefix = STUB[:STUB.index("elif name == 'stat'")]
        for stub in self.bin.iterdir():
            stub.write_text('#!' + sys.executable + '\nimport shutil\n' + prefix + 'else:\n' +
                            ''.join('    ' + line + '\n' for line in FULL.splitlines()))
            stub.chmod(0o755)

    def run_installer(self):
        fixture = self.base / 'identities.json'
        fixture.write_text(json.dumps(self.data))
        (self.base / 'etc/nsswitch.conf').write_text(self.data.get('nss',
            'passwd: files systemd\ngroup: files systemd\nshadow: files\ngshadow: files\n'))
        (self.base / 'etc/login.defs').write_text(self.data.get('ranges', ''))
        env = {'PATH': str(self.bin), 'IDENTITY_DATA': str(fixture), 'SANDBOX': str(self.base),
               'IDENTITY_LOG': str(self.log), 'LC_ALL': 'C'}
        return subprocess.run(['/bin/sh', str(self.script)], env=env,
                              text=True, capture_output=True, timeout=30)

    def test_config_hardlink_refused_before_mutation(self):
        directory = self.base / 'etc/sco-relay'
        directory.mkdir()
        target = self.base / 'unrelated-file'
        target.write_text('existing-secret')
        os.link(target, directory / 'sco-relay.conf')
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe config', result.stderr)
        self.assertFalse(self.log.exists())
        self.assertEqual(target.read_text(), 'existing-secret')

    def test_config_named_acl_refused_before_mask_can_be_widened(self):
        directory = self.base / 'etc/sco-relay'
        directory.mkdir()
        config = directory / 'sco-relay.conf'
        config.write_text('existing-secret')
        config.chmod(0o600)
        # Named intruder read is masked off at 0600; old chmod 0640 reactivates it.
        self.data['acl'] = {'etc/sco-relay/sco-relay.conf':
                            'user::rw-\nuser:1001:r--\ngroup::---\nmask::---\nother::---'}
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe config ACL', result.stderr)
        self.assertFalse(self.log.exists())
        self.assertEqual(config.stat().st_mode & 0o777, 0o600)

    def test_config_parent_default_acl_refused(self):
        self.data['acl'] = {'etc': 'user::rwx\ngroup::r-x\nother::r-x\n'
                            'default:user::rwx\ndefault:user:1001:r-x\ndefault:mask::r-x'}
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe config ACL', result.stderr)
        self.assertFalse(self.log.exists())

    def test_missing_acl_tool_refused_before_mutation(self):
        (self.bin / 'getfacl').unlink()
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('getfacl is required', result.stderr)
        self.assertFalse(self.log.exists())

    def test_post_build_acl_drift_refused_before_install(self):
        self.data['after_build'] = {'acl': {'etc':
            'user::rwx\nuser:1001:rwx\ngroup::r-x\nmask::rwx\nother::r-x'}}
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe config ACL', result.stderr)
        self.assertIn('cmake --build', self.log.read_text())
        self.assertNotIn('install ', self.log.read_text())

    def test_full_systemd_install_and_config_preserving_upgrade(self):
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        config = self.base / 'etc/sco-relay/sco-relay.conf'
        content = config.read_text()
        self.assertIn('passphrase = ' + 'ab' * 32, content)
        self.assertEqual(config.stat().st_mode & 0o777, 0o640)
        self.assertIn('done. The relay runs', result.stdout)
        self.assertNotIn('ab' * 32, result.stdout + result.stderr)
        self.assertTrue((self.base / 'usr/local/bin/sco-relay').is_file())
        self.assertTrue((self.base / 'etc/systemd/system/sco-relay.service').is_file())
        self.assertEqual(list((self.base / 'var/tmp').iterdir()), [])
        config.write_text(content.replace('ab' * 32, 'keep-this-existing-secret'))
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('keep-this-existing-secret', config.read_text())
        self.assertIn('systemctl restart sco-relay.service', self.log.read_text())

    def test_files_only_full_install(self):
        self.data['nss'] = 'passwd: files\ngroup: files\nshadow: files\ngshadow: files\n'
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_dynamic_provider_endpoint_is_allowed(self):
        directory = self.base / 'run/systemd/userdb'
        directory.mkdir()
        (directory / 'io.systemd.DynamicUser').touch()
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unsupported_nss_policies_refused(self):
        for source in ('files sss', 'files ldap', 'compat', 'systemd files',
                       'files [SUCCESS=merge] systemd'):
            with self.subTest(source=source):
                self.data['nss'] = ('passwd: ' + source + '\ngroup: files\n'
                                    'shadow: files\ngshadow: files\n')
                result = self.run_installer()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.log.exists())

    def test_multiplexer_endpoint_refused(self):
        directory = self.base / 'run/systemd/userdb'
        directory.mkdir()
        (directory / 'io.systemd.Multiplexer').touch()
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('external systemd userdb', result.stderr)
        self.assertFalse(self.log.exists())

    def test_nss_whitespace_and_comments(self):
        self.data['nss'] = ('passwd:\tfiles\t systemd # local first\n'
                            'group:  files   systemd\nshadow: files\ngshadow: files\n')
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_custom_system_ranges(self):
        self.data['ranges'] = 'SYS_UID_MIN 2000\nSYS_UID_MAX 2999\nSYS_GID_MIN 3000\nSYS_GID_MAX 3999\n'
        self.data['passwd'][1] = self.data['passwd'][1].replace(':450:450:', ':2450:3450:')
        self.data['group'][1] = 'sco-relay:x:3450:'
        self.data['groups'] = '3450'
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_fresh_install_creates_and_validates_identity(self):
        self.data['passwd'].pop()
        self.data['group'].pop()
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('groupadd --system sco-relay', self.log.read_text())
        self.assertIn('useradd --system', self.log.read_text())

    def test_created_identity_is_checked_before_file_install(self):
        self.data['passwd'].pop()
        self.data['group'].pop()
        self.data['after_create'] = {'groups': '450 27'}
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('supplementary groups', result.stderr)
        self.assertNotIn('Nothing was changed', result.stderr)
        self.assertIn('useradd --system', self.log.read_text())
        self.assertNotIn('install ', self.log.read_text())

    def test_full_foreground_install(self):
        (self.base / 'run/systemd/system').rmdir()
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('setpriv --reuid=sco-relay --regid=sco-relay --init-groups', self.log.read_text())
        self.assertNotIn('systemctl ', self.log.read_text())
        self.assertEqual(list((self.base / 'var/tmp').iterdir()), [])

    def test_config_symlink_refused_without_chowning_target(self):
        directory = self.base / 'etc/sco-relay'
        directory.mkdir()
        target = self.base / 'unrelated-file'
        target.write_text('must stay untouched')
        (directory / 'sco-relay.conf').symlink_to(target)
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe config path', result.stderr)
        self.assertFalse(self.log.exists())
        self.assertEqual(target.read_text(), 'must stay untouched')

    def test_dangling_config_directory_symlink_refused(self):
        (self.base / 'etc/sco-relay').symlink_to(self.base / 'missing')
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe config path', result.stderr)
        self.assertFalse(self.log.exists())

    def test_post_build_config_symlink_refused_before_chown_or_chmod(self):
        directory = self.base / 'etc/sco-relay'
        directory.mkdir()
        (directory / 'sco-relay.conf').write_text('original config')
        target = self.base / 'unrelated-file'
        target.write_text('must stay untouched')
        target.chmod(0o600)
        self.data['config_symlink_after_build'] = True
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe config path', result.stderr)
        self.assertIn('cmake --build', self.log.read_text())
        self.assertNotIn('install ', self.log.read_text())
        self.assertNotIn('chown ', self.log.read_text())
        self.assertNotIn('chmod ', self.log.read_text())
        self.assertEqual(target.read_text(), 'must stay untouched')
        self.assertEqual(target.stat().st_mode & 0o777, 0o600)

    def test_provider_records_refused_before_build(self):
        userdb = self.base / 'etc/userdb'
        userdb.mkdir()
        (userdb / 'hidden.user').write_text('{}')
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('external systemd userdb', result.stderr)
        self.assertFalse(self.log.exists())

    def test_missing_local_record_is_refused(self):
        self.data['files_passwd'] = [self.data['passwd'][0]]
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('identity is not local', result.stderr)
        self.assertFalse(self.log.exists())

    def test_numeric_collision_is_refused(self):
        self.data['numeric_passwd'] = ['alias:x:450:451::/nonexistent:/bin/false']
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('inconsistent numeric lookup', result.stderr)
        self.assertFalse(self.log.exists())

    def test_invalid_ranges_refused(self):
        for ranges in ('SYS_UID_MAX 65519\n', 'SYS_UID_MIN 500\nSYS_UID_MAX 400\n',
                       'SYS_UID_MIN 0101\n', 'SYS_UID_MAX 999\nSYS_UID_MAX 999\n'):
            with self.subTest(ranges=ranges):
                self.data['ranges'] = ranges
                result = self.run_installer()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('unsupported system ID ranges', result.stderr)
                self.assertFalse(self.log.exists())

    def test_post_build_recheck_refuses_before_install(self):
        self.data['after_build'] = {'group': ['root:x:0:', 'sco-relay:x:450:intruder']}
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsafe existing sco-relay identity', result.stderr)
        self.assertIn('cmake --build', self.log.read_text())
        self.assertNotIn('install ', self.log.read_text())
        self.assertNotIn('systemctl ', self.log.read_text())
        self.assertEqual(list((self.base / 'var/tmp').iterdir()), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
