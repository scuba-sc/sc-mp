#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check relay configure-time source protection, including with tests disabled."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    root, cmake, archives, profile, sdk_hash, source_hash = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="sc-mp-boundary-config-") as temp:
        build = Path(temp) / "build"
        args = [cmake, "-S", root, "-B", str(build), "-G", "Unix Makefiles",
                "-DSC_MP_BUILD_TESTS=OFF", "-DSC_MP_BUILD_RELAY=ON",
                f"-DSCO_SDK_ARCHIVE_DIR={archives}", f"-DSCO_SDK_SHA256={sdk_hash}",
                f"-DSCO_CORE_SOURCE_SHA256={source_hash}", f"-DSC_MP_NET_BOUNDARY_PROFILE={profile}"]

        def configure(extra=()):
            return subprocess.run(args + list(extra), text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=90)

        def refused(result, expected, label):
            if result.returncode == 0 or expected not in result.stdout:
                raise RuntimeError(f"{label} was not refused as expected:\n{result.stdout}")
            print(f"ok   {label} refused")

        result = configure()
        if result.returncode:
            raise RuntimeError(f"matching kit configure failed:\n{result.stdout}")
        print("ok   matching kit accepted with tests disabled")
        other = "dev-1b" if profile == "rc2" else "rc2"
        refused(configure((f"-DSC_MP_NET_BOUNDARY_PROFILE={other}",)),
                "does not match the reviewed", "mismatched profile and archive pair")

        kit = build / "_sdk" / f"core-source-{source_hash}" / "sco-sdk-core-source-2.4.0"
        # A changed SDK CMakeLists must be rejected before it can execute.
        cmakelists = kit / "CMakeLists.txt"
        original = cmakelists.read_bytes()
        cmakelists.write_text('message(FATAL_ERROR "UNTRUSTED_CONFIG_EXECUTED")\n')
        try:
            result = configure()
            refused(result, "core-source extraction file changed", "modified kit CMakeLists")
            if "UNTRUSTED_CONFIG_EXECUTED" in result.stdout:
                raise RuntimeError("untrusted kit CMakeLists ran")
        finally:
            cmakelists.write_bytes(original)

        for relative in ("COMPATIBILITY.json", "include/sco/net/udp.h",
                         "src/net/udp_posix.cpp", "src/net/udp_win.cpp"):
            target = kit / relative
            original = target.read_bytes()
            target.write_bytes(original + b"\n")
            try:
                refused(configure(), "core-source extraction file changed", f"modified {relative}")
            finally:
                target.write_bytes(original)

        extra = kit / ".unreviewed-hidden-file"
        extra.write_text("extra")
        try:
            refused(configure(), "missing or extra files", "extra hidden kit file")
        finally:
            extra.unlink()

        sdk = build / "_sdk" / f"src-{sdk_hash}" / "sco-sdk-2.4.0"
        for relative, expected in (("include/sco_api.h", "SDK extraction file changed"),
                                   ("SHA256SUMS", "SDK inventory changed")):
            target = sdk / relative
            original = target.read_bytes()
            target.write_bytes(original + b"\n")
            try:
                refused(configure(), expected, f"modified SDK {relative}")
            finally:
                target.write_bytes(original)
        extra = sdk / ".unreviewed-hidden-file"
        extra.write_text("extra")
        try:
            refused(configure(), "SDK extraction has missing or extra files", "extra hidden SDK file")
        finally:
            extra.unlink()

        # On POSIX, also cover a symlinked intermediate directory.
        net = kit / "src/net"
        saved = kit / "src/net.saved"
        net.rename(saved)
        try:
            os.symlink("net.saved", net)
            refused(configure(), "symbolic link", "symlinked source directory")
        finally:
            if net.is_symlink():
                net.unlink()
            saved.rename(net)

        result = configure()
        if result.returncode:
            raise RuntimeError(f"restored kit configure failed:\n{result.stdout}")
        print("ok   restored kit accepted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
