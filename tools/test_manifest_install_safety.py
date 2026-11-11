# SPDX-License-Identifier: GPL-3.0-only
"""Exercise installed-manifest checks without deleting caller-owned paths."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor


def main():
    cmake, script, build, module, config = sys.argv[1:]
    scratch = Path(tempfile.mkdtemp(prefix="sc-mp-manifest-safety-"))
    sentinel_dir = scratch / "caller-owned"
    sentinel_dir.mkdir()
    sentinel = sentinel_dir / "sentinel.txt"
    sentinel.write_text("preserve", encoding="utf-8")
    allocation_root = scratch / "temp; unicode-ü"
    allocation_root.mkdir()
    env = os.environ.copy()
    env.update(TEMP=str(allocation_root), TMP=str(allocation_root), TMPDIR=str(allocation_root))
    base = [cmake, f"-DSC_MP_BUILD_DIR={build}", f"-DSC_MP_EXPECTED_MODULE={module}",
            f"-DSC_MP_CONFIG={config}", "-DSC_MP_TEST_INSTALL=ON"]

    def run(extra):
        return subprocess.run(base + extra + ["-P", script], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding="utf-8", errors="replace", timeout=120)

    refused = run([f"-DSC_MP_INSTALL_PREFIX={sentinel_dir}", f"-DSC_MP_PYTHON={sys.executable}"])
    assert refused.returncode != 0 and "SC_MP_INSTALL_PREFIX is unsupported" in refused.stdout, refused.stdout
    assert sentinel.read_text(encoding="utf-8") == "preserve"

    link = scratch / "caller-link"
    try:
        link.symlink_to(sentinel_dir, target_is_directory=True)
    except OSError:
        print("manifest install safety: directory-symlink case unavailable on this host")
    else:
        refused_link = run([f"-DSC_MP_INSTALL_PREFIX={link}", f"-DSC_MP_PYTHON={sys.executable}"])
        assert refused_link.returncode != 0 and "SC_MP_INSTALL_PREFIX is unsupported" in refused_link.stdout, refused_link.stdout
        assert link.is_symlink() and sentinel.read_text(encoding="utf-8") == "preserve"

    missing = run(["-DSC_MP_PYTHON="])
    assert missing.returncode != 0 and "requires Python 3" in missing.stdout, missing.stdout
    absent = run([f"-DSC_MP_PYTHON={scratch / 'absent-python'}"])
    assert absent.returncode != 0 and "requires Python 3" in absent.stdout, absent.stdout

    # Two simultaneous invocations must allocate different install roots.
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda _: run([f"-DSC_MP_PYTHON={sys.executable}"]), range(2)))
    allocated = []
    for result in results:
        assert result.returncode == 0, result.stdout
        match = re.search(r"SC_MP_MANIFEST_SCRATCH=([^\r\n]+)", result.stdout)
        assert match, result.stdout
        path = Path(match.group(1)).resolve(strict=True)
        assert path.parent == allocation_root.resolve(strict=True), (path, allocation_root)
        plugin = path / "data" / "plugins" / "multiplayer"
        assert (plugin / "plugin.ini").is_file() and (plugin / module).is_file()
        allocated.append(path)
    assert len(set(allocated)) == len(allocated), allocated
    assert sentinel.read_text(encoding="utf-8") == "preserve"
    print("manifest install safety: sentinel preserved, missing Python refused, concurrent installs isolated")
    print(f"manifest install safety scratch retained: {scratch}")


if __name__ == "__main__":
    main()
