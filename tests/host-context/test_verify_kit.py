#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise source-kit rejection paths with fixture-specific pins."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
QUALIFIER = ROOT / 'spikes' / 'host-context' / 'qualify.py'
HELPER = ROOT / 'cmake' / 'VerifyExtractedKit.cmake'
spec = importlib.util.spec_from_file_location('host_context_qualifier', QUALIFIER)
q = importlib.util.module_from_spec(spec)
spec.loader.exec_module(q)
CMAKE = 'cmake'
REAL_ARCHIVE = None
EXPECTED_TESTS = 21


def sha(data):
    return hashlib.sha256(data).hexdigest()


def entry(name, data=b'', mode=stat.S_IFREG | 0o644):
    item = zipfile.ZipInfo(name)
    item.create_system = 3
    item.external_attr = mode << 16
    return item, data


class ArchiveChecks(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.old_archive, self.old_inventory = q.ARCHIVE, q.INVENTORY
        self.addCleanup(self.tmp.cleanup)
        self.addCleanup(self.restore)

    def restore(self):
        q.ARCHIVE, q.INVENTORY = self.old_archive, self.old_inventory

    def make_archive(self, extra=()):
        data = b'fixture\n'
        inventory = (sha(data) + '  CMakeLists.txt\n').encode()
        archive = self.root / 'fixture.zip'
        with zipfile.ZipFile(archive, 'w') as out:
            for item, value in [entry(q.PREFIX + '/CMakeLists.txt', data),
                                entry(q.PREFIX + '/SHA256SUMS', inventory), *extra]:
                out.writestr(item, value)
        q.ARCHIVE = sha(archive.read_bytes())
        q.INVENTORY = sha(inventory)
        return archive

    def test_literal_production_pins(self):
        self.assertEqual(q.CORE, '4c09ab2f73caa3d1103214adc56193d2434a8795')
        self.assertEqual(q.ARCHIVE, '19e80a7d1661475a86aaed80d1827cec8c5e4868d990bca27b089ff8e6d5ed07')
        self.assertEqual(q.INVENTORY, '2206c288af7997a138156485cc75b33613102af28feaf99621f2d99d305658b8')

    def test_too_many_entries_refused_before_extraction(self):
        extra = [entry(q.PREFIX + '/extra-' + str(i), b'') for i in range(2047)]
        archive = self.make_archive(extra)  # Two required files plus 2,047 extras.
        output = self.root / 'not-extracted'
        with self.assertRaisesRegex(ValueError, 'archive exceeds extraction bounds'):
            q.extract(archive, output)
        self.assertFalse(output.exists())

    def test_uncompressed_size_limit_refused_before_extraction(self):
        archive = self.make_archive()
        with zipfile.ZipFile(archive, 'a', compression=zipfile.ZIP_DEFLATED) as out:
            item, _ = entry(q.PREFIX + '/large', b'')
            item.compress_type = zipfile.ZIP_DEFLATED
            with out.open(item, 'w') as stream:
                block = b'\0' * (1024 * 1024)
                for _ in range(65):
                    stream.write(block)
        q.ARCHIVE = sha(archive.read_bytes())
        output = self.root / 'not-extracted'
        with self.assertRaisesRegex(ValueError, 'archive exceeds extraction bounds'):
            q.extract(archive, output)
        self.assertFalse(output.exists())

    def test_valid_synthetic_archive(self):
        archive = self.make_archive()
        kit, count = q.extract(archive, self.root / 'out')
        self.assertEqual(count, 1)
        self.assertEqual((kit / 'CMakeLists.txt').read_bytes(), b'fixture\n')

    def test_valid_explicit_directory(self):
        archive = self.make_archive([entry(q.PREFIX + '/empty/', b'', stat.S_IFDIR | 0o755)])
        kit, count = q.extract(archive, self.root / 'out')
        self.assertEqual(count, 1)
        self.assertTrue((kit / 'empty').is_dir())

    def test_unsafe_archive_entries_are_refused_before_extraction(self):
        cases = {
            'parent traversal': entry(q.PREFIX + '/../escape', b'x'),
            'absolute path': entry('/escape', b'x'),
            'backslash': entry(q.PREFIX + '/bad\\name', b'x'),
            'empty component': entry(q.PREFIX + '/bad//name', b'x'),
            'symlink': entry(q.PREFIX + '/link', b'target', stat.S_IFLNK | 0o777),
            'fifo': entry(q.PREFIX + '/pipe', b'', stat.S_IFIFO | 0o600),
            'directory with regular metadata': entry(q.PREFIX + '/bad-dir/', b'', stat.S_IFREG | 0o644),
            'file with directory metadata': entry(q.PREFIX + '/bad-file', b'', stat.S_IFDIR | 0o755),
            'duplicate': entry(q.PREFIX + '/CMakeLists.txt', b'fixture\n'),
            'casefold collision': entry(q.PREFIX + '/cmakelists.txt', b'other'),
            'file as parent': entry(q.PREFIX + '/CMakeLists.txt/child', b'x'),
        }
        for label, bad in cases.items():
            with self.subTest(label=label):
                archive = self.make_archive([bad])
                output = self.root / 'not-extracted'
                with self.assertRaisesRegex(ValueError, 'unsafe or duplicate archive entry'):
                    q.extract(archive, output)
                self.assertFalse(output.exists())

    def test_real_archive_wrong_hash_rejected_before_extraction(self):
        self.assertIsNotNone(REAL_ARCHIVE)
        data = bytearray(REAL_ARCHIVE.read_bytes())
        self.assertEqual(sha(data), self.old_archive)
        data[-1] ^= 1
        changed = self.root / 'changed-real-kit.zip'
        changed.write_bytes(data)
        output = self.root / 'not-extracted'
        with self.assertRaisesRegex(ValueError, 'archive hash mismatch'):
            q.extract(changed, output)
        self.assertFalse(output.exists())


class ExtractedTreeChecks(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.kit = self.root / 'kit'
        self.kit.mkdir()
        self.payload = b'file(WRITE "${CMAKE_BINARY_DIR}/sentinel" "executed")\n'
        (self.kit / 'CMakeLists.txt').write_bytes(self.payload)
        self.set_inventory(sha(self.payload) + '  CMakeLists.txt\n')
        self.project = self.root / 'project'
        self.project.mkdir()
        (self.project / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(verification NONE)\n'
            'include("' + HELPER.as_posix() + '")\n'
            'sc_mp_verify_extracted_kit("${KIT}" "${EXPECTED}" "fixture")\n'
            'add_subdirectory("${KIT}" kit)\n')

    def set_inventory(self, value):
        encoded = value.encode()
        (self.kit / 'SHA256SUMS').write_bytes(encoded)
        self.expected = sha(encoded)

    def run_cmake(self, kit=None, expected=None, python=None):
        build = self.root / 'build'
        command = [CMAKE, '-S', str(self.project), '-B', str(build),
                   '-DKIT=' + str(kit or self.kit), '-DEXPECTED=' + (expected or self.expected)]
        if python:
            command.append('-D_kit_python=' + str(python))
        result = subprocess.run(command,
                                text=True, capture_output=True, timeout=30)
        return result, build / 'sentinel'

    def assert_rejected(self, kit=None, expected=None):
        result, sentinel = self.run_cmake(kit, expected)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('kit verification failed', result.stdout + result.stderr)
        self.assertFalse(sentinel.exists(), 'SDK CMake executed before verification')

    def test_valid_tree_reaches_subdirectory(self):
        result, sentinel = self.run_cmake()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(sentinel.exists())

    def test_wrong_inventory_hash(self):
        self.assert_rejected(expected='0' * 64)

    def test_failed_python_version_check(self):
        result, sentinel = self.run_cmake(python=self.root / 'missing-python3')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Python 3.8 or newer is required', result.stdout + result.stderr)
        self.assertFalse(sentinel.exists())

    def test_modified_content(self):
        (self.kit / 'CMakeLists.txt').write_bytes(b'changed')
        self.assert_rejected()

    def test_extra_hidden_file(self):
        (self.kit / '.hidden').write_bytes(b'extra')
        self.assert_rejected()

    def test_missing_file(self):
        (self.kit / 'CMakeLists.txt').unlink()
        self.assert_rejected()

    def test_symlink_file(self):
        target = self.root / 'same-content'
        target.write_bytes(self.payload)
        (self.kit / 'CMakeLists.txt').unlink()
        (self.kit / 'CMakeLists.txt').symlink_to(target)
        self.assert_rejected()

    def test_symlink_directory(self):
        (self.kit / 'link').symlink_to(self.root, target_is_directory=True)
        self.assert_rejected()

    def test_symlink_inventory(self):
        inventory = self.root / 'inventory'
        inventory.write_bytes((self.kit / 'SHA256SUMS').read_bytes())
        (self.kit / 'SHA256SUMS').unlink()
        (self.kit / 'SHA256SUMS').symlink_to(inventory)
        self.assert_rejected()

    def test_symlink_root(self):
        link = self.root / 'link-root'
        link.symlink_to(self.kit, target_is_directory=True)
        self.assert_rejected(kit=link)

    def test_symlink_ancestor(self):
        link = self.root / 'link-parent'
        link.symlink_to(self.root, target_is_directory=True)
        self.assert_rejected(kit=link / 'kit')

    def test_symlink_parent_component_refused_before_normalization(self):
        target = self.root / 'elsewhere' / 'child'
        target.mkdir(parents=True)
        link = self.root / 'link-parent'
        link.symlink_to(target, target_is_directory=True)
        result, sentinel = self.run_cmake(kit=link / '..' / 'kit')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('parent component in kit root path', result.stdout + result.stderr)
        self.assertFalse(sentinel.exists())

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'FIFO requires POSIX')
    def test_nonregular_entry(self):
        os.mkfifo(self.kit / 'pipe')
        self.assert_rejected()

    def test_unsafe_and_malformed_inventory(self):
        valid = sha(self.payload) + '  CMakeLists.txt\n'
        cases = {
            'parent': '0' * 64 + '  ../escape\n',
            'absolute': '0' * 64 + '  /escape\n',
            'backslash': '0' * 64 + '  path\\escape\n',
            'duplicate': valid + valid,
            'bad hash': 'z' * 64 + '  bad\n',
            'dot': '0' * 64 + '  x/./y\n',
        }
        for label, inventory in cases.items():
            with self.subTest(label=label):
                self.set_inventory(inventory)
                self.assert_rejected()


def complete(result):
    return (result.testsRun == EXPECTED_TESTS and not result.skipped and
            not result.expectedFailures and result.wasSuccessful())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--real-archive', required=True, type=Path)
    args = parser.parse_args()
    CMAKE, REAL_ARCHIVE = args.cmake, args.real_archive
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    if suite.countTestCases() != EXPECTED_TESTS:
        print('expected exactly 21 verifier tests', file=sys.stderr)
        raise SystemExit(1)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not complete(result):
        print('verifier qualification requires 21 executed tests and zero skips or expected failures',
              file=sys.stderr)
        raise SystemExit(1)
