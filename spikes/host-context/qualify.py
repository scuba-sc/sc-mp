#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline exact-input qualification; creates a fresh evidence directory."""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

CORE = "44ffd1290e261e12684b38ac862ec0c4f08921df"
ARCHIVE = "807a322eab2b3d8c25e10c3582008ec3ec50b6b536bc947f2b28cf4b63095c22"
INVENTORY = "812d09236dc952309d3f1c63a843f6ff0407f6465066c4bd7d068b123d3b503c"
HELPER = "ece58eff511fb1b5bef24784d98e0027be5f42d810a02eb7975f9dceec2581e9"
VERIFIER = "594d539d996306ad560d44efe7bea1f8c1de99b6682e2fa563d6cd52827bf9aa"
VERIFIER_TESTS = "1c70e69bd5617179325da6b0b062339624d0c7d53a4c26cd3c3efafdaeb2a3fe"
VERIFIER_DOC = "7562d3567664728ad3e48abf83d431981119fa2609c38c8d3b36420a660ebe70"
PREFIX = "sco-sdk-core-source-2.4.0"
FILES = ("CMakeLists.txt", "context.h", "context.cpp", "main.cpp", "README.md", "qualify.py")
TESTS = {"host-context-" + name for name in
         ("basic", "overflow", "drain", "drain-now", "output-contract", "output-overflow",
          "coherence", "replace-during-copy", "success-drain", "leave-during-copy", "owned-maintenance")}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(archive, destination):
    # Hash and parse the same bounded bytes; the output directory is trusted
    # and private for this offline qualification, not an adversarial sandbox.
    with archive.open("rb") as stream:
        archive_bytes = stream.read(64 * 1024 * 1024 + 1)
    if len(archive_bytes) > 64 * 1024 * 1024:
        raise ValueError("compressed archive exceeds bounds")
    if hashlib.sha256(archive_bytes).hexdigest() != ARCHIVE:
        raise ValueError("core-source archive hash mismatch")
    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as z:
        entries = z.infolist()
        if len(entries) > 2048 or sum(e.file_size for e in entries) > 64 * 1024 * 1024:
            raise ValueError("archive exceeds extraction bounds")
        # Include implicit directories, so file/directory and case-fold
        # conflicts are rejected regardless of ZIP entry order.
        nodes = {}
        for e in entries:
            name = e.filename
            is_dir = e.is_dir()
            parts = name[:-1].split("/") if is_dir else name.split("/")
            mode = e.external_attr >> 16
            if (e.orig_filename != name or not parts or parts[0] != PREFIX or
                    (len(parts) == 1 and not is_dir) or
                    any(part in ("", ".", "..") for part in parts) or
                    "\\" in name or "\x00" in name or
                    (mode and (not (stat.S_ISREG(mode) or stat.S_ISDIR(mode))
                               or stat.S_ISDIR(mode) != is_dir))):
                raise ValueError("unsafe or duplicate archive entry")
            for i in range(1, len(parts) + 1):
                path = "/".join(parts[:i])
                key = path.casefold()
                kind = "directory" if i < len(parts) or is_dir else "file"
                explicit = i == len(parts)
                previous = nodes.get(key)
                if previous:
                    old_path, old_kind, old_explicit = previous
                    if old_path != path or old_kind != kind or (explicit and old_explicit):
                        raise ValueError("unsafe or duplicate archive entry")
                    nodes[key] = (path, kind, explicit or old_explicit)
                else:
                    nodes[key] = (path, kind, explicit)
        z.extractall(destination)
    kit = destination / PREFIX
    return kit, verify_kit(kit)


