#!/usr/bin/env python3
"""Run the exact source SDK packager using Git's executable-mode bits.

The source packager uses os.access(X_OK), which does not preserve Git executable
bits on every host. Only that query is normalized. The full checkout is checked
against its index and expected commit before and after packaging; the produced
archives must still match independently supplied SHA-256 values.
"""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys

from verify_dev_sdk import verify_pair


def git(root, *arguments):
    return subprocess.check_output(["git", "-C", str(root), *arguments])


def checked_index(root, expected_commit):
    if not re.fullmatch(r"[0-9a-f]{40}", expected_commit):
        raise ValueError("expected commit must be a full lowercase SHA-1")
    if git(root, "rev-parse", "--show-object-format").strip() != b"sha1":
        raise ValueError("unsupported Git object format")
    if git(root, "rev-parse", "HEAD").strip().decode("ascii") != expected_commit:
        raise ValueError("checkout commit does not match")
    if git(root, "status", "--porcelain=v1", "--untracked-files=all", "--ignored=matching"):
        raise ValueError("checkout is not clean, including ignored files")
    index = {}
    for record in git(root, "ls-files", "--stage", "-z").split(b"\0"):
        if not record:
            continue
        metadata, filename = record.split(b"\t", 1)
        mode, object_id, stage = metadata.decode("ascii").split(" ")
        name = filename.decode("utf-8")
        if mode not in ("100644", "100755") or stage != "0" or name in index:
            raise ValueError("unsupported or unmerged Git index entry")
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("unsafe Git index path")
        path = root / relative
        for component in (path, *path.parents):
            if component == root:
                break
            if component.is_symlink():
                raise ValueError("symbolic link in source checkout")
        data = path.read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
        if actual != object_id:
            raise ValueError("checkout file bytes do not match Git index: " + name)
        index[relative.as_posix()] = mode == "100755"
    if not index:
        raise ValueError("empty Git index")
    return index


class GitModeOS:
    def __init__(self, root, index):
        self.root = root
        self.index = index

    def __getattr__(self, name):
        return getattr(os, name)

    def access(self, path, mode):
        if mode != os.X_OK:
            return os.access(path, mode)
        try:
            relative = Path(path).resolve().relative_to(self.root).as_posix()
            return self.index[relative]
        except (KeyError, ValueError) as error:
            raise ValueError("packager asked for an untracked executable mode") from error


def package(root, output, commit, sdk_hash, source_hash):
    root = root.resolve(strict=True)
    if output.exists():
        raise ValueError("output directory already exists")
    index = checked_index(root, commit)
    packager = root / "sdk" / "package.py"
    # Guard the narrowly scoped normalization against a future packager rewrite.
    if packager.read_text(encoding="utf-8").count("os.access(full, os.X_OK)") != 1:
        raise ValueError("source packager executable-mode call changed")
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("sc_mp_source_sdk_packager", packager)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.os = GitModeOS(root, index)
    manifest = module.load_manifest()
    module.build(manifest, "legacy", str(output))
    module.build(manifest, "core-source", str(output))
    if checked_index(root, commit) != index:
        raise ValueError("source checkout changed during packaging")
    verify_pair(output, "2.4.0", commit, sdk_hash, source_hash)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--sdk-sha256", required=True)
    parser.add_argument("--core-source-sha256", required=True)
    args = parser.parse_args()
    try:
        package(args.core, args.out, args.source_commit, args.sdk_sha256, args.core_source_sha256)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"development SDK packaging failed: {error}", file=sys.stderr)
        return 1
    print("verified exact-source development SDK archives")
    return 0


if __name__ == "__main__":
    sys.exit(main())
