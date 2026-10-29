#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Prove the source include allowlist stays narrow while the Linux-only build gate accepts the guarded Windows SDK
# headers used by the relay. Run the real checker on temporary source copies so the checkout is never modified.
#
# The first cases (the Windows headers, a path that climbs out of server/) stop at the first failure. Then come the
# five holes that run 4 found in the gate, one case each. A hole case tries several ways through the hole and prints
# "ok: H<n> ..." when the checker refuses all of them for the right reason, or "FAIL: H<n> ..." naming the ways it let
# through. No case stops the script: every one runs before it exits, so a gate with several holes shows them all.
#   H1  a missing link line file (Ninja writes none) was skipped, not refused
#   H2  a library inside -Wl,...,/opt/game.a,... (or behind a tab, or in an @response file) was not seen on the link line
#   H3  an include the line scanner could not see: a comment inside the directive (#/**/include), a split directive
#   H4  another spelling of the directive: the %:include digraph, #import
#   H5  an include of server/tests/*.h (any folder below server/) was admitted into the production sources
# Two controls show that includes inside comments and strings are still accepted. The second one lists forms the
# older line scanner refused by mistake (an include inside a multi-line comment), so it fails against that scanner.
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

# ---- the five holes of run 4 ------------------------------------------------------------------------------

holes=0
hole_bad=""
nl='
'
tab=$(printf '\t')

write_facts() {   # <new facts file> <link line file>: the real facts, with another link_file
    grep -v '^link_file=' "$facts" > "$1"
    echo "link_file=$2" >> "$1"
}

variant_refused() {   # <what> <words the output must hold> <facts file>: the checker, run on the copy, must refuse
    if sh "$checker" "$copy_root" "$3" > "$work/variant.log" 2>&1; then
        hole_bad="$hole_bad; accepted: $1"
    elif ! grep -Fq -- "$2" "$work/variant.log"; then
        hole_bad="$hole_bad; refused for another reason: $1"
    fi
}

variant_accepted() {   # <what> <facts file>: the checker, run on the copy, must accept
    if ! sh "$checker" "$copy_root" "$2" > "$work/variant.log" 2>&1; then
        hole_bad="$hole_bad; refused: $1"
    fi
}

hole_done() {   # <label> <what the case shows>
    if [ -z "$hole_bad" ]; then
        echo "ok: $1 $2"
    else
        echo "FAIL: $1 $2$hole_bad" >&2
        holes=$((holes + 1))
    fi
    hole_bad=""
}

refuse_source() {   # <what> <words the output must hold> <text>: <text> added to server/main.cpp must be refused
    copy_sources
    printf '\n%s\n' "$3" >> "$copy_root/server/main.cpp"
    variant_refused "$1" "$2" "$facts"
}

accept_source() {   # <what> <text>: <text> added to server/main.cpp must be accepted
    copy_sources
    printf '\n%s\n' "$2" >> "$copy_root/server/main.cpp"
    variant_accepted "$1" "$facts"
}

# H1: the program's link line file is required. A generator that writes none (Ninja) leaves nothing to read, and a
# check that cannot read the real link line must fail rather than pass.
copy_sources
write_facts "$work/facts-missing.txt" "$work/no-such-folder/link.txt"
variant_refused "a link line file that does not exist" "link line file" "$work/facts-missing.txt"
grep -v '^link_file=' "$facts" > "$work/facts-unnamed.txt"
variant_refused "no link line file named at all" "link line file" "$work/facts-unnamed.txt"
hole_done H1 "a missing link line file is refused (a generator that writes none, such as Ninja, cannot pass)"

# H2: the link line is split at blanks, tabs and commas, so a library handed over inside -Wl,... counts, and an
# @response file is refused because its words are not on the line. The plain line is the control for the others.
link_head='/usr/bin/c++ -O3 -DNDEBUG CMakeFiles/sco-relay.dir/main.cpp.o -o ../sco-relay'
link_libs='libsco_relay_server.a libsco_net.a libsco_relay_sockets.a'

refuse_link() {   # <what> <words the output must hold> <link line>
    printf '%s\n' "$3" > "$work/link.txt"
    write_facts "$work/facts-link.txt" "$work/link.txt"
    variant_refused "$1" "$2" "$work/facts-link.txt"
}

copy_sources
printf '%s\n' "$link_head $link_libs" > "$work/link.txt"
write_facts "$work/facts-link.txt" "$work/link.txt"
variant_accepted "the plain link line (control)" "$work/facts-link.txt"
refuse_link "an archive inside -Wl,--whole-archive,...,--no-whole-archive" "unexpected libraries on the link line: " \
    "$link_head $link_libs -Wl,--whole-archive,/tmp/game.a,--no-whole-archive"
refuse_link "a library named inside -Wl,-lgame" "unexpected libraries on the link line: " \
    "$link_head $link_libs -Wl,-lgame"
