#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What this is: proves the relay server holds no game code. From the build: the sco-core source kit was configured
# without its game pack, the program links only our server library, the kit's network library and the kit's two
# socket files (as CMake declares it, with the libraries each target passes on, and as the program's real link line
# says), and those libraries hold only the objects they should. The real link line is required, not optional: it is
# read from the program's link.txt, which only the Makefile generators write, so a build without that file (Ninja, for
# one) fails this check instead of skipping the part it cannot see. The shell that runs link.txt drops quotes and
# backslash escapes, so they are dropped here first; then the line is split into words at blanks and at commas, so a
# library inside -Wl,... counts as a library, and an @response file is refused because the words in it are not on the
# line. Linker scripts and unrecognized link options or positional linker inputs are refused too. From the sources: only
# server/net_boundary.cpp names the kit's network library (includes a sco/net header or sc_net.h, or writes
# sco::net::, even in a comment), and every #include in the server and shared relay wire header is a same-directory
# server header, the shared wire header at its fixed relative path, or an approved standard, POSIX or platform header
# (net_boundary.cpp also the library's headers from the extracted kit). An approved system header must not resolve to
# a same-named file in an actual source or project include directory. The includes are read twice and
# either reading may refuse. The plain reading takes any line, or line joined across backslash-newlines, that starts
# with # (or the digraph %:) and the word include (or import), with any blanks between, form feed and vertical tab
# too. The C++ reading adds what the plain one cannot see: it removes comments, so "#/**/include" and "/* x */
# #include" are found, and it skips string and character literals. So an include inside a one-line comment or a
# string is not an include, while one on a line of its own inside a multi-line comment is still refused. An include
# that climbs out of its folder with ".." is never admitted, whatever it points at. Text the C++ reading cannot read
# (a raw string literal, a backslash followed by blanks at a line end, a comment that never ends) is refused too.
# Exit 0 means every check passed.
#
# Only the production set is read: server/*.cpp, server/*.h, plugins/multiplayer/relay_wire.h and
# cmake/ScoCoreSource.cmake. Not server/tests, server/tools or any document.
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
   [ -n "$(fact server_sources)" ] &&
   [ "$(fact server_links)" = "sco_net;sco_relay_sockets" ] && [ "$(fact server_interface_links)" = "sco_net;sco_relay_sockets" ] &&
   [ -z "$(fact net_links)" ] && [ -z "$(fact net_interface_links)" ] &&
   [ -z "$(fact sockets_links)" ] && [ -z "$(fact sockets_interface_links)" ]; then
    ok "CMake declares that the program links only libsco_relay_server, libsco_net and libsco_relay_sockets, and its own object is main.cpp.o"
else
    fail "unexpected program, sources or links: program sources '$(fact program_sources)', server sources '$(fact server_sources)', program links '$(fact program_links)' (passes on '$(fact program_interface_links)'), server library links '$(fact server_links)' (passes on '$(fact server_interface_links)'), network library links '$(fact net_links)' (passes on '$(fact net_interface_links)'), socket library links '$(fact sockets_links)' (passes on '$(fact sockets_interface_links)')"
fi

# The executable and server may use only the pinned SDK include directory in their effective compiler include path.
# Any other -I path, including one outside the repository, can shadow direct or transitive system headers. The test
# target has its own include path but is not part of the product.
production_include_dirs=$(fact production_include_dirs)
kit_include=$(fact kit_include)
if ! grep -q '^production_include_dirs=' "$facts"; then
    fail "build facts do not record the product compiler include paths"
elif [ -z "$kit_include" ] || [ ! -d "$kit_include" ]; then
    fail "build facts do not name an existing pinned kit include directory"
else
    include_dir_bad=""
    kit_include_seen=0
    old_ifs=$IFS
    IFS=';'
    for include_dir in $production_include_dirs; do
        [ -n "$include_dir" ] || continue
        if [ "$include_dir" = "$kit_include" ]; then
            kit_include_seen=1
        else
            include_dir_bad="$include_dir_bad $include_dir"
        fi
    done
    IFS=$old_ifs
    if [ -z "$include_dir_bad" ] && [ "$kit_include_seen" -eq 1 ]; then
        ok "the only explicit product include path is the pinned kit directory"
    elif [ -z "$include_dir_bad" ]; then
        fail "the product compiler include paths omit the pinned kit directory"
    else
        fail "product compiler include paths include directories outside the pinned kit ($include_dir_bad)"
    fi
