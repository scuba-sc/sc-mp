#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate an extracted source kit against a pinned inventory before CMake uses it."""
import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import stat


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def verify_kit(root, inventory_hash):
    if '..' in Path(root).parts:
        raise ValueError('parent component in kit root path')
    root = Path(os.path.abspath(root))
    # Refuse symlinks in the entire path to the extraction root.
    for path in reversed((root, *root.parents)):
        if stat.S_ISLNK(path.lstat().st_mode):
            raise ValueError('symlink in kit root path: ' + str(path))
    if not root.is_dir():
        raise ValueError('kit root is not a directory')
    actual = set()
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            path = Path(directory) / name
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                raise ValueError('symlink in kit: ' + str(path))
            if stat.S_ISREG(mode):
                actual.add(path.relative_to(root).as_posix())
            elif not stat.S_ISDIR(mode):
                raise ValueError('nonregular kit entry: ' + str(path))
    inventory = root / 'SHA256SUMS'
    if 'SHA256SUMS' not in actual or digest(inventory) != inventory_hash:
        raise ValueError('inventory hash mismatch')
    expected = {'SHA256SUMS'}
    entries = []
    for line in inventory.read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        if not match:
            raise ValueError('malformed inventory entry')
        expected_hash, name = match.groups()
        path = PurePosixPath(name)
        if (path.is_absolute() or '\\' in name or name != path.as_posix() or
                any(part in ('', '.', '..') for part in name.split('/')) or name in expected):
            raise ValueError('unsafe or duplicate inventory path: ' + name)
        expected.add(name)
        entries.append((name, expected_hash))
    if actual != expected:
        raise ValueError('kit file set mismatch')
    for name, expected_hash in entries:
        if digest(root / name) != expected_hash:
            raise ValueError('kit content mismatch: ' + name)
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root')
    parser.add_argument('inventory_hash')
    args = parser.parse_args()
    try:
        count = verify_kit(args.root, args.inventory_hash)
    except (OSError, ValueError, UnicodeError) as exc:
        parser.exit(1, 'kit verification failed: ' + str(exc) + '\n')
    print('kit verified: ' + str(count) + ' files')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
