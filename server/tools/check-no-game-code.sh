#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What this is: proves the relay server holds no game code. From the build: the sco-core source kit was configured
# without its game pack, the program links only our server library, the kit's network library and the kit's two
# socket files (as CMake declares it, with the libraries each target passes on, and as the program's real link line
# says), and those libraries hold only the objects they should. The real link line is required, not optional: it is
# read from the program's link.txt, which only the Makefile generators write, so a build without that file (Ninja, for
# one) fails this check instead of skipping the part it cannot see. The line is split into words at blanks and at
# commas, so a library inside -Wl,... counts as a library, and an @response file is refused because the words in it
# are not on the line. From the sources: only server/net_boundary.cpp names the kit's network library (includes a
# sco/net header or sc_net.h, or writes sco::net::, even in a comment), and every #include in server/ is one of our
# own headers (server/<name>.h, never a file from a folder below server/, such as server/tests), the shared relay wire
# header, or an approved standard, POSIX or platform header (net_boundary.cpp also the library's). The includes are
# read the way the compiler reads them: a backslash at a line end joins the next line, comments are removed, "%:" is
# "#". So "#/**/include", a directive split over two lines and "%:include" are found, while an include inside a
# comment is not an include. An include that climbs out of its folder with ".." is never admitted, whatever it points
# at. Text the include reader cannot read (a raw string literal, a backslash followed by blanks at a line end, a
# comment that never ends) is refused too. Exit 0 means every check passed.
#
# Only the production set is read: server/*.cpp, server/*.h and cmake/ScoCoreSource.cmake. Not server/tests,
# server/tools or any document.
#
# Usage: sh server/tools/check-no-game-code.sh <repository root> <build facts file>
# The facts file is written by CMake (server/tests/CMakeLists.txt) when it generates the build: where the program
# and the libraries are, what each links and passes on to what links it, the program's link line file, and the
# extracted kit. CTest runs this script as relay_no_game_code.
set -u

root=${1:-}
facts=${2:-}
if [ -z "$root" ] || [ -z "$facts" ] || [ ! -d "$root/server" ]; then
    echo "usage: sh server/tools/check-no-game-code.sh <repository root> <build facts file>" >&2
    exit 2
fi
if [ ! -f "$facts" ]; then
    echo "check-no-game-code: no build facts file $facts. Configure and generate the build first (cmake -S . -B build)." >&2
    exit 2
fi
root=$(cd "$root" && pwd)

fails=0
ok() { echo "ok:   $*"; }
fail() { echo "FAIL: $*" >&2; fails=$((fails + 1)); }

fact() {   # <key>: the value after "key=" in the facts file, empty when the key has none
    sed -n "s/^$1=//p" "$facts" | head -n 1
}

# ---- from the build ---------------------------------------------------------------------------------

cache=$(fact cache)
if [ -f "$cache" ] && grep -q '^SCO_GAME_SC:BOOL=OFF$' "$cache"; then
    ok "the kit was configured with SCO_GAME_SC=OFF"
else
    fail "SCO_GAME_SC is not OFF in the build's CMakeCache.txt ($cache)"
fi
if [ -f "$cache" ] && grep -q '^SCO_BUILD_TESTS:BOOL=OFF$' "$cache"; then
    ok "the kit's own tests are off (SCO_BUILD_TESTS=OFF)"
else
    fail "SCO_BUILD_TESTS is not OFF in the build's CMakeCache.txt ($cache)"
fi

# What each target links, as CMake knows it, and what each passes on to whatever links it (the INTERFACE libraries,
# which reach the program's link line through the target that lists them). Nothing else may end up on that line.
if [ "$(fact program_sources)" = "main.cpp" ] && [ "$(fact program_links)" = "sco_relay_server" ] && [ -z "$(fact program_interface_links)" ] &&
   [ "$(fact server_links)" = "sco_net;sco_relay_sockets" ] && [ "$(fact server_interface_links)" = "sco_net;sco_relay_sockets" ] &&
   [ -z "$(fact net_links)" ] && [ -z "$(fact net_interface_links)" ] &&
   [ -z "$(fact sockets_links)" ] && [ -z "$(fact sockets_interface_links)" ]; then
    ok "CMake declares that the program links only libsco_relay_server, libsco_net and libsco_relay_sockets, and its own object is main.cpp.o"