fi

compile_commands=$(fact compile_commands)
if [ -z "$compile_commands" ] || [ ! -f "$compile_commands" ]; then
    fail "the generated CMake compile command database is missing ($compile_commands)"
elif command -v python3 > /dev/null 2>&1; then
    if ! python3 "$root/server/tools/check-production-compile-inputs.py" "$root" "$facts"; then
        fail "the generated product compile commands contain untrusted or uninspected inputs"
    fi
else
    fail "python3 is required to inspect the generated production compile commands"
fi

expected_server_sources=$(for source in "$root"/server/*.cpp; do
    [ "$(basename "$source")" = main.cpp ] || echo "$source"
done | sort | tr '\n' ';' | sed 's/;$//')
actual_server_sources=$(printf '%s\n' "$(fact server_sources)" | tr ';' '\n' | sort | tr '\n' ';' | sed 's/;$//')
if [ -n "$expected_server_sources" ] && [ "$actual_server_sources" = "$expected_server_sources" ]; then
    ok "CMake compiles exactly the reviewed server/*.cpp sources into libsco_relay_server"
else
    fail "CMake server sources differ from server/*.cpp (actual: '$(fact server_sources)'; expected: '$expected_server_sources')"
fi

# The program's real link line, which the generator must write (Makefile generators: link.txt; Ninja and the Visual
# Studio generators write none, so the check fails there instead of skipping what it cannot see). CMake's own words
# for a library end in .a or .so, or start with -l; the objects end in .o. CMake quotes some words of the line, and
# the shell drops double and single quotes and backslashes (octal 042, 047 and 134) before the linker sees a word, so
# they are dropped first: '/opt/game.a' is the word /opt/game.a. The words are split at any blank and at commas, so
# -Wl,--whole-archive,/opt/game.a,--no-whole-archive gives the word /opt/game.a. A word that starts with @ names a
# response file, whose words are not on this line: refused. Linker scripts and plugin/remap inputs, raw .obj inputs,
# unsafe forwarding through -Xlinker, and other positional inputs whose contents this check cannot inspect are refused.
link_file=$(fact link_file)
if [ -n "$link_file" ] && [ -f "$link_file" ]; then
    words=$(tr -d '\042\047\134' < "$link_file" | awk '{ n = split($0, w, /[[:space:],]+/); for (i = 1; i <= n; i++) if (w[i] != "") print w[i] }')
    if printf '%s\n' "$words" | grep -q '^@'; then
        fail "the program's link line names an @response file, whose words this check cannot read. The whole link line is: $(cat "$link_file")"
    fi
    linker_scripts=$(printf '%s\n' "$words" |
        grep -E '^-T($|.+)|^--(script|default-script|mri-script|error-handling-script|version-script)(=|$)|^-dT.*|^-c.*' |
        grep -Ev '^-T(text|data|bss|text-segment|rodata-segment|ldata-segment)(=.*)?$' | tr '\n' ' ')
    if [ -n "$linker_scripts" ]; then
        fail "the program's link line names a linker script option ($linker_scripts), whose contents this check cannot inspect. The whole link line is: $(cat "$link_file")"
    fi
    unsafe_linker_inputs=$(printf '%s\n' "$words" | grep -E '^-plugin(=|$)|^--plugin(=|$)|^--remap-inputs-file(=|$)' | tr '\n' ' ')
    if [ -n "$unsafe_linker_inputs" ]; then
        fail "the program's link line names an unsafe linker plugin or remap input option ($unsafe_linker_inputs). The whole link line is: $(cat "$link_file")"
    fi
    libraries=$(printf '%s\n' "$words" | grep -E '\.(a|so)$|^-l' | sed 's|.*/||' | sort -u | tr '\n' ' ')
    objects=$(printf '%s\n' "$words" | grep -Ei '\.(o|obj)$' | tr '\n' ' ')
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

    # Match archives to the exact files CMake generated, not just their basenames. Sorting unique basenames would let
    # an archive at another path impersonate a permitted library, or let a duplicate input disappear from the check.
    link_cwd=$(dirname "$(fact program)")
    server_library=$(fact server_library)
    net_library=$(fact net_library)
    sockets_library=$(fact sockets_library)
    server_count=0
    net_count=0
    sockets_count=0
    other_archives=""
    for archive in $(printf '%s\n' "$words" | grep -E '\.(a|so)$'); do
        case "$archive" in
            /*) resolved="$archive" ;;
            *) resolved="$link_cwd/$archive" ;;
        esac
        if [ -e "$resolved" ] && [ -n "$server_library" ] && [ "$resolved" -ef "$server_library" ]; then
            server_count=$((server_count + 1))
        elif [ -e "$resolved" ] && [ -n "$net_library" ] && [ "$resolved" -ef "$net_library" ]; then
            net_count=$((net_count + 1))
        elif [ -e "$resolved" ] && [ -n "$sockets_library" ] && [ "$resolved" -ef "$sockets_library" ]; then
            sockets_count=$((sockets_count + 1))
        else
            other_archives="$other_archives $archive"
        fi
    done
    if [ -z "$other_archives" ] && [ "$server_count" -eq 1 ] && [ "$net_count" -eq 1 ] && [ "$sockets_count" -eq 1 ]; then
        ok "each expected archive appears once at its CMake-generated path"
    else
        fail "unexpected, substituted or duplicate archive inputs: '$other_archives' (expected one each from CMake facts). The whole link line is: $(cat "$link_file")"
    fi

    # A positional filename can be an ld script regardless of its extension, so checking .a/.so/.o/.obj alone is
    # insufficient. Keep the command, -o output and expected inputs; reject any other bare word. Inspect the contents
    # of -Wl,... and -Xlinker forwarding too: an unrecognized linker option or bare linker argument could load code.
    unknown_inputs=$(tr -d '\042\047\134' < "$link_file" | awk '
        function takes_value(x) {
            return x == "-z" || x == "-Map" || x == "--Map" ||
                   x == "--dependency-file" || x == "-u" || x == "--undefined" ||
                   x == "-e" || x == "--entry" || x == "--defsym" ||
                   x == "-Ttext" || x == "-Tdata" || x == "-Tbss" || x == "-Ttext-segment" ||
                   x == "-Trodata-segment" || x == "-Tldata-segment"
        }
        function safe_z_value(x) {
            return x == "relro" || x == "now" || x == "lazy" || x == "defs" || x == "nodefs" ||
                   x == "execstack" || x == "noexecstack" || x ~ /^(common-page-size|max-page-size)=/
        }
        function safe_linker_option(x) {
            return x == "--as-needed" || x == "--no-as-needed" || x == "--whole-archive" ||
                   x == "--no-whole-archive" || x == "--start-group" || x == "--end-group" ||
                   x == "--no-undefined" || x == "--eh-frame-hdr" || x == "--gc-sections" ||
                   x == "--no-keep-memory" || x == "--no-demangle" || x == "--warn-common" ||
                   x == "--no-warn-common" || x == "--fatal-warnings" || x == "--no-fatal-warnings" ||
                   x == "--build-id" || x ~ /^--build-id=/ || x ~ /^--dependency-file=/ ||
                   x ~ /^(-Map|--Map)=/ ||
                   x ~ /^-l.+$/ || x ~ /^-B(static|dynamic|symbolic|symbolic-functions)$/ ||
                   x == "-s" || x == "-S" || x ~ /^-O[0-9]+$/ ||
                   x ~ /^-T(text|data|bss|text-segment|rodata-segment|ldata-segment)(=.*)?$/
        }
        function safe_driver_option(x) {
            return x == "-pthread" || x == "-rdynamic" || x == "-pie" || x == "-no-pie" ||
                   x == "--no-default-config" ||
                   x == "-static" || x == "-static-libgcc" || x == "-static-libstdc++" ||
                   x ~ /^-O([0-3]|s|z|fast)?$/ || x ~ /^-g([0-3]|gdb)?$/ ||
                   x == "-DNDEBUG" || x ~ /^-D[^=]+(=.*)?$/ || x ~ /^-U[^=]+$/ ||
                   x ~ /^-fuse-ld=(bfd|gold|lld|mold)$/ || x ~ /^-m(32|64)$/ ||
                   x ~ /^-T(text|data|bss|text-segment|rodata-segment|ldata-segment)(=.*)?$/
        }
        {
            for (i = 1; i <= NF; i++) {
                w = $i
                if (first) { first = 0; continue }
                if (output_next) { output_next = 0; continue }
                if (w == "-o") { output_next = 1; continue }
                if (w == "-Xlinker") {
                    if (i == NF) { print w; continue }
                    x = $(++i)
                    if (x !~ /^-/ && !xlinker_value) print x
                    if (xlinker_value == "z") {
                        if (!safe_z_value(x)) print x
                        xlinker_value = ""
                    } else if (xlinker_value == "value") {
                        xlinker_value = ""
                    } else if (takes_value(x)) {
                        xlinker_value = x == "-z" ? "z" : "value"
                    } else if (x !~ /^-/ || !safe_linker_option(x)) {
                        print x
                        xlinker_value = ""
                    } else {
                        xlinker_value = ""
                    }
                    continue
                }
                if (w ~ /^-Xlinker/) { print w; continue }
                if (w ~ /^-Wl,/) {
                    n = split(substr(w, 5), a, ",")
                    for (j = 1; j <= n; j++) {
                        x = a[j]
                        if (wl_value == "z") {
                            if (!safe_z_value(x)) print x
                            wl_value = ""
                        } else if (wl_value == "value") {
                            wl_value = ""
                        } else if (takes_value(x)) {
                            wl_value = x == "-z" ? "z" : "value"
                        } else if (x ~ /^-/) {
                            if (!safe_linker_option(x)) print x
                        } else if (x !~ /\.(a|so|o|obj)$/) {
                            print x
                        }
                    }
                    continue
                }
                if (w ~ /^-/) {
                    if (!safe_driver_option(w)) print w
                    continue
                }
                if (w ~ /^CMakeFiles\/sco-relay\.dir\/main\.cpp\.o$/) continue
                if (w ~ /\.(o|obj)$/) { print w; continue }
                # Archive words have already been resolved against the CMake facts above. Their path spelling can vary
                # by generator (for example ../sco-core-source/libsco_net.a), so do not require a basename here.
                if (w ~ /\.(a|so)$/) continue
                print w
            }
        }
        BEGIN { first = 1 }
        END {
            if (output_next) print "-o without output path"
            if (search_next) print "-L without directory"
            if (wl_value) print "-Wl option without its value"
            if (xlinker_value) print "-Xlinker option without its value"
        }
    ' | tr '\n' ' ')
    if [ -n "$unknown_inputs" ]; then
        fail "the program's link line has unrecognized link option or positional input(s) ($unknown_inputs), whose effects this check cannot inspect. The whole link line is: $(cat "$link_file")"
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

# The production set: the server's sources, the shared wire header, and the one CMake module that fetches the kit.
production="server/*.cpp server/*.h plugins/multiplayer/relay_wire.h cmake/ScoCoreSource.cmake"

# Only the boundary file names the network library: not an include, not a type, not a comment.
leaks=$(cd "$root" && grep -lE 'sco/net/|sc_net\.h|sco_net\.h|sco::net::' $production | grep -vxF 'server/net_boundary.cpp')
if [ -z "$leaks" ]; then
    ok "only server/net_boundary.cpp includes a sco-core header or names sco::net::"
else
    fail "these files name sco-core's network library, which only server/net_boundary.cpp may: $(echo $leaks). Move the use into server/net_boundary.cpp (see server/README.md, '## The sco-core boundary')"
fi

# The include reader: lists the include directives of one C++ source file (on standard input), one per line, as the
# header name, or as the whole directive when it cannot be read as a name (which no allowlist admits). It reads the
# file twice and lists what either reading finds, so the second reading can only add refusals to the first.
#   The plain reading knows nothing about C++, so no comment and no literal can fool it. It takes every physical line
#   as written, and every line joined across backslash-newlines, whose first word is # (or the digraph %:) and then
#   include (or import, which GCC also accepts), with any blanks in between: space, tab, form feed, vertical tab.
#   `include_next` is unrecognized and refused as an unknown directive. A
#   lone carriage return ends a line, as it does for GCC and Clang.
#   The C++ reading finds what the plain one cannot. It removes comments (each becomes one blank; a block comment
#   may span lines and then joins them), so #/**/include and "/* note */ #include" are directives. It skips string
#   and character literals, so a /* inside one starts no comment. A quote that follows a number and comes before a
#   letter or digit is a digit separator (1'000), not a literal, as in the C++14 grammar. A number never contains a
#   literal, so the look back at the number stops at the end of the last literal ('1''a/*' is two literals).
#   NUL bytes are dropped before either reading, because GCC ignores them.
# So an include inside a one-line comment or a string is not an include, but one on a line of its own inside a
# multi-line comment is refused (the plain reading sees it): the gate fails closed. Three things the C++ reading
# cannot read are listed as lines of their own, and so refused: a raw string literal, a backslash followed by blanks
# at a line end (compilers differ on whether it joins the lines), and a comment that never ends. Plain awk: no
# extension of any one implementation.
include_scanner='
function emit(t,    rest, name, kind) {
    if (t !~ /^[[:space:]]*(#|%:)[[:space:]]*(include|import)/) return
    rest = t
    sub(/^[[:space:]]*(#|%:)[[:space:]]*(include|import)/, "", rest)
    if (match(rest, /^[[:space:]]*[<"][^>"]*[>"]/)) {
        name = substr(rest, RSTART, RLENGTH)
        kind = substr(name, match(name, /[<"]/) , 1) == "<" ? "angle:" : "quote:"
        sub(/^[[:space:]]*[<"]/, "", name)
        sub(/[>"]$/, "", name)
        print kind name
    } else {
        sub(/^[[:space:]]+/, "", t)
        sub(/[[:space:]]+$/, "", t)
        print "unknown:" t
    }
}

function ends_in_number(t,    k, c, r, i, n) {
    k = length(t)
    while (k > lit_end) {
        c = substr(t, k, 1)
        if (c ~ /[A-Za-z0-9_.]/ || c == q) { k--; continue }
        if ((c == "+" || c == "-") && substr(t, k - 1, 1) ~ /[eEpP]/) { k--; continue }
        break
    }
    r = substr(t, k + 1)
    n = length(r)
    i = 1
    while (i <= n) {
        c = substr(r, i, 1)
        if (c ~ /[0-9]/ || (c == "." && substr(r, i + 1, 1) ~ /[0-9]/)) return 1
        if (c ~ /[A-Za-z_]/) {
            while (i <= n && substr(r, i, 1) ~ /[A-Za-z0-9_]/) i++
        } else {
            i++
        }
    }
    return 0
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
            if (c == q && substr(s, i + 1, 1) ~ /[A-Za-z0-9_]/ && ends_in_number(cur)) {
                cur = cur c
                i++
            } else {
                if (c == "\"" && substr(cur, length(cur), 1) == "R") print "raw string literal"
                j = literal_end(s, i)
                cur = cur substr(s, i, j - i + 1)
                lit_end = length(cur)
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
        lit_end = 0
    }
}

function feed(line,    logical) {
    if (line ~ /\\[[:space:]]+$/) print "backslash followed by blanks at a line end"
    emit(line)
    if (line ~ /\\$/) {
        pend = pend substr(line, 1, length(line) - 1)
        return
    }
    logical = pend line
    if (pend != "") emit(logical)
    pend = ""
    scan(logical)
}

BEGIN {
    q = sprintf("%c", 39)
    special = "[/\"%" q "]"
    lit_end = 0
}

{
    line = $0
    if (NR == 1 && substr(line, 1, 3) == sprintf("%c%c%c", 239, 187, 191)) line = substr(line, 4)
    sub(/\r$/, "", line)
    cn = split(line, parts, "\r")
    if (cn == 0) {
        feed("")
        next
    }
    for (ck = 1; ck <= cn; ck++) feed(parts[ck])
}

END {
    if (pend != "") {
        emit(pend)
        scan(pend)
    }
    if (in_block) {
        emit(cur)
        print "comment that never ends"
    }
}
'

list_includes() {   # the include directives of the source file on standard input, one per line
    tr -d '\000' | LC_ALL=C awk "$include_scanner"
}

# The reader must work with this machine's awk: a canary with a comment, a digraph and a split directive. An awk that
# fails to run the program would list nothing, and "nothing" must never read as "no foreign include".
canary=$(printf '%s\n' '/* #include <hidden.h> */' '%:/**/include <canary.h>' '#inc\' 'lude <spliced.h>' | list_includes | sort -u | tr '\n' ' ')
if [ "$canary" != "angle:canary.h angle:spliced.h " ]; then
    fail "the include reader does not work with this awk (it listed: '$canary')"
fi

# Every include, file by file, so the message can name the file. The library's own headers are allowed in the
# boundary file alone and must come from the exact extracted kit. Outside server/ only the relay wire header, shared
# with the plugin, is allowed, and inside server/ only a same-directory header is allowed (a header in a folder below
# server/, such as server/tests, is never production). Production targets have no repository-root include path.
foreign=$(for file in "$root"/server/*.h "$root"/server/*.cpp "$root"/plugins/multiplayer/relay_wire.h; do
    list_includes < "$file" |
    sort -u |
    while IFS= read -r include; do
        case "$include" in
            angle:*) kind=angle; name=${include#angle:} ;;
            quote:*) kind=quote; name=${include#quote:} ;;
            unknown:*) kind=unknown; name=${include#unknown:} ;;
            *) kind=unknown; name=$include ;;
        esac
        case "$name" in
            sco/net/*|sc_net.h)
                if [ "$(basename "$file")" != net_boundary.cpp ]; then
                    echo "$(basename "$file"): $name"
                    continue
                fi
                shadow=""
                if [ "$kind" = quote ] && [ -f "$(dirname "$file")/$name" ]; then
                    shadow="$(dirname "$file")/$name"
                fi
                if [ -n "$shadow" ]; then
                    echo "$(basename "$file"): $name is shadowed by $shadow"
                elif [ -n "$(fact kit)" ] && [ -f "$(fact kit)/include/$name" ]; then
                    continue
                else
                    echo "$(basename "$file"): $name is missing from the pinned kit include directory"
                fi
                continue
                ;;
            ../plugins/multiplayer/relay_wire.h)
                if [ "$kind" = quote ] && [ "$(dirname "$file")" = "$root/server" ] && [ -f "$root/plugins/multiplayer/relay_wire.h" ]; then
                    continue
                fi
                ;;
            *..*) echo "$(basename "$file"): $name"; continue ;;
        esac
        if is_allowed_system_header "$(basename "$file")" "$name"; then
            # Quoted includes search the source/header's own directory first. Product include dirs are recorded and
            # checked above; none may point back into this repository. The kit include path is pinned separately.
            shadow=""
            if [ "$kind" = quote ] && [ -f "$(dirname "$file")/$name" ]; then
                shadow="$(dirname "$file")/$name"
            elif [ -n "$(fact kit)" ] && [ -f "$(fact kit)/include/$name" ]; then
                shadow="$(fact kit)/include/$name"
            fi
            if [ -n "$shadow" ]; then
                echo "$(basename "$file"): $name is shadowed by $shadow"
            else
                continue
            fi
        fi
        if [ "$file" = "$root/plugins/multiplayer/relay_wire.h" ] && [ "$kind" = quote ] &&
           [ "$name" = "$(basename "$name")" ] && [ -f "$(dirname "$file")/$name" ]; then
            echo "relay_wire.h: sibling plugin header $name is not scanned by the relay boundary"
            continue
        fi
        case "$name" in
            *.h) [ "$kind" = quote ] && [ "$name" = "$(basename "$name")" ] && [ -f "$(dirname "$file")/$name" ] && continue ;;
        esac
        echo "$(basename "$file"): $name"
    done
done)
if [ -z "$foreign" ]; then
    ok "every production #include is a project header, plugins/multiplayer/relay_wire.h, an unshadowed approved system header (and the library's, in net_boundary.cpp only)"
else
    fail "production sources include headers they may not, resolve an approved system header to a project file, or have text the include reader cannot read: $(echo $foreign)"
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
