#!/usr/bin/env python3
"""Check a coordinated, local SDK pair before using it for development tests.

Expected hashes and source commit must come from the coordinating core owner;
archive metadata alone cannot authenticate who produced an archive.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile


def verify_pair(directory, version, commit, sdk_hash, source_hash):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-.][A-Za-z0-9.-]+)?", version):
        raise ValueError("invalid SDK version")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("source commit must be a full lowercase Git SHA-1")
    for digest in (sdk_hash, source_hash):
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("archive hashes must be lowercase SHA-256 values")
    abis = []
    for stem, profile, expected in ((f"sco-sdk-{version}", "legacy", sdk_hash),
                                    (f"sco-sdk-core-source-{version}", "core-source", source_hash)):
        path = directory / f"{stem}.zip"
        with path.open("rb") as file:
            hasher = hashlib.sha256()
            for chunk in iter(lambda: file.read(1024 * 1024), b""):
                hasher.update(chunk)
            actual = hasher.hexdigest()
        if actual != expected:
            raise ValueError(f"{path.name}: SHA-256 mismatch")
        with zipfile.ZipFile(path) as archive:
            name = f"{stem}/COMPATIBILITY.json"
            entries = [item for item in archive.infolist() if item.filename == name]
            if len(entries) != 1 or entries[0].file_size > 65536:
                raise ValueError(f"{path.name}: missing, duplicate or oversized compatibility metadata")
            metadata = json.loads(archive.read(entries[0]))
        if (not isinstance(metadata, dict) or metadata.get("schema_version") != 1
                or metadata.get("profile") != profile or metadata.get("core_sdk_version") != version
                or not isinstance(metadata.get("source_commits"), dict)
                or metadata["source_commits"].get("core") != commit
                or (profile == "legacy" and metadata["source_commits"].get("game-sc") != commit)):
            raise ValueError(f"{path.name}: compatibility metadata does not match the requested input")
        abi = metadata.get("plugin_abi_version")
        if not isinstance(abi, str) or not abi:
            raise ValueError(f"{path.name}: missing plugin ABI version")
        abis.append(abi)
    if abis[0] != abis[1]:
        raise ValueError("SDK and core-source plugin ABI versions differ")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive-dir", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--sdk-sha256", required=True)
    parser.add_argument("--core-source-sha256", required=True)
    args = parser.parse_args()
    try:
        verify_pair(args.archive_dir, args.version, args.source_commit,
                    args.sdk_sha256, args.core_source_sha256)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"development SDK verification failed: {error}", file=sys.stderr)
        return 1
    print(f"verified local SDK pair for source {args.source_commit}, SDK {args.version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
