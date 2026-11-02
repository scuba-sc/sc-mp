#!/usr/bin/env python3
"""Exercise the Git-mode packaging adapter with real local Git checkouts."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import package_dev_sdk
from package_dev_sdk import GitModeOS, checked_index


class GitModeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "runner space" / "core"
        self.root.mkdir(parents=True)
        self.run_git("init", "--quiet")
        self.run_git("config", "core.filemode", "false")
        (self.root / ".gitattributes").write_bytes(b"*.txt text=auto\n")
        (self.root / ".gitignore").write_bytes(b"ignored.bin\n")
        (self.root / "plain.txt").write_bytes(b"line one\nline two\n")
        (self.root / "executable").write_bytes(b"#!/bin/sh\n")
        self.run_git("add", ".")
        self.run_git("update-index", "--chmod=+x", "executable")
        self.run_git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                     "commit", "--quiet", "-m", "fixture")
        self.commit = self.run_git("rev-parse", "HEAD").decode().strip()

    def run_git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], stderr=subprocess.PIPE)

    def test_git_mode_overrides_host_access(self):
        index = checked_index(self.root, self.commit)
        adapter = GitModeOS(self.root.resolve(), index)
        self.assertFalse(adapter.access(self.root / "plain.txt", os.X_OK))
        self.assertTrue(adapter.access(self.root / "executable", os.X_OK))
        with self.assertRaisesRegex(ValueError, "untracked"):
            adapter.access(self.root / "missing", os.X_OK)

    def test_wrong_commit_rejected(self):
        with self.assertRaisesRegex(ValueError, "commit does not match"):
            checked_index(self.root, "0" * 40)

    def test_untracked_and_ignored_files_rejected(self):
        for name in ("untracked.bin", "ignored.bin"):
            path = self.root / name
            path.write_bytes(b"untracked")
            with self.assertRaisesRegex(ValueError, "not clean"):
                checked_index(self.root, self.commit)
            path.unlink()

    def test_staged_changes_rejected(self):
        (self.root / "plain.txt").write_bytes(b"changed\n")
        self.run_git("add", "plain.txt")
        with self.assertRaisesRegex(ValueError, "not clean"):
            checked_index(self.root, self.commit)

    def test_crlf_checkout_bytes_rejected_even_when_git_diff_is_clean(self):
        self.run_git("config", "core.autocrlf", "true")
        (self.root / "plain.txt").write_bytes(b"line one\r\nline two\r\n")
        self.run_git("diff", "--quiet")
        original = package_dev_sdk.git
        def hide_status(root, *args):
            return b"" if args[0] == "status" else original(root, *args)
        with mock.patch.object(package_dev_sdk, "git", side_effect=hide_status):
            with self.assertRaisesRegex(ValueError, "file bytes do not match"):
                checked_index(self.root, self.commit)


if __name__ == "__main__":
    unittest.main()
