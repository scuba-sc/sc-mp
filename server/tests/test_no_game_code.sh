#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Prove the source include allowlist stays narrow while the Linux-only build gate accepts the guarded Windows SDK
# headers used by the relay, checks the shared relay wire header, and rejects system-header shadows. Run the real
# checker on temporary source copies so the checkout is never modified.
#
# The first cases (the Windows headers, a path that climbs out of server/) stop at the first failure. Then come the
# five holes listed in sc-mp pull request #6, one case each. A hole case tries several ways through the hole and prints
# "ok: H<n> ..." when the checker refuses all of them for the right reason, or "FAIL: H<n> ..." naming the ways it let
# through. No case stops the script: every one runs before it exits, so a gate with several holes shows them all.
#   H1  a missing link line file (Ninja writes none) was skipped, not refused
#   H2  a library inside -Wl,...,/opt/game.a,... (or behind a tab, in quotes, or in an @response file) was not seen on
#       the link line
#   H3  an include the line scanner could not see: a comment inside the directive (#/**/include), a split directive,
#       a lone carriage return that ends a line, a NUL byte, a quote the reader takes for a digit separator
#   H4  another spelling of the directive: the %:include digraph, #import
#   H5  an include of server/tests/*.h (any folder below server/) was admitted into the production sources
#   B0  ordinary GCC/Clang linker options were rejected (positive controls)
#   B1  an allowlisted system header resolved to a same-named project file
#   B2  an SDK header could be shadowed by a project wrapper
#   B3  a project-root include directory could shadow a transitive system header
#   B4  a same-basename source outside server/ could replace a reviewed server source
#   B5  the shared plugins/multiplayer/relay_wire.h was outside the production source scan
#   B6  library search paths were not rejected
#   B7  CMake archive substitutions or duplicate archive inputs were not rejected
#   B8  raw .obj objects and unknown positional linker inputs were not rejected
#   B9  linker-script/plugin/remap options, including -Xlinker forwarding, were not rejected
#   B10 compiler include aliases and driver configuration/wrapper/plugin overrides were not rejected
# Two more cases guard the rewritten include reader against regressions the review of the first rewrite found. They
# are not holes of the old gate (it refused these), so they are labelled R:
#   R1  form feed and vertical tab count as blanks around the directive, as they did for the old line scanner
#   R2  a quote that only looks like a digit separator cannot open a comment that does not exist and hide an include
# Two controls show that includes inside one-line comments and strings are still accepted, and that an include on a
# line of its own inside a multi-line comment is refused anyway (the plain reading is not fooled by comments).
set -eu

root=${1:-}
facts=${2:-}
if [ -z "$root" ] || [ -z "$facts" ] || [ ! -f "$root/server/tools/check-no-game-code.sh" ]; then
    echo "usage: sh test_no_game_code.sh <repository root> <build facts file>" >&2
    exit 2
fi

checker="$root/server/tools/check-no-game-code.sh"
facts_input=$facts
work=$(mktemp -d)
trap 'rm -rf "$work"' 0 HUP INT TERM
copy_root="$work/repo"

