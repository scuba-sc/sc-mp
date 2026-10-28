#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Prove the source include allowlist stays narrow while the Linux-only build gate accepts the guarded Windows SDK
# headers used by the relay. Run the real checker on temporary source copies so the checkout is never modified.
set -eu

root=${1:-}
facts=${2:-}
if [ -z "$root" ] || [ -z "$facts" ] || [ ! -f "$root/server/tools/check-no-game-code.sh" ]; then
    echo "usage: sh test_no_game_code.sh <repository root> <build facts file>" >&2
    exit 2
fi

checker="$root/server/tools/check-no-game-code.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' 0 HUP INT TERM
copy_root="$work/repo"

copy_sources() {
    rm -rf "$copy_root"
    mkdir -p "$copy_root/cmake" "$copy_root/plugins/multiplayer"
    cp -R "$root/server" "$copy_root/server"
    cp "$root/cmake/ScoCoreSource.cmake" "$copy_root/cmake/ScoCoreSource.cmake"
    cp "$root/plugins/multiplayer/relay_wire.h" "$copy_root/plugins/multiplayer/relay_wire.h"
}

# The production source includes windows.h in main.cpp under _WIN32 and sal.h in log.h under _MSC_VER. The full
# checker must accept these guarded platform headers.
copy_sources
if ! sh "$checker" "$copy_root" "$facts" > "$work/allowed.log" 2>&1; then
    cat "$work/allowed.log" >&2
    echo "relay_no_game_code_checks: the approved Windows headers were rejected" >&2
    exit 1
fi
if ! grep -Fq 'approved system header' "$work/allowed.log"; then
    cat "$work/allowed.log" >&2
    echo "relay_no_game_code_checks: the include check did not report its approval" >&2
    exit 1
fi
echo "ok: accepted the conditional Windows SDK headers"

# A similarly named Windows SDK header is not implicitly approved by the exact header exceptions.
copy_sources
printf '\n#include <windowsx.h>\n' >> "$copy_root/server/main.cpp"
if sh "$checker" "$copy_root" "$facts" > "$work/windowsx.log" 2>&1; then
    echo "relay_no_game_code_checks: accepted an unapproved Windows header" >&2
    exit 1
fi
if ! grep -Fq 'main.cpp: windowsx.h' "$work/windowsx.log"; then
    cat "$work/windowsx.log" >&2
    echo "relay_no_game_code_checks: rejected the Windows header for an unexpected reason" >&2
    exit 1
fi
echo "ok: rejected the unapproved windowsx.h include"

# Keep windows.h confined to main.cpp, where it is guarded by _WIN32 for platform startup APIs.
copy_sources
printf '\n#include <windows.h>\n' >> "$copy_root/server/log.cpp"
if sh "$checker" "$copy_root" "$facts" > "$work/misplaced.log" 2>&1; then
    echo "relay_no_game_code_checks: accepted windows.h outside main.cpp" >&2
    exit 1
fi
if ! grep -Fq 'log.cpp: windows.h' "$work/misplaced.log"; then
    cat "$work/misplaced.log" >&2
    echo "relay_no_game_code_checks: rejected the misplaced Windows header for an unexpected reason" >&2
    exit 1
fi
echo "ok: rejected windows.h outside main.cpp"

# Keep sal.h confined to log.h, where it is guarded by _MSC_VER for MSVC format annotations.
copy_sources
printf '\n#include <sal.h>\n' >> "$copy_root/server/log.cpp"
if sh "$checker" "$copy_root" "$facts" > "$work/sal.log" 2>&1; then
    echo "relay_no_game_code_checks: accepted sal.h outside log.h" >&2
    exit 1
fi
if ! grep -Fq 'log.cpp: sal.h' "$work/sal.log"; then
    cat "$work/sal.log" >&2
    echo "relay_no_game_code_checks: rejected the misplaced SAL header for an unexpected reason" >&2
    exit 1
fi
echo "ok: rejected sal.h outside log.h"

# An include that resolves through a parent component remains forbidden, even when it names the shared wire header.
copy_sources
printf '\n#include "server/../plugins/multiplayer/relay_wire.h"\n' >> "$copy_root/server/main.cpp"
if sh "$checker" "$copy_root" "$facts" > "$work/traversal.log" 2>&1; then
    echo "relay_no_game_code_checks: accepted an include that climbs out of server/" >&2
    exit 1
fi
if ! grep -Fq 'main.cpp: server/../plugins/multiplayer/relay_wire.h' "$work/traversal.log"; then
    cat "$work/traversal.log" >&2
    echo "relay_no_game_code_checks: rejected the parent path for an unexpected reason" >&2
    exit 1
fi
echo "ok: rejected an include that climbs out of server/"