else
    fail "unexpected program, sources or links: program sources '$(fact program_sources)', program links '$(fact program_links)' (passes on '$(fact program_interface_links)'), server library links '$(fact server_links)' (passes on '$(fact server_interface_links)'), network library links '$(fact net_links)' (passes on '$(fact net_interface_links)'), socket library links '$(fact sockets_links)' (passes on '$(fact sockets_interface_links)')"
fi

# The program's real link line, which the generator must write (Makefile generators: link.txt; Ninja and the Visual
# Studio generators write none, so the check fails there instead of skipping what it cannot see). CMake's own words
# for a library end in .a or .so, or start with -l; the objects end in .o. CMake quotes some words of the line. The
# words are split at any blank and at commas, so -Wl,--whole-archive,/opt/game.a,--no-whole-archive gives the word
# /opt/game.a. A word that starts with @ names a response file, whose words are not on this line: refused.
link_file=$(fact link_file)
if [ -n "$link_file" ] && [ -f "$link_file" ]; then
    words=$(tr -d '"' < "$link_file" | awk '{ n = split($0, w, /[[:space:],]+/); for (i = 1; i <= n; i++) if (w[i] != "") print w[i] }')
    if printf '%s\n' "$words" | grep -q '^@'; then
        fail "the program's link line names an @response file, whose words this check cannot read. The whole link line is: $(cat "$link_file")"
    fi
    libraries=$(printf '%s\n' "$words" | grep -E '\.(a|so)$|^-l' | sed 's|.*/||' | sort -u | tr '\n' ' ')
    objects=$(printf '%s\n' "$words" | grep -E '\.o$' | tr '\n' ' ')
    if [ "$libraries" = "libsco_net.a libsco_relay_server.a libsco_relay_sockets.a " ]; then
        ok "the program's link line names only libsco_relay_server, libsco_net and libsco_relay_sockets"
    else
        fail "unexpected libraries on the link line: $libraries"
    fi
    if [ "$objects" = "CMakeFiles/sco-relay.dir/main.cpp.o " ]; then
        ok "the program's own object on the link line is main.cpp.o and nothing else"
    else
        fail "unexpected objects on the link line: '$objects'. The whole link line is: $(cat "$link_file")"
    fi
else
    fail "the program's link line file is missing (${link_file:-none named}). Only the Makefile generators write link.txt, and the real link line cannot be checked without it: configure the build with the Unix Makefiles generator"
fi

members() {   # <archive>: the objects inside it, sorted, on one line
    if [ ! -f "$1" ]; then
        echo "missing"
    else
        ar t "$1" | sort | tr '\n' ' '
    fi
}

check_members() {   # <archive> <expected objects>
    got=$(members "$1")
    if [ "$got" = "$2 " ]; then
        ok "$(basename "$1") holds only: $2"
    else
        fail "$(basename "$1") holds: $got (expected: $2)"
    fi
}