refuse_link "an archive in front of a known one, separated by a tab" "unexpected libraries on the link line: " \
    "$link_head /tmp/game.a${tab}/opt/libsco_net.a libsco_relay_server.a libsco_relay_sockets.a"
refuse_link "an @response file" "response file" "$link_head $link_libs @CMakeFiles/sco-relay.dir/objects.rsp"
refuse_link "an @response file inside -Wl," "response file" "$link_head $link_libs -Wl,@game.rsp"
hole_done H2 "a library hidden in a -Wl, list, behind a tab or in an @response file is refused"

# H3: includes the old line scanner (a grep for a line that starts with the hash) could not see. game_pack.h stands
# for any header the allowlist does not admit.
refuse_source "#/**/include, a comment inside the directive" "main.cpp: game_pack.h" "#/**/include <game_pack.h>"
refuse_source "# /* note */ include" "main.cpp: game_pack.h" "# /* note */ include <game_pack.h>"
refuse_source "#include /* note */ <game_pack.h>" "main.cpp: game_pack.h" "#include /* note */ <game_pack.h>"
refuse_source "a comment in front of the directive" "main.cpp: game_pack.h" "/* note */ #include <game_pack.h>"
refuse_source "a directive after a multi-line comment" "main.cpp: game_pack.h" \
    "/* first line${nl}last line */ #include <game_pack.h>"
refuse_source "a directive split in its keyword by a backslash" "main.cpp: game_pack.h" "#inc\\${nl}lude <game_pack.h>"
refuse_source "a directive split after the hash by a backslash" "main.cpp: game_pack.h" "#\\${nl}include <game_pack.h>"
refuse_source "a directive after a string that holds a comment opener" "main.cpp: game_pack.h" \
    "const char* s = \"/*\";${nl}#include <game_pack.h>${nl}// */"
refuse_source "a directive after a quote character and a string with a comment opener" "main.cpp: game_pack.h" \
    "char c = '\"'; const char* s = \"a/*b\";${nl}#include <game_pack.h>${nl}// */"
refuse_source "a directive after a digit separator and a string with a comment opener" "main.cpp: game_pack.h" \
    "int n = 1'000; const char* s = \"it's /* \";${nl}#include <game_pack.h>${nl}// */"
refuse_source "a raw string literal, which the reader cannot read through" "main.cpp: raw string literal" \
    "const char* s = R\"(x\"/*)\";${nl}#include <game_pack.h>${nl}const char* t = \"*/\";"
hole_done H3 "an include behind a comment, a line splice or a literal is found; text that cannot be read is refused"

# H4: the other spellings of the directive.
refuse_source "the digraph %:include" "main.cpp: game_pack.h" "%:include <game_pack.h>"
refuse_source "the digraph with a blank, %: include" "main.cpp: game_pack.h" "%: include <game_pack.h>"
refuse_source "the digraph and a comment, %:/**/include" "main.cpp: game_pack.h" "%:/**/include <game_pack.h>"
refuse_source "#import, the other directive GCC accepts" "main.cpp: game_pack.h" "#import <game_pack.h>"
hole_done H4 "the %: digraph spelling and #import are read as includes"

# H5: only server/<name>.h is ours. A header in a folder below server/ (the tests, the tools) is not production.
for folder in tests tools; do
    copy_sources
    mkdir -p "$copy_root/server/$folder"
    : > "$copy_root/server/$folder/probe.h"
    printf '\n#include "server/%s/probe.h"\n' "$folder" >> "$copy_root/server/main.cpp"
    variant_refused "an include of server/$folder/probe.h" "main.cpp: server/$folder/probe.h" "$facts"
done
accept_source "an include of server/log.h (control)" "#include \"server/log.h\""
hole_done H5 "a header from a folder below server/ is refused, a server/<name>.h header is accepted"

# Control: an include that is commented out, or only text in a string, is not an include.
accept_source "a // comment" "// #include <game_pack.h>"
accept_source "a one-line /* */ comment" "/* #include <game_pack.h> */"
accept_source "a comment holding a digraph" "// %:include <game_pack.h>"
accept_source "a string holding a directive" "const char* s = \"#include <game_pack.h>\";"
hole_done control "includes inside comments and strings are still accepted"

# Control: the same for the forms a line scanner gets wrong (the old one refused both, by mistake).
accept_source "a multi-line comment" "/*${nl}#include <game_pack.h>${nl}*/"
accept_source "a // comment continued by a backslash" "// a comment that goes on \\${nl}#include <game_pack.h>"
hole_done control "includes inside a multi-line comment or a comment continued by a backslash are accepted"

if [ "$holes" -ne 0 ]; then
    echo "relay_no_game_code_checks: $holes of the hole and control cases above failed" >&2
    exit 1
fi
echo "ok: every hole and control case passed"