copy_sources() {
    rm -rf "$copy_root"
    mkdir -p "$copy_root/cmake" "$copy_root/plugins/multiplayer"
    cp -R "$root/server" "$copy_root/server"
    cp "$root/cmake/ScoCoreSource.cmake" "$copy_root/cmake/ScoCoreSource.cmake"
    cp "$root/plugins/multiplayer/relay_wire.h" "$copy_root/plugins/multiplayer/relay_wire.h"
    copy_server_sources=$(for source in "$copy_root"/server/*.cpp; do
        [ "$(basename "$source")" = main.cpp ] || echo "$source"
    done | sort | tr '\n' ';' | sed 's/;$//')
    input_kit_root=$(sed -n 's/^kit=//p' "$facts_input" | head -n 1)
    input_kit_include=$(sed -n 's/^kit_include=//p' "$facts_input" | head -n 1)
    input_cache=$(sed -n 's/^cache=//p' "$facts_input" | head -n 1)
    input_c_compiler_id=$(sed -n 's/^c_compiler_id=//p' "$facts_input" | head -n 1)
    input_cxx_compiler_id=$(sed -n 's/^cxx_compiler_id=//p' "$facts_input" | head -n 1)
    input_program=$(sed -n 's/^program=//p' "$facts_input" | head -n 1)
    input_program_dir=${input_program%/*}
    input_net_sources=$(sed -n 's/^net_sources=//p' "$facts_input" | head -n 1)
    input_socket_sources=$(sed -n 's/^sockets_sources=//p' "$facts_input" | head -n 1)
    compile_commands_copy="$work/compile-commands-copy.json"
    python3 - "$copy_root" "$input_kit_root" "$input_kit_include" "$input_cache" \
        "$input_c_compiler_id" "$input_cxx_compiler_id" "$copy_server_sources" \
        "$input_net_sources" "$input_socket_sources" "$compile_commands_copy" <<'PY'
import json
import os
import sys

root, kit_root, kit_include, cache_path, c_compiler_id, cxx_compiler_id, server_list, net_list, socket_list, output = sys.argv[1:]
entries = []
cache = {}
with open(cache_path, encoding="utf-8") as stream:
    for line in stream:
        key_and_type, separator, value = line.rstrip("\n").partition("=")
        key, type_separator, _value_type = key_and_type.partition(":")
        if separator and type_separator:
            cache[key] = value
compiler_c = cache.get("CMAKE_C_COMPILER", "")
compiler_cxx = cache.get("CMAKE_CXX_COMPILER", "")
if not compiler_c or not compiler_cxx:
    raise SystemExit("CMakeCache.txt does not name both configured compilers")

def add(source, target, base):
    if not source:
        return
    if not os.path.isabs(source):
        source = os.path.join(base, source)
    compiler, compiler_id = (compiler_c, c_compiler_id) if source.endswith(".c") else (compiler_cxx, cxx_compiler_id)
    arguments = [compiler]
    if "clang" in compiler_id.lower():
        arguments.append("--no-default-config")
    arguments.extend(["-I", kit_include, "-c", source])
    entries.append({
        "directory": root,
        "file": source,
        "arguments": arguments,
        "output": os.path.join(root, "CMakeFiles", f"{target}.dir", os.path.basename(source) + ".o"),
    })

for source in server_list.split(";"):
    add(source, "sco_relay_server", root)
add(os.path.join(root, "server", "main.cpp"), "sco-relay", root)
for source in net_list.split(";"):
    add(source, "sco_net", kit_root)
for source in socket_list.split(";"):
    add(source, "sco_relay_sockets", kit_root)
with open(output, "w", encoding="utf-8") as stream:
    json.dump(entries, stream)
PY
    grep -Ev '^server_sources=|^production_include_dirs=|^compile_commands=|^program=' "$facts_input" > "$work/facts-copy.tmp"
    printf 'server_sources=%s\nproduction_include_dirs=%s\ncompile_commands=%s\nprogram=%s/check-no-game-code-test-program\n' \
        "$copy_server_sources" "$input_kit_include" "$compile_commands_copy" "$input_program_dir" >> "$work/facts-copy.tmp"
    mv "$work/facts-copy.tmp" "$work/facts-copy.txt"
    facts="$work/facts-copy.txt"
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

# ---- the five holes listed in sc-mp pull request #6 -------------------------------------------------------

holes=0
hole_bad=""
nl='
'
tab=$(printf '\t')
ff=$(printf '\f')
vt=$(printf '\v')
cr=$(printf '\r')

write_facts() {   # <new facts file> <link line file>: the real facts, with another link_file
    grep -v '^link_file=' "$facts" > "$1"
    echo "link_file=$2" >> "$1"
}

fact() {   # <key>: the value after key= in the generated facts
    sed -n "s/^$1=//p" "$facts" | head -n 1
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

variant_env_refused() {   # <what> <output text> <environment name> <environment value>
    if env "$3=$4" sh "$checker" "$copy_root" "$facts" > "$work/variant.log" 2>&1; then
        hole_bad="$hole_bad; accepted: $1"
    elif ! grep -Fq -- "$2" "$work/variant.log"; then
        hole_bad="$hole_bad; refused for another reason: $1"
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

refuse_printf() {   # <what> <words the output must hold> <printf format>: the bytes it prints, added to main.cpp, must be refused
    copy_sources
    printf "$3" >> "$copy_root/server/main.cpp"
    variant_refused "$1" "$2" "$facts"
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
link_head=$(python3 - "$(fact cache)" "$(fact cxx_compiler_id)" <<'PY'
import shlex
import sys

cache_path, compiler_id = sys.argv[1:]
compiler = ""
with open(cache_path, encoding="utf-8") as stream:
    for line in stream:
        key_type, separator, value = line.rstrip("\n").partition("=")
        if separator and key_type.partition(":")[0] == "CMAKE_CXX_COMPILER":
            compiler = value
if not compiler:
    raise SystemExit("CMakeCache.txt does not name the configured C++ compiler")
arguments = [compiler]
if "clang" in compiler_id.lower():
    arguments.append("--no-default-config")
arguments.extend(["-O3", "-DNDEBUG", "CMakeFiles/sco-relay.dir/main.cpp.o", "-o", "../sco-relay"])
print(shlex.join(arguments))
PY
)
link_libs="$(fact server_library) $(fact net_library) $(fact sockets_library)"

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
    "$link_head /tmp/game.a${tab}$(fact net_library) $(fact server_library) $(fact sockets_library)"
refuse_link "an @response file" "response file" "$link_head $link_libs @CMakeFiles/sco-relay.dir/objects.rsp"
refuse_link "an @response file inside -Wl," "response file" "$link_head $link_libs -Wl,@game.rsp"
# The shell that runs link.txt drops quotes and backslash escapes, so they must not hide a word.
refuse_link "a single-quoted archive inside -Wl,--whole-archive,..." "unexpected libraries on the link line: " \
    "$link_head $link_libs -Wl,--whole-archive,'/opt/game.a',--no-whole-archive"
refuse_link "a single-quoted @response file" "response file" "$link_head $link_libs '@game.rsp'"
refuse_link "a backslash-escaped archive" "unexpected libraries on the link line: " "$link_head $link_libs /opt/ga\\me.a"
refuse_link "a backslash-escaped @response file" "response file" "$link_head $link_libs \\@game.rsp"
hole_done H2 "a library hidden in a -Wl, list, behind a tab, in quotes or in an @response file is refused"

# The old unique-basename comparison could accept another archive that happened to use an allowed name, or a
# duplicate occurrence of an expected archive. Both must be tied to the exact CMake archive paths and counts.
mkdir -p "$work/substitute"
: > "$work/substitute/libsco_net.a"
refuse_link "a same-named archive from another directory" "substituted or duplicate archive inputs" \
    "$link_head $(fact server_library) $work/substitute/libsco_net.a $(fact sockets_library)"
refuse_link "a duplicate occurrence of the server archive" "substituted or duplicate archive inputs" \
    "$link_head $link_libs $(fact server_library)"
hole_done B7 "archives must resolve to the generated library paths and appear exactly once"

# Ordinary GCC/Clang linker options remain usable when they do not hide inputs.
accept_link() {   # <what> <link line>
    printf '%s\n' "$2" > "$work/link.txt"
    write_facts "$work/facts-link.txt" "$work/link.txt"
    variant_accepted "$1" "$work/facts-link.txt"
}
accept_link "-Wl,-z,relro (ordinary GCC/Clang linker option)" "$link_head $link_libs -Wl,-z,relro"
accept_link "-Xlinker -z -Xlinker relro (ordinary forwarded linker option)" \
    "$link_head $link_libs -Xlinker -z -Xlinker relro"
accept_link "-Wl,-Ttext,ADDRESS (ordinary ld section-address option)" "$link_head $link_libs -Wl,-Ttext,0x400000"
accept_link "-Wl,-Tbss,ADDRESS (ordinary ld section-address option)" "$link_head $link_libs -Wl,-Tbss,0x500000"
accept_link "CMake's -Xlinker --dependency-file output option" \
    "$link_head $link_libs -Xlinker --dependency-file=server/CMakeFiles/sco-relay.dir/link.d"
hole_done B0 "ordinary GCC/Clang linker options remain accepted"

# B2: a linker may consume objects in .obj form, and a positional file can be a linker script even without a suffix.
refuse_link "an additional object with a .obj suffix" "unexpected objects on the link line" \
    "$link_head $link_libs /tmp/game.obj"
refuse_link "an additional object with an uppercase .OBJ suffix" "unexpected objects on the link line" \
    "$link_head $link_libs /tmp/game.OBJ"
refuse_link "an extensionless positional linker input" "unrecognized link option or positional input" \
    "$link_head $link_libs /tmp/game"
refuse_link "an extensionless input inside a -Wl list" "unrecognized link option or positional input" \
    "$link_head $link_libs -Wl,/tmp/game"
refuse_link "an extensionless input forwarded with -Xlinker" "unrecognized link option or positional input" \
    "$link_head $link_libs -Xlinker /tmp/game"
hole_done B8 "raw .obj objects and unrecognized positional linker inputs are refused"

# B6: even with the explicit target archives unchanged, -L can make the linker select an untrusted implicit library
# such as libstdc++.so. The relay's CMake link command uses exact target paths and needs no extra library search path.
refuse_link "a direct library search path" "unrecognized link option or positional input" \
    "$link_head $link_libs -L /tmp/relay-injected-libraries"
refuse_link "an attached direct library search path" "unrecognized link option or positional input" \
    "$link_head $link_libs -L/tmp/relay-injected-libraries"
refuse_link "a -Wl library search path" "unrecognized link option or positional input" \
    "$link_head $link_libs -Wl,-L,/tmp/relay-injected-libraries"
refuse_link "an attached -Wl library search path" "unrecognized link option or positional input" \
    "$link_head $link_libs -Wl,-L/tmp/relay-injected-libraries"
refuse_link "an -Xlinker library search path" "unrecognized link option or positional input" \
    "$link_head $link_libs -Xlinker -L -Xlinker /tmp/relay-injected-libraries"
refuse_link "an attached -Xlinker library search path" "unrecognized link option or positional input" \
    "$link_head $link_libs -Xlinker -L/tmp/relay-injected-libraries"
hole_done B6 "direct and forwarded library search paths are refused"

# B3: refuse every GNU ld script spelling, whether it reaches ld directly, through -Wl, or via -Xlinker.
refuse_link "-T followed by a script path" "linker script option" "$link_head $link_libs -T /tmp/game.ld"
refuse_link "-T concatenated with a script path" "linker script option" "$link_head $link_libs -T/tmp/game.ld"
refuse_link "--script followed by a script path" "linker script option" "$link_head $link_libs --script /tmp/game.ld"
refuse_link "--script= with a script path" "linker script option" "$link_head $link_libs --script=/tmp/game.ld"
refuse_link "-Wl,-T,script" "linker script option" "$link_head $link_libs -Wl,-T,/tmp/game.ld"
refuse_link "-Wl,-Tscript" "linker script option" "$link_head $link_libs -Wl,-T/tmp/game.ld"
refuse_link "-Wl,--script,script" "linker script option" "$link_head $link_libs -Wl,--script,/tmp/game.ld"
refuse_link "-Wl,--script=script" "linker script option" "$link_head $link_libs -Wl,--script=/tmp/game.ld"
refuse_link "-Xlinker -T -Xlinker script" "linker script option" \
    "$link_head $link_libs -Xlinker -T -Xlinker /tmp/game.ld"
refuse_link "-Xlinker -Tscript" "linker script option" "$link_head $link_libs -Xlinker -T/tmp/game.ld"
refuse_link "-Xlinker --script -Xlinker script" "linker script option" \
    "$link_head $link_libs -Xlinker --script -Xlinker /tmp/game.ld"
refuse_link "-Xlinker --script=script" "linker script option" \
    "$link_head $link_libs -Xlinker --script=/tmp/game.ld"
refuse_link "-Xlinker=--script=script" "unrecognized link option or positional input" \
    "$link_head $link_libs -Xlinker=--script=/tmp/game.ld"
refuse_link "-c MRI script path" "linker script option" "$link_head $link_libs -c /tmp/game.mri"
refuse_link "-c with an attached MRI script path" "linker script option" "$link_head $link_libs -c/tmp/game.mri"
refuse_link "--mri-script path" "linker script option" "$link_head $link_libs --mri-script /tmp/game.mri"
refuse_link "--mri-script= path" "linker script option" "$link_head $link_libs --mri-script=/tmp/game.mri"
refuse_link "--default-script path" "linker script option" "$link_head $link_libs --default-script /tmp/game.ld"
refuse_link "--default-script= path" "linker script option" "$link_head $link_libs --default-script=/tmp/game.ld"
refuse_link "-dT default script" "linker script option" "$link_head $link_libs -dT /tmp/game.ld"
refuse_link "-dT with an attached script path" "linker script option" "$link_head $link_libs -dT/tmp/game.ld"
refuse_link "--error-handling-script path" "linker script option" \
    "$link_head $link_libs --error-handling-script /tmp/errors.sh"
refuse_link "--error-handling-script= path" "linker script option" \
    "$link_head $link_libs --error-handling-script=/tmp/errors.sh"
refuse_link "--version-script path" "linker script option" "$link_head $link_libs --version-script /tmp/game.map"
refuse_link "--version-script= path" "linker script option" "$link_head $link_libs --version-script=/tmp/game.map"
refuse_link "--remap-inputs-file path" "unsafe linker plugin or remap input option" \
    "$link_head $link_libs --remap-inputs-file /tmp/remap.txt"
refuse_link "--remap-inputs-file= path" "unsafe linker plugin or remap input option" \
    "$link_head $link_libs --remap-inputs-file=/tmp/remap.txt"
refuse_link "a remap file passed through -Wl" "unsafe linker plugin or remap input option" \
    "$link_head $link_libs -Wl,--remap-inputs-file,/tmp/remap.txt"
refuse_link "a linker plugin path" "unsafe linker plugin or remap input option" \
    "$link_head $link_libs -Wl,-plugin,/tmp/plugin.so"
refuse_link "an embedded linker plugin path" "unsafe linker plugin or remap input option" \
    "$link_head $link_libs -Wl,--plugin=/tmp/plugin.so"
refuse_link "an attached linker plugin path" "unsafe linker plugin or remap input option" \
    "$link_head $link_libs -Wl,-plugin=/tmp/plugin.so"
refuse_link "a direct -plugin= path" "unsafe linker plugin or remap input option" \
    "$link_head $link_libs -plugin=/tmp/plugin.so"
refuse_link "a direct --plugin= path" "unsafe linker plugin or remap input option" \
    "$link_head $link_libs --plugin=/tmp/plugin.so"
refuse_link "a plugin option carrying an otherwise approved archive" "unsafe linker plugin or remap input option" \
    "$link_head $(fact server_library) -Wl,-plugin,$(fact net_library) $(fact sockets_library)"
refuse_link "an embedded plugin option carrying an approved archive" "unsafe linker plugin or remap input option" \
    "$link_head $(fact server_library) -Wl,--plugin=$(fact net_library) $(fact sockets_library)"
hole_done B9 "GNU ld script options are refused through direct, -Wl and -Xlinker forms"

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
refuse_source "a lone carriage return, which ends a line for GCC and Clang" "main.cpp: game_pack.h" \
    "int x = 0;${cr}#include <game_pack.h>"
# A number never contains a literal: the second quote of '1''a/*' starts a character literal, not a digit separator, so
# no comment opens and a directive that only the C++ reading can find stays visible.
refuse_source "'1''a/*' inside #if 0, then #/**/include" "main.cpp: game_pack.h" \
    "#if 0${nl}'1''a/*'${nl}#endif${nl}#/**/include <game_pack.h>${nl}#if 0${nl}*/${nl}#endif"
refuse_source "'1''a/*' inside #if 0, then a directive split by a comment" "main.cpp: game_pack.h" \
    "#if 0${nl}'1''a/*'${nl}#endif${nl}#/*${nl}*/include <game_pack.h>${nl}#if 0${nl}*/${nl}#endif"
refuse_source "'0''x/*' in a define, then %:/**/include" "main.cpp: game_pack.h" \
    "#define U '0''x/*'${nl}%:/**/include <game_pack.h>${nl}#define V */"
refuse_source "u8'1''a/*' then #/**/include" "main.cpp: game_pack.h" \
    "int u = u8'1''a/*';${nl}#/**/include <game_pack.h>${nl}#define W */"
# GCC ignores NUL bytes, so they must not hide a directive.
refuse_printf "a NUL byte before the hash" "main.cpp: game_pack.h" '\n\000#include <game_pack.h>\n'
refuse_printf "a NUL byte after the hash" "main.cpp: game_pack.h" '\n#\000include <game_pack.h>\n'
hole_done H3 "an include behind a comment, a line splice or a literal is found; text that cannot be read is refused"

# H4: the other spellings of the directive.
refuse_source "the digraph %:include" "main.cpp: game_pack.h" "%:include <game_pack.h>"
refuse_source "the digraph with a blank, %: include" "main.cpp: game_pack.h" "%: include <game_pack.h>"
refuse_source "the digraph and a comment, %:/**/include" "main.cpp: game_pack.h" "%:/**/include <game_pack.h>"
refuse_source "#import, the other directive GCC accepts" "main.cpp: game_pack.h" "#import <game_pack.h>"
refuse_source "#include_next cannot bypass the include allowlist" "main.cpp: #include_next" \
    "#include_next <game_pack.h>"
hole_done H4 "the %: digraph spelling, #import and #include_next are refused"

# H5: only a same-directory server header is ours. A header in a folder below server/ (the tests, the tools) is not
# production, and the product has no repository-root include path to resolve "server/<name>.h".
for folder in tests tools; do
    copy_sources
    mkdir -p "$copy_root/server/$folder"
    : > "$copy_root/server/$folder/probe.h"
    printf '\n#include "server/%s/probe.h"\n' "$folder" >> "$copy_root/server/main.cpp"
    variant_refused "an include of server/$folder/probe.h" "main.cpp: server/$folder/probe.h" "$facts"
done
accept_source "a same-directory server header (control)" "#include \"log.h\""
hole_done H5 "a header from a folder below server/ is refused, and a same-directory server header is accepted"

# B1: a system name is safe only if compiler lookup reaches the platform header. Only kit/include is searched for
# angle headers; quoted headers also search the including file's own source directory.
copy_sources
: > "$copy_root/algorithm"
printf '\n#include <algorithm>\n' >> "$copy_root/server/main.cpp"
variant_accepted "a repository-root file cannot shadow an angle include when the root is not an include path" "$facts"
hole_done B1 "a repository-root file is not mistaken for an angle-header shadow"

copy_sources
mkdir -p "$copy_root/server"
: > "$copy_root/server/algorithm"
printf '\n#include "algorithm"\n' >> "$copy_root/server/main.cpp"
variant_refused "a quoted system include shadowed by the server source directory" \
    "main.cpp: algorithm is shadowed by" "$facts"
hole_done B1 "an allowlisted quoted header shadowed from its source directory is refused"

copy_sources
mkdir -p "$copy_root/server"
: > "$copy_root/server/algorithm"
printf '\n#include <algorithm>\n' >> "$copy_root/server/main.cpp"
variant_accepted "an angle system include ignores the server source directory" "$facts"
hole_done B1 "a source directory that is not an angle include path does not cause a false refusal"

write_facts_kit() {   # <new facts file> <kit root>: the generated facts with another kit directory
    grep -v '^kit=' "$facts" > "$1"
    echo "kit=$2" >> "$1"
}
copy_sources
mkdir -p "$work/fake-kit/include"
: > "$work/fake-kit/include/algorithm"
printf '\n#include <algorithm>\n' >> "$copy_root/server/main.cpp"
write_facts_kit "$work/facts-kit.txt" "$work/fake-kit"
variant_refused "an angle system include shadowed by the kit include directory" \
    "main.cpp: algorithm is shadowed by" "$work/facts-kit.txt"
hole_done B1 "an allowlisted header shadowed from the kit's actual include directory is refused"

copy_sources
mkdir -p "$copy_root/plugins/multiplayer"
: > "$copy_root/plugins/multiplayer/algorithm"
printf '\n#include "algorithm"\n' >> "$copy_root/plugins/multiplayer/relay_wire.h"
variant_refused "a quoted system include shadowed beside the shared wire header" \
    "relay_wire.h: algorithm is shadowed by" "$facts"
hole_done B1 "an allowlisted quoted header shadowed beside the shared header is refused"

# SDK headers are admitted only when the quoted lookup cannot select a source-local project wrapper first, and the
# exact pinned kit file exists. A repository-root file cannot shadow the quote because the production target has no
# repository-root include directory; a root include path itself is rejected in the next group.
copy_sources
mkdir -p "$copy_root/sco/net"
printf '#pragma once\n#include_next <sco/net/core.h>\n' > "$copy_root/sco/net/core.h"
variant_accepted "a repository-root wrapper cannot shadow the kit header without a repository-root include path" "$facts"
copy_sources
mkdir -p "$copy_root/server/sco/net"
printf '#pragma once\n' > "$copy_root/server/sco/net/core.h"
variant_refused "a quote-local wrapper shadowing the pinned sco-core header" \
    "net_boundary.cpp: sco/net/core.h is shadowed by" "$facts"
hole_done B2 "sco-core headers are resolved from the pinned kit without project shadow wrappers"

# A repository-root include directory would make nested implementation headers such as bits/stl_vector.h visible to
# transitive system includes. The checker must reject that build fact even though its source scanner sees only the
# direct, approved <vector> include.
copy_sources
mkdir -p "$copy_root/bits"
printf '#pragma once\n' > "$copy_root/bits/stl_vector.h"
printf '\n#include <vector>\n' >> "$copy_root/server/main.cpp"
variant_accepted "a transitive system-header shadow file outside every product include directory" "$facts"
write_facts_include() {   # <new facts file> <include directory list>
    grep -v '^production_include_dirs=' "$facts" > "$1"
    printf 'production_include_dirs=%s\n' "$2" >> "$1"
}
write_facts_include "$work/facts-root-include.txt" "$copy_root"
variant_refused "the same transitive system-header shadow when the project root enters the compiler paths" \
    "product compiler include paths include directories outside the pinned kit" "$work/facts-root-include.txt"

write_facts_include "$work/facts-external-include.txt" "$work/external-headers"
variant_refused "an external include directory that shadows a system header" \
    "product compiler include paths include directories outside the pinned kit" "$work/facts-external-include.txt"

write_compile_variant() {   # <output JSON> <additional compiler arguments...>
    output=$1
    shift
    python3 - "$compile_commands_copy" "$output" "$@" <<'PY'
import json
import sys

source, output, *extra = sys.argv[1:]
with open(source, encoding="utf-8") as stream:
    entries = json.load(stream)
entries[0]["arguments"].extend(extra)
with open(output, "w", encoding="utf-8") as stream:
    json.dump(entries, stream)
PY
}
write_facts_compile() {   # <facts output> <compile commands file>
    grep -v '^compile_commands=' "$facts" > "$1"
    printf 'compile_commands=%s\n' "$2" >> "$1"
}
write_compile_variant "$work/compile-root-include.json" -I "$copy_root"
write_facts_compile "$work/facts-compile-root.txt" "$work/compile-root-include.json"
variant_refused "a CXXFLAGS-style repository include path in the actual compile command" \
    "compiler include path is outside the pinned kit directory" "$work/facts-compile-root.txt"
mkdir -p "$work/external-headers"
: > "$work/external-headers/algorithm"
write_compile_variant "$work/compile-external-include.json" -isystem "$work/external-headers"
write_facts_compile "$work/facts-compile-external.txt" "$work/compile-external-include.json"
variant_refused "an external compiler include path in the actual compile command" \
    "compiler include path is outside the pinned kit directory" "$work/facts-compile-external.txt"
write_compile_variant "$work/compile-cxx-isystem.json" -cxx-isystem "$work/external-headers"
write_facts_compile "$work/facts-cxx-isystem.txt" "$work/compile-cxx-isystem.json"
variant_refused "Clang's C++ system-include path spelling" \
    "compiler include path is outside the pinned kit directory" "$work/facts-cxx-isystem.txt"
write_compile_variant "$work/compile-include-directory.json" --include-directory "$work/external-headers"
write_facts_compile "$work/facts-include-directory.txt" "$work/compile-include-directory.json"
variant_refused "Clang's long include-directory spelling" \
    "compiler include path is outside the pinned kit directory" "$work/facts-include-directory.txt"
write_compile_variant "$work/compile-stdlib-system.json" "-stdlib++-isystem$work/external-headers"
write_facts_compile "$work/facts-stdlib-system.txt" "$work/compile-stdlib-system.json"
variant_refused "Clang's standard-library include-path spelling" \
    "compiler include path is outside the pinned kit directory" "$work/facts-stdlib-system.txt"
for include_flag in \
    -iwithsysroot \
    -isystem-after \
    -iframework \
    -iframeworkwithsysroot \
    -imsvc \
    --include-directory-after; do
    include_name=$(printf '%s' "$include_flag" | tr -cd '[:alnum:]')
    write_compile_variant "$work/compile-$include_name.json" "$include_flag" "$work/external-headers"
    write_facts_compile "$work/facts-$include_name.txt" "$work/compile-$include_name.json"
    variant_refused "compiler include search option $include_flag" \
        "compiler include path is outside the pinned kit directory" "$work/facts-$include_name.txt"
done
write_compile_variant "$work/compile-forced-include.json" -include "$copy_root/game_code.h"
write_facts_compile "$work/facts-forced-include.txt" "$work/compile-forced-include.json"
variant_refused "a forced include added by target or global compile flags" \
    "forced include, PCH, VFS overlay, plugin, specs, wrapper or frontend input is not allowed" "$work/facts-forced-include.txt"
write_compile_variant "$work/compile-clang-include-equals.json" "--include=$copy_root/game_code.h"
write_facts_compile "$work/facts-clang-include-equals.txt" "$work/compile-clang-include-equals.json"
variant_refused "Clang's attached --include forced-header alias" \
    "forced include or macro input is not allowed" "$work/facts-clang-include-equals.txt"
write_compile_variant "$work/compile-clang-include-attached.json" "--include$copy_root/game_code.h"
write_facts_compile "$work/facts-clang-include-attached.txt" "$work/compile-clang-include-attached.json"
variant_refused "Clang's joined --include forced-header alias" \
    "forced include or macro input is not allowed" "$work/facts-clang-include-attached.txt"
write_compile_variant "$work/compile-clang-include-separated.json" --include "$copy_root/game_code.h"
write_facts_compile "$work/facts-clang-include-separated.txt" "$work/compile-clang-include-separated.json"
variant_refused "Clang's separated --include forced-header alias" \
    "forced include or macro input is not allowed" "$work/facts-clang-include-separated.txt"
write_compile_variant "$work/compile-clang-imacros-equals.json" "--imacros=$copy_root/game_macros.h"
write_facts_compile "$work/facts-clang-imacros-equals.txt" "$work/compile-clang-imacros-equals.json"
variant_refused "Clang's attached --imacros forced-macro alias" \
    "forced include or macro input is not allowed" "$work/facts-clang-imacros-equals.txt"
write_compile_variant "$work/compile-clang-imacros-attached.json" "--imacros$copy_root/game_macros.h"
write_facts_compile "$work/facts-clang-imacros-attached.txt" "$work/compile-clang-imacros-attached.json"
variant_refused "Clang's joined --imacros forced-macro alias" \
    "forced include or macro input is not allowed" "$work/facts-clang-imacros-attached.txt"
write_compile_variant "$work/compile-clang-imacros-separated.json" --imacros "$copy_root/game_macros.h"
write_facts_compile "$work/facts-clang-imacros-separated.txt" "$work/compile-clang-imacros-separated.json"
variant_refused "Clang's separated --imacros forced-macro alias" \
    "forced include or macro input is not allowed" "$work/facts-clang-imacros-separated.txt"
write_compile_variant "$work/compile-pch.json" -include-pch "$copy_root/game.pch"
write_facts_compile "$work/facts-pch.txt" "$work/compile-pch.json"
variant_refused "a precompiled header added to a product compile command" \
    "forced include, PCH, VFS overlay, plugin, specs, wrapper or frontend input is not allowed" "$work/facts-pch.txt"
write_compile_variant "$work/compile-pass-plugin.json" "-fpass-plugin=$work/pass-plugin.so"
write_facts_compile "$work/facts-pass-plugin.txt" "$work/compile-pass-plugin.json"
variant_refused "Clang's pass-plugin code-loading option" \
    "forced include, PCH, VFS overlay, plugin, specs, wrapper or frontend input is not allowed" "$work/facts-pass-plugin.txt"
write_compile_variant "$work/compile-B-prefix.json" -B "$work/compiler-prefix"
write_facts_compile "$work/facts-B-prefix.txt" "$work/compile-B-prefix.json"
variant_refused "a GCC -B compiler prefix that adds implicit include roots" \
    "alternate compiler, toolchain or system-header roots are not allowed" "$work/facts-B-prefix.txt"
write_compile_variant "$work/compile-clang-prefix-equals.json" "--prefix=$work/compiler-prefix"
write_facts_compile "$work/facts-clang-prefix-equals.txt" "$work/compile-clang-prefix-equals.json"
variant_refused "Clang's attached executable/library search prefix" \
    "alternate compiler, toolchain or system-header roots are not allowed" "$work/facts-clang-prefix-equals.txt"
write_compile_variant "$work/compile-clang-prefix-separated.json" --prefix "$work/compiler-prefix"
write_facts_compile "$work/facts-clang-prefix-separated.txt" "$work/compile-clang-prefix-separated.json"
variant_refused "Clang's separated executable/library search prefix" \
    "alternate compiler, toolchain or system-header roots are not allowed" "$work/facts-clang-prefix-separated.txt"
write_compile_variant "$work/compile-clang-external-as.json" -fno-integrated-as
write_facts_compile "$work/facts-clang-external-as.txt" "$work/compile-clang-external-as.json"
variant_refused "Clang's external assembler option" \
    "selecting an external assembler is not allowed" "$work/facts-clang-external-as.txt"
for driver_pair in \
    "specs|-specs" \
    "wrapper|-wrapper"; do
    driver_name=${driver_pair%%|*}
    driver_option=${driver_pair#*|}
    write_compile_variant "$work/compile-$driver_name.json" "$driver_option" "$work/$driver_name.input"
    write_facts_compile "$work/facts-$driver_name.txt" "$work/compile-$driver_name.json"
    variant_refused "GCC's separated -$driver_name compiler override" \
        "forced include, PCH, VFS overlay, plugin, specs, wrapper or frontend input is not allowed" \
        "$work/facts-$driver_name.txt"
done
write_compile_variant "$work/compile-specs-attached.json" "-specs=$work/specs.input"
write_facts_compile "$work/facts-specs-attached.txt" "$work/compile-specs-attached.json"
variant_refused "GCC's attached -specs compiler override" \
    "forced include, PCH, VFS overlay, plugin, specs, wrapper or frontend input is not allowed" \
    "$work/facts-specs-attached.txt"
write_compile_variant "$work/compile-wrapper-attached.json" "-wrapper=$work/compiler-wrapper"
write_facts_compile "$work/facts-wrapper-attached.txt" "$work/compile-wrapper-attached.json"
variant_refused "GCC's attached -wrapper compiler override" \
    "forced include, PCH, VFS overlay, plugin, specs, wrapper or frontend input is not allowed" \
    "$work/facts-wrapper-attached.txt"
write_compile_variant "$work/compile-clang-config.json" "--config=$work/clang.cfg"
write_facts_compile "$work/facts-clang-config.txt" "$work/compile-clang-config.json"
variant_refused "Clang's external compiler configuration file option" \
    "compiler configuration files are not allowed" "$work/facts-clang-config.txt"
write_compile_variant "$work/compile-clang-config-separated.json" --config "$work/clang.cfg"
write_facts_compile "$work/facts-clang-config-separated.txt" "$work/compile-clang-config-separated.json"
variant_refused "Clang's separated compiler configuration file option" \
    "compiler configuration files are not allowed" "$work/facts-clang-config-separated.txt"
write_compile_variant "$work/compile-clang-config-user-dir.json" "--config-user-dir=$work/clang-configs"
write_facts_compile "$work/facts-clang-config-user-dir.txt" "$work/compile-clang-config-user-dir.json"
variant_refused "Clang's user configuration search directory" \
    "compiler configuration search directories are not allowed" "$work/facts-clang-config-user-dir.txt"
write_compile_with_flag_all() {   # <output JSON> <compiler flag>: add to every synthetic compile command
    python3 - "$compile_commands_copy" "$1" "$2" <<'PY'
import json
import sys

source, output, flag = sys.argv[1:]
with open(source, encoding="utf-8") as stream:
    entries = json.load(stream)
for entry in entries:
    entry["arguments"].insert(1, flag)
with open(output, "w", encoding="utf-8") as stream:
    json.dump(entries, stream)
PY
}
write_compile_without_flag_all() {   # <output JSON> <compiler flag>: remove the flag from every compile command
    python3 - "$compile_commands_copy" "$1" "$2" <<'PY'
import json
import sys

source, output, flag = sys.argv[1:]
with open(source, encoding="utf-8") as stream:
    entries = json.load(stream)
for entry in entries:
    arguments = entry.get("arguments", [])
    entry["arguments"] = [argument for argument in arguments if argument != flag]
with open(output, "w", encoding="utf-8") as stream:
    json.dump(entries, stream)
PY
}
write_link_flag() {   # <source link.txt> <output link.txt> <flag>: add a driver option after argv[0]
    python3 - "$1" "$2" "$3" <<'PY'
import shlex
import sys

source, output, flag = sys.argv[1:]
with open(source, encoding="utf-8") as stream:
    argv = shlex.split(stream.read())
argv.insert(1, flag)
with open(output, "w", encoding="utf-8") as stream:
    stream.write(shlex.join(argv) + "\n")
PY
}
write_link_without_flag() {   # <source link.txt> <output link.txt> <flag>: remove the driver option
    python3 - "$1" "$2" "$3" <<'PY'
import shlex
import sys

source, output, flag = sys.argv[1:]
with open(source, encoding="utf-8") as stream:
    arguments = shlex.split(stream.read())
arguments = [argument for argument in arguments if argument != flag]
with open(output, "w", encoding="utf-8") as stream:
    stream.write(shlex.join(arguments) + "\n")
PY
}
write_clang_facts() {   # <output facts> <compile DB> <link command>
    grep -Ev '^c_compiler_id=|^cxx_compiler_id=|^compile_commands=|^link_file=' "$facts" > "$1"
    printf 'c_compiler_id=Clang\ncxx_compiler_id=Clang\ncompile_commands=%s\nlink_file=%s\n' \
        "$2" "$3" >> "$1"
}
write_compile_without_flag_all "$work/compile-clang-defaults-enabled.json" --no-default-config
write_link_without_flag "$(fact link_file)" "$work/link-clang-defaults-enabled.txt" --no-default-config
write_clang_facts "$work/facts-clang-no-default.txt" \
    "$work/compile-clang-defaults-enabled.json" "$work/link-clang-defaults-enabled.txt"
variant_refused "Clang compile commands without --no-default-config" \
    "Clang default configuration files are not disabled" "$work/facts-clang-no-default.txt"
write_compile_with_flag_all "$work/compile-clang-defaults-disabled.json" --no-default-config
write_clang_facts "$work/facts-clang-link-defaults-enabled.txt" \
    "$work/compile-clang-defaults-disabled.json" "$work/link-clang-defaults-enabled.txt"
variant_refused "a Clang program link that can load a default config" \
    "Clang default configuration files are not disabled for the program link" "$work/facts-clang-link-defaults-enabled.txt"
write_link_flag "$(fact link_file)" "$work/link-clang-defaults-disabled.txt" --no-default-config
write_clang_facts "$work/facts-clang-defaults-disabled.txt" \
    "$work/compile-clang-defaults-disabled.json" "$work/link-clang-defaults-disabled.txt"
variant_accepted "Clang compile and link commands with default config loading disabled" \
    "$work/facts-clang-defaults-disabled.txt"
write_compile_with_flag_all "$work/compile-clang-config-user-all.json" --no-default-config
python3 - "$work/compile-clang-config-user-all.json" "$work/clang-configs" <<'PY'
import json
import sys

path, config_dir = sys.argv[1:]
with open(path, encoding="utf-8") as stream:
    entries = json.load(stream)
entries[0]["arguments"].append("--config-user-dir=" + config_dir)
with open(path, "w", encoding="utf-8") as stream:
    json.dump(entries, stream)
PY
write_clang_facts "$work/facts-clang-explicit-user-dir.txt" \
    "$work/compile-clang-config-user-all.json" "$work/link-clang-defaults-disabled.txt"
variant_refused "Clang's explicit config user directory even with defaults disabled" \
    "compiler configuration search directories are not allowed" "$work/facts-clang-explicit-user-dir.txt"
write_link_flag "$work/link-clang-defaults-disabled.txt" "$work/link-clang-user-dir.txt" \
    "--config-user-dir=$work/clang-configs"
write_clang_facts "$work/facts-clang-link-user-dir.txt" \
    "$work/compile-clang-defaults-disabled.json" "$work/link-clang-user-dir.txt"
variant_refused "Clang's explicit link-time config directory" \
    "Clang configuration files or search directories are not allowed in the program link command" \
    "$work/facts-clang-link-user-dir.txt"
for include_context_flag in -iprefix -iwithprefix -iwithprefixbefore -imultilib -imultiarch \
    --include-prefix --include-with-prefix --include-with-prefix-before --include-with-prefix-after; do
    include_context_name=$(printf '%s' "$include_context_flag" | tr -cd '[:alnum:]')
    write_compile_variant "$work/compile-$include_context_name.json" "$include_context_flag" "$work/injected-include-prefix"
    write_facts_compile "$work/facts-$include_context_name.txt" "$work/compile-$include_context_name.json"
    variant_refused "compiler include prefix or multiarch option $include_context_flag" \
        "compiler include prefixes or multiarch paths are not allowed" "$work/facts-$include_context_name.txt"
done
kit_include_path=$(fact kit_include)
write_compile_variant "$work/compile-iwithprefix-composition.json" \
    -iprefix "$kit_include_path/" -iwithprefix ../untrusted/include
write_facts_compile "$work/facts-iwithprefix-composition.txt" "$work/compile-iwithprefix-composition.json"
variant_refused "a relative GCC include prefix composed with -iprefix" \
    "compiler include prefixes or multiarch paths are not allowed" "$work/facts-iwithprefix-composition.txt"
write_compile_variant "$work/compile-long-prefix-composition.json" \
    --include-prefix "$kit_include_path/" --include-with-prefix "$kit_include_path"
write_facts_compile "$work/facts-long-prefix-composition.txt" "$work/compile-long-prefix-composition.json"
variant_refused "long include-prefix arguments each naming the kit but composing another path" \
    "compiler include prefixes or multiarch paths are not allowed" "$work/facts-long-prefix-composition.txt"
write_compile_variant "$work/compile-Xpreprocessor.json" -Xpreprocessor -include -Xpreprocessor "$work/injected.h"
write_facts_compile "$work/facts-Xpreprocessor.txt" "$work/compile-Xpreprocessor.json"
variant_refused "Clang's opaque preprocessor argument forwarding" \
    "opaque compiler/preprocessor forwarding is not inspected" "$work/facts-Xpreprocessor.txt"
variant_env_refused "an implicit CPATH entry" "compiler environment overrides include, linker or toolchain search: CPATH" CPATH "$copy_root"
variant_env_refused "Clang's compiler argument override environment" \
    "compiler environment overrides include, linker or toolchain search: CCC_OVERRIDE_OPTIONS" CCC_OVERRIDE_OPTIONS "-include"
variant_env_refused "CMake's implicit compiler flag initialization" \
    "compiler environment overrides include, linker or toolchain search: CXXFLAGS" CXXFLAGS "-include $work/injected.h"
variant_env_refused "CMake's implicit linker flag initialization" \
    "compiler environment overrides include, linker or toolchain search: LDFLAGS" LDFLAGS "-Wl,-T,$work/injected.ld"
variant_env_refused "GCC's implicit linker search path" \
    "compiler environment overrides include, linker or toolchain search: LIBRARY_PATH" LIBRARY_PATH "$work/untrusted-libraries"
variant_env_refused "MSVC's implicit linker search path" \
    "compiler environment overrides include, linker or toolchain search: LIB" LIB "$work/untrusted-libraries"
variant_env_refused "MSVC's linker option environment" \
    "compiler environment overrides include, linker or toolchain search: LINK" LINK "/LIBPATH:$work/untrusted-libraries"
write_compile_variant "$work/compile-mllvm.json" -mllvm -load="$work/pass-plugin.so"
write_facts_compile "$work/facts-mllvm.txt" "$work/compile-mllvm.json"
variant_refused "Clang LLVM backend plugin forwarding" \
    "LLVM backend option forwarding is not inspected" "$work/facts-mllvm.txt"
write_compile_without_output() {   # <output JSON>: model CMake 3.20's compile_commands format
    python3 - "$compile_commands_copy" "$1" <<'PY'
import json
import os
import sys

source, output = sys.argv[1:]
with open(source, encoding="utf-8") as stream:
    entries = json.load(stream)
for entry in entries:
    object_file = os.path.relpath(entry.pop("output"), entry["directory"])
    entry["arguments"][1:1] = ["-o", object_file]
with open(output, "w", encoding="utf-8") as stream:
    json.dump(entries, stream)
PY
}
write_compile_without_output "$work/compile-cmake-320.json"
write_facts_compile "$work/facts-cmake-320.txt" "$work/compile-cmake-320.json"
variant_accepted "the CMake 3.20 compile database form with object paths read from -o" "$work/facts-cmake-320.txt"
hole_done B10 "effective compiler inputs reject header aliases, tool prefixes, external assemblers, forced inputs, plugins and implicit environment paths"

# Match source identities, not just archive member basenames. A substituted source with the same filename must fail
# even if the static archive still reports session.cpp.o.
copy_sources
cp "$copy_root/server/session.cpp" "$copy_root/plugins/multiplayer/session.cpp"
printf '\n#include <game_pack.h>\n' >> "$copy_root/plugins/multiplayer/session.cpp"
substituted_sources=$(fact server_sources | sed "s|$copy_root/server/session.cpp|$copy_root/plugins/multiplayer/session.cpp|")
write_facts_sources() {   # <new facts file> <source list>
    grep -v '^server_sources=' "$facts" > "$1"
    printf 'server_sources=%s\n' "$2" >> "$1"
}
write_facts_sources "$work/facts-substituted-source.txt" "$substituted_sources"
variant_refused "an out-of-tree source replacing server/session.cpp with the same basename" \
    "CMake server sources differ from server/*.cpp" "$work/facts-substituted-source.txt"
hole_done B4 "the built source list is matched to the reviewed server source files, not archive names alone"

# B5: the shared relay wire header is production code too, so its includes and sco-core references are checked.
copy_sources
printf '\n#include <game_pack.h>\n' >> "$copy_root/plugins/multiplayer/relay_wire.h"
variant_refused "an unapproved include in the shared wire header" "relay_wire.h: game_pack.h" "$facts"
copy_sources
printf '\n// sco::net:: hidden in the shared wire header\n' >> "$copy_root/plugins/multiplayer/relay_wire.h"
variant_refused "a sco-core reference in the shared wire header" \
    "plugins/multiplayer/relay_wire.h" "$facts"
copy_sources
printf '#include <game_pack.h>\n' > "$copy_root/plugins/multiplayer/motion.h"
printf '\n#include "motion.h"\n' >> "$copy_root/plugins/multiplayer/relay_wire.h"
variant_refused "an unscanned same-directory plugin header included by the relay wire contract" \
    "relay_wire.h: sibling plugin header motion.h is not scanned" "$facts"
hole_done B5 "the shared relay wire header rejects unscanned sibling plugin headers and sco-core references"

# R1: the preprocessor counts form feed and vertical tab as blanks, before the hash and around it. The plain reading
# takes any blank; a reader that knew only space and tab let these through.
refuse_source "a form feed before the hash" "main.cpp: game_pack.h" "${ff}#include <game_pack.h>"
refuse_source "a vertical tab before the hash" "main.cpp: game_pack.h" "${vt}#include <game_pack.h>"
refuse_source "a vertical tab after the hash" "main.cpp: game_pack.h" "#${vt}include <game_pack.h>"
refuse_source "a form feed after the hash" "main.cpp: game_pack.h" "#${ff}include <game_pack.h>"
refuse_source "a form feed and a vertical tab after the keyword" "main.cpp: game_pack.h" "#include${ff}${vt}<game_pack.h>"
refuse_source "a vertical tab before a digraph" "main.cpp: game_pack.h" "${vt}%:include <game_pack.h>"
hole_done R1 "a form feed or a vertical tab around the directive does not hide it"

# R2: the C++ reading must not enter a comment the compiler does not see. A quote after a number is a digit separator
# only when a letter or digit follows it, and the number may be 1e+ or follow an identifier (x.1). Each text below holds
# a quote followed by /* that opens a character literal for the compiler, then an include, then a line with */. The
# include is spelled plainly (the plain reading finds it), and then #/**/include or a split directive, which only the
# C++ reading can find, so the test fails if the reading is fooled however the plain one is placed.
for spelling in '#include <game_pack.h>' '#/**/include <game_pack.h>' "#inc\\${nl}lude <game_pack.h>"; do
    refuse_source "1'/*' (a quote before a comment opener) then $spelling" "main.cpp: game_pack.h" \
        "#define UNUSED_A 1'/*'${nl}${spelling}${nl}#define UNUSED_B */"
    refuse_source "1e+'a'/*' inside #if 0 then $spelling" "main.cpp: game_pack.h" \
        "#if 0${nl}1e+'a'/*'${nl}#endif${nl}${spelling}${nl}#if 0${nl}*/${nl}#endif"
    refuse_source "x.1'a'/*' (a number after an identifier) then $spelling" "main.cpp: game_pack.h" \
        "#define UNUSED_C x.1'a'/*'${nl}${spelling}${nl}#define UNUSED_D */"
done
hole_done R2 "a quote that only looks like a digit separator cannot hide an include"

# Control: an include that is commented out, or only text in a string, is not an include.
accept_source "a // comment" "// #include <game_pack.h>"
accept_source "a one-line /* */ comment" "/* #include <game_pack.h> */"
accept_source "a comment holding a digraph" "// %:include <game_pack.h>"
accept_source "a string holding a directive" "const char* s = \"#include <game_pack.h>\";"
accept_source "an allowed same-directory include in a file with CR LF line ends" "#include \"log.h\"${cr}"
hole_done control "includes inside one-line comments and strings are still accepted"

# Control: the plain reading is not fooled by comments, so an include on a line of its own inside a multi-line comment,
# or on the line a // comment continues onto, is refused, as the old gate did. This fails closed on purpose.
refuse_source "a multi-line comment" "main.cpp: game_pack.h" "/*${nl}#include <game_pack.h>${nl}*/"
refuse_source "a // comment continued by a backslash" "main.cpp: game_pack.h" "// a comment that goes on \\${nl}#include <game_pack.h>"
hole_done control "an include on a line of its own inside a multi-line comment is refused (fail closed)"

if [ "$holes" -ne 0 ]; then
    echo "relay_no_game_code_checks: $holes of the hole and control cases above failed" >&2
    exit 1
fi
echo "ok: every hole and control case passed"