server_objects=$(for source in "$root"/server/*.cpp; do
    name=$(basename "$source")
    [ "$name" = main.cpp ] || echo "$name.o"
done | sort | tr '\n' ' ')
check_members "$(fact server_library)" "${server_objects% }"
check_members "$(fact net_library)" "core.cpp.o reliable.cpp.o sha2.c.o wire.cpp.o"
check_members "$(fact sockets_library)" "scope.cpp.o udp_posix.cpp.o"

program=$(fact program)
if [ -x "$program" ] && command -v ldd > /dev/null 2>&1; then
    extra=$(ldd "$program" | awk '{print $1}' | grep -Ev '^(linux-vdso\.so\.1|libstdc\+\+\.so\.6|libm\.so\.6|libgcc_s\.so\.1|libc\.so\.6|/lib64/ld-linux-x86-64\.so\.2)$' | tr '\n' ' ')
    if [ -z "$extra" ]; then
        ok "the program needs only the C++ runtime and libc"
    else
        fail "the program needs other libraries: $extra"
    fi
fi

# ---- from the sources -------------------------------------------------------------------------------

# The Windows SDK headers are confined to their guarded use sites: windows.h in main.cpp under _WIN32, and sal.h
# in log.h under _MSC_VER. Every other platform header must be added by name and file here, with a negative-control
# case in test_no_game_code.sh.
is_allowed_system_header() {   # <source basename> <header name>
    case "$2" in
        windows.h) [ "$1" = main.cpp ] && return 0; return 1 ;;
        sal.h) [ "$1" = log.h ] && return 0; return 1 ;;
    esac
    case "$2" in
        algorithm|array|atomic|cerrno|chrono|cmath|csignal|cstdarg|cstddef|cstdint|cstdio|cstdlib|cstring|ctime) return 0 ;;
        functional|initializer_list|iterator|limits|map|memory|optional|set|string|string_view|type_traits|utility|vector) return 0 ;;
        arpa/inet.h|fcntl.h|netinet/in.h|poll.h|signal.h|sys/socket.h|sys/types.h|time.h|unistd.h) return 0 ;;
    esac
    return 1
}

# The production set: the server's sources and the one CMake module that fetches the kit.
production="server/*.cpp server/*.h cmake/ScoCoreSource.cmake"

# Only the boundary file names the network library: not an include, not a type, not a comment.
leaks=$(cd "$root" && grep -lE 'sco/net/|sc_net\.h|sco_net\.h|sco::net::' $production | grep -vxF 'server/net_boundary.cpp')
if [ -z "$leaks" ]; then
    ok "only server/net_boundary.cpp includes a sco-core header or names sco::net::"
else
    fail "these files name sco-core's network library, which only server/net_boundary.cpp may: $(echo $leaks). Move the use into server/net_boundary.cpp (see server/README.md, '## The sco-core boundary')"
fi

# The include reader: lists the include directives of one C++ source file (on standard input), one per line, as the
# header name, or as the whole directive when it cannot be read as a name (which no allowlist admits). It reads the
# file in the compiler's order. A backslash at a line end joins the next line. Each comment becomes one blank (a
# block comment may span lines and then joins them), the digraph %: is a #, and a line whose first word is
# # include (or # import, which GCC also accepts) is a directive. String and character literals are skipped, so a
# /* inside one starts no comment; a quote that follows a number is a digit separator (1_000), not a literal.
# Three things it cannot read are listed as lines of their own, and so refused: a raw string literal (its text is
# not subject to the rules above), a backslash followed by blanks at a line end (compilers differ on whether it
# joins the lines), and a comment that never ends. Plain awk: no extension of any one implementation.
include_scanner='
function emit(t,    rest, name) {
    if (t !~ /^[ \t]*#[ \t]*(include|import)/) return
    rest = t
    sub(/^[ \t]*#[ \t]*(include|import)/, "", rest)
    if (match(rest, /^[ \t]*[<"][^>"]*[>"]/)) {
        name = substr(rest, RSTART, RLENGTH)
        sub(/^[ \t]*[<"]/, "", name)
        sub(/[>"]$/, "", name)
        print name
    } else {
        sub(/^[ \t]+/, "", t)
        sub(/[ \t]+$/, "", t)
        print t
    }
}

function ends_in_number(t,    k) {
    k = length(t)
    while (k > 0 && substr(t, k, 1) ~ /[A-Za-z0-9_.]/) k--
    return substr(t, k + 1) ~ /^[.]?[0-9]/
}

function literal_end(s, i,    n, j, e) {
    n = length(s)
    j = i + 1
    while (j <= n) {
        e = substr(s, j, 1)
        if (e == "\\") { j += 2; continue }
        if (e == substr(s, i, 1)) return j
        j++
    }
    return n
}

function scan(s,    i, n, c, d, rest, p, j) {
    n = length(s)
    i = 1
    while (i <= n) {
        if (in_block) {
            p = index(substr(s, i), "*/")
            if (p == 0) return
            in_block = 0
            cur = cur " "
            i += p + 1
            continue
        }
        rest = substr(s, i)
        if (!match(rest, special)) {
            cur = cur rest
            break
        }
        cur = cur substr(rest, 1, RSTART - 1)
        i += RSTART - 1
        c = substr(s, i, 1)
        d = substr(s, i, 2)
        if (d == "/*") {
            in_block = 1
            i += 2
        } else if (d == "//") {
            break
        } else if (d == "%:") {
            cur = cur "#"
            i += 2
        } else if (c == "\"" || c == q) {
            if (c == q && ends_in_number(cur)) {
                cur = cur c
                i++
            } else {
                if (c == "\"" && substr(cur, length(cur), 1) == "R") print "raw string literal"
                j = literal_end(s, i)
                cur = cur substr(s, i, j - i + 1)
                i = j + 1
            }
        } else {
            cur = cur c
            i++
        }
    }
    if (!in_block) {
        emit(cur)
        cur = ""
    }
}