def verify_kit(kit):
    verifier_path = Path(__file__).resolve().parents[2] / "cmake" / "verify_host_context_kit.py"
    if digest(verifier_path) != VERIFIER:
        raise ValueError("reviewed tree verifier changed")
    spec = importlib.util.spec_from_file_location("host_context_kit_verifier", verifier_path)
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    return verifier.verify_kit(kit, INVENTORY)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--core-archive", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path,
                    help="new directory; existing evidence is never overwritten")
    ap.add_argument("--cmake", default="cmake")
    ap.add_argument("--cc", default="clang-19")
    ap.add_argument("--cxx", default="clang++-19")
    ap.add_argument("--sanitize", action="store_true")
    args = ap.parse_args()
    out = args.output.resolve()
    if out.exists():
        ap.error("output already exists; choose a fresh evidence directory")
    out.mkdir(parents=True)
    source = Path(__file__).resolve().parent
    receipt = {"core_candidate": CORE, "candidate_status": "RC3 candidate; experimental host-source only",
               "archive_sha256_expected": ARCHIVE, "inventory_sha256_expected": INVENTORY,
               "qualified": False, "stages": [], "sanitized": args.sanitize}
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    if args.sanitize:
        env["ASAN_OPTIONS"] = "detect_leaks=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1"

    def run(label, command, timeout=600):
        with (out / (label + ".log")).open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                    env=env, timeout=timeout)
        receipt["stages"].append({"name": label, "command": command,
                                   "exit_code": result.returncode})
        if result.returncode:
            raise RuntimeError(label + " failed; see retained log")

    try:
        # Preserve compiler-driver symlink names: clang++ and clang link differently.
        cmake = os.path.abspath(shutil.which(args.cmake) or args.cmake)
        cc = os.path.abspath(shutil.which(args.cc) or args.cc)
        cxx = os.path.abspath(shutil.which(args.cxx) or args.cxx)
        git = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                             text=True, capture_output=True)
        receipt["source_commit"] = git.stdout.strip() if git.returncode == 0 else None
        status = subprocess.run(["git", "-C", str(source.parents[1]), "status", "--short", "--untracked-files=all", "--",
                                 "spikes/host-context", "cmake/VerifyExtractedKit.cmake",
                                 "cmake/verify_host_context_kit.py", "tests/host-context"],
                                text=True, capture_output=True)
        receipt["source_status"] = status.stdout.splitlines() if status.returncode == 0 else None
        receipt["source_sha256"] = {name: digest(source / name) for name in FILES}
        snapshot = out / "source" / "spikes" / "host-context"
        snapshot.mkdir(parents=True)
        for name in FILES:
            shutil.copyfile(source / name, snapshot / name)
            if digest(snapshot / name) != receipt["source_sha256"][name]:
                raise RuntimeError("source changed during snapshot")
        root = source.parents[1]
        pinned = {
            "cmake/VerifyExtractedKit.cmake": HELPER,
            "cmake/verify_host_context_kit.py": VERIFIER,
            "tests/host-context/test_verify_kit.py": VERIFIER_TESTS,
            "tests/host-context/README.md": VERIFIER_DOC,
        }
        for name, expected in pinned.items():
            original = root / name
            if digest(original) != expected:
                raise ValueError("reviewed verifier input changed: " + name)
            target = out / "source" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
            if digest(target) != expected:
                raise RuntimeError("verifier input changed during snapshot: " + name)
        receipt["verifier_sha256"] = pinned
        run("verifier-tests", [sys.executable, str(out / "source" / "tests" / "host-context" / "test_verify_kit.py"),
                               "--cmake", cmake, "--real-archive", str(args.core_archive.resolve())])
        kit, count = extract(args.core_archive.resolve(), out / "kit")
        receipt["inventory_entries"] = count
        run("cmake-version", [cmake, "--version"])
        run("cc-version", [cc, "--version"])
        run("cxx-version", [cxx, "--version"])
        flags = "-fsanitize=address,undefined -fno-omit-frame-pointer" if args.sanitize else ""
        run("configure", [cmake, "-S", str(snapshot), "-B", str(out / "build"),
                          "-DCMAKE_BUILD_TYPE=" + ("Debug" if args.sanitize else "Release"),
                          "-DCMAKE_C_COMPILER=" + cc, "-DCMAKE_CXX_COMPILER=" + cxx,
                          "-DCMAKE_C_FLAGS=" + flags, "-DCMAKE_CXX_FLAGS=" + flags,
                          "-DCMAKE_EXE_LINKER_FLAGS=" + flags,
                          "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DSDK_ROOT=" + str(kit)])
        run("build", [cmake, "--build", str(out / "build"), "--target", "host-context", "--parallel", "4"])
        ctest = str(Path(cmake).with_name("ctest"))
        run("ctest", [ctest, "--test-dir", str(out / "build"), "--output-on-failure",
                      "--no-tests=error", "--output-junit", str(out / "ctest.xml")], timeout=180)
        cases = list(ET.parse(out / "ctest.xml").getroot().iter("testcase"))
        if (len(cases) != len(TESTS) or {case.get("name") for case in cases} != TESTS
                or any(case.find(tag) is not None for case in cases
                       for tag in ("failure", "error", "skipped"))):
            raise RuntimeError("expected focused tests did not all pass")
        receipt["tests_passed"] = sorted(TESTS)
        if any(digest(source / name) != h or digest(snapshot / name) != h
               for name, h in receipt["source_sha256"].items()):
            raise RuntimeError("source changed during qualification")
        for name, expected in pinned.items():
            if digest(root / name) != expected or digest(out / "source" / name) != expected:
                raise RuntimeError("verifier input changed during qualification: " + name)
        verify_kit(kit)
        receipt["post_build_inputs_verified"] = True
        receipt["binary_sha256"] = digest(out / "build" / "host-context")
        receipt["qualified"] = True
    except Exception as exc:
        receipt["error"] = str(exc)
    finally:
        receipt["log_sha256"] = {p.name: digest(p) for p in sorted(out.glob("*.log"))}
        (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"qualified": receipt["qualified"], "receipt": str(out / "receipt.json")}))
    return 0 if receipt["qualified"] else 1


if __name__ == "__main__":
    sys.exit(main())