BEGIN {
    q = sprintf("%c", 39)
    special = "[/\"%" q "]"
}

{
    line = $0
    if (NR == 1 && substr(line, 1, 3) == sprintf("%c%c%c", 239, 187, 191)) line = substr(line, 4)
    sub(/\r$/, "", line)
    if (line ~ /\\[ \t]+$/) print "backslash followed by blanks at a line end"
    if (line ~ /\\$/) {
        pend = pend substr(line, 1, length(line) - 1)
        next
    }
    scan(pend line)
    pend = ""
}

END {
    if (pend != "") scan(pend)
    if (in_block) {
        emit(cur)
        print "comment that never ends"
    }
}
'

list_includes() {   # the include directives of the source file on standard input, one per line
    LC_ALL=C awk "$include_scanner"
}

# The reader must work with this machine's awk: a canary with a comment, a digraph and a split directive. An awk that
# fails to run the program would list nothing, and "nothing" must never read as "no foreign include".
canary=$(printf '%s\n' '/* #include <hidden.h> */' '%:/**/include <canary.h>' '#inc\' 'lude <spliced.h>' | list_includes | tr '\n' ' ')
if [ "$canary" != "canary.h spliced.h " ]; then
    fail "the include reader does not work with this awk (it listed: '$canary')"
fi

# Every include, file by file, so the message can name the file. The library's own headers are allowed in the
# boundary file alone. Outside server/ only the relay wire header, shared with the plugin, is allowed, and inside
# server/ only server/<name>.h (a header in a folder below server/, such as server/tests, is never production). A
# name with ".." in it is refused first: "server/../plugins/multiplayer/motion.h" starts like one of ours and leaves
# server/.
foreign=$(for file in "$root"/server/*.h "$root"/server/*.cpp; do
    list_includes < "$file" |
    sort -u |
    while read -r name; do
        case "$name" in
            *..*) echo "$(basename "$file"): $name"; continue ;;
        esac
        case "$name" in
            sco/net/*|sc_net.h) [ "$(basename "$file")" = net_boundary.cpp ] && continue ;;
            server/*/*) ;;
            server/*.h) [ -f "$root/$name" ] && continue ;;
            plugins/multiplayer/relay_wire.h) [ -f "$root/$name" ] && continue ;;
        esac
        is_allowed_system_header "$(basename "$file")" "$name" && continue
        echo "$(basename "$file"): $name"
    done
done)
if [ -z "$foreign" ]; then
    ok "every #include in server/ is our own server/<name>.h, plugins/multiplayer/relay_wire.h, an approved system header (and the library's, in net_boundary.cpp only)"
else
    fail "server/ includes headers it may not, or has text the include reader cannot read: $(echo $foreign)"
fi

if (cd "$root" && grep -Eq 'external/sc-offline|multiplayer_wire' $production); then
    fail "the server's sources or its kit module refer to the plugin's own wire header (multiplayer_wire) or to an sc-offline checkout"
else
    ok "the server's sources and its kit module do not refer to the plugin's own wire header or to an sc-offline checkout"
fi

if (cd "$root" && grep -Eq '^set\(SCO_GAME_SC OFF CACHE BOOL' cmake/ScoCoreSource.cmake); then
    ok "cmake/ScoCoreSource.cmake sets SCO_GAME_SC to OFF"
else
    fail "cmake/ScoCoreSource.cmake does not set SCO_GAME_SC to OFF"
fi

if [ "$fails" -ne 0 ]; then
    echo "check-no-game-code: $fails check(s) failed" >&2
    exit 1
fi
echo "check-no-game-code: all checks passed"
