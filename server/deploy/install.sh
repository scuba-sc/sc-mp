#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What this is: the one-command installer for the sco-relay server on a plain Linux machine. Run it as root from your own
# copy of this repository:
#
#     sudo sh server/deploy/install.sh
#
# It builds only the relay from this tree, installs it as /usr/local/bin/sco-relay with an account of its own and one
# protected config file, and runs it: as a systemd service that starts at boot, or, where systemd is not running, in the
# foreground as that account (the command does not return; Ctrl+C or SIGTERM stops the relay, which exits 0). Run again,
# it upgrades the program and keeps the config, and with it the passphrase your players already hold.
#
# Root runs cmake and the compiler on this tree, so the script works on a copy of its own and never on the tree you ran
# it from: it copies the tree (without .git and build folders) into a new folder under /var/tmp, as root, keeping owners
# and modes; checks that copy; and configures and builds only the copy. The folder is new every run (never one that was
# there before), owned by root and closed to everyone else, and removed when the script ends. The check is a safeguard
# against common mistakes, such as a clone with loose permissions or a stray symlink. It is not a defence against someone
# who already controls your copy: root builds and runs what is in it, so run it only from a copy you trust. What it
# checks is said where it happens below, and in server/deploy/README.md.
#
# Network use: the package manager, only when a build tool is missing and only on Debian and Ubuntu; and the build's
# one download, the pinned SDK files that cmake/ScoSdk.cmake and cmake/ScoCoreSource.cmake fetch and check against a
# SHA-256 kept in this repository. The relay itself opens one UDP port and nothing else. The firewall is not touched.
# server/deploy/README.md describes every step and what to open for your players.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
account=sco-relay
program=/usr/local/bin/sco-relay
config_dir=/etc/sco-relay
config=$config_dir/sco-relay.conf
unit_target=/etc/systemd/system/sco-relay.service
lock_dir=/run/sco-relay-installer.lock

say() { echo "install: $*"; }
die() { echo "install: $*" >&2; exit 1; }

# What this script makes and must not leave behind: the half-made config and the build folder (which holds the copy).
build_dir=""
temporary=""
lock_held=""
cleanup() {
    [ -z "$temporary" ] || rm -f "$temporary"
    [ -z "$build_dir" ] || rm -rf "$build_dir"
    if [ -n "$lock_held" ]; then
        rmdir "$lock_dir" || echo "install: cannot remove installation lock $lock_dir; inspect it before retrying" >&2
        lock_held=""
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

# ---- is this the right place to run it ---------------------------------------------------------------------------

[ "$(uname -s)" = Linux ] || die "this installer is for Linux. On Windows, build the relay and start it by hand (server/README.md, 'Run')."
[ "$(id -u)" -eq 0 ] || die "run it as root: sudo sh server/deploy/install.sh"
if [ ! -f "$root/CMakeLists.txt" ] || [ ! -f "$root/server/main.cpp" ] || [ ! -f "$root/server/sco-relay.example.conf" ] || [ ! -f "$here/sco-relay.service" ]; then
    die "run it from a copy of the sc-mp repository (a clone or an unpacked archive): sudo sh server/deploy/install.sh"
fi
command -v tar > /dev/null 2>&1 || die "tar is needed to copy the source tree"

# ---- identity preflight: read only, before even making the build folder ------------------------------------------
# Never adopt or repair an unrelated account. NSS enumeration is required to detect shared primary groups and
# numeric aliases; an unavailable/non-enumerable database fails closed. Recheck after the build, before installation.
validate_identity() {
    identity_error="unsafe existing $account identity; use a dedicated locked system account and private group. Installation stopped."
    # Permit files, optionally followed by systemd in its dynamic-user-only mode.
    # Remote NSS and userdb providers may hide records from enumeration. DynamicUser
    # identities occupy 61184..65519, outside the system ranges accepted below.
    awk '
        { sub(/#.*/, "") }
        /^[[:space:]]*(passwd|group|shadow|gshadow|initgroups)[[:space:]]*:/ {
            name = $0; sub(/:.*/, "", name); gsub(/[[:space:]]/, "", name)
            source = $0; sub(/^[^:]*:[[:space:]]*/, "", source); sub(/[[:space:]]*$/, "", source)
            gsub(/[[:space:]]+/, " ", source)
            if (source != "files" && source != "files systemd") bad = 1
            if (seen[name]++) bad = 1
        }
        END { exit (bad || !seen["passwd"] || !seen["group"] || !seen["shadow"] || !seen["gshadow"]) ? 1 : 0 }
    ' /etc/nsswitch.conf || die "$identity_error (supported NSS policy: files, optionally files systemd)"
    if awk '/^[[:space:]]*(passwd|group|shadow|gshadow|initgroups)[[:space:]]*:/ && /systemd/ { found=1 } END { exit !found }' /etc/nsswitch.conf; then
        # Newer nss-systemd can route arbitrary Varlink/static userdb providers.
        # Only PID 1 DynamicUser is supported, not homed, machined or userdbd.
        for record in /etc/userdb/* /run/userdb/* /run/host/userdb/* /usr/lib/userdb/* /run/systemd/userdb/*; do
            [ -e "$record" ] || [ -L "$record" ] || continue
            case $record in /run/systemd/userdb/io.systemd.DynamicUser) ;;
                *) die "$identity_error (external systemd userdb provider or record: $record)" ;;
            esac
        done
    fi
    ranges=$(awk '
        /^[[:space:]]*#/ || NF == 0 { next }
        $1 ~ /^(SYS_UID_MIN|SYS_UID_MAX|SYS_GID_MIN|SYS_GID_MAX|UID_MIN|GID_MIN)$/ {
            if (NF != 2 || $2 !~ /^[1-9][0-9]*$/ || length($2) > 10 || seen[$1]++) bad=1
            v[$1]=$2+0
        }
        END {
            umin=("SYS_UID_MIN" in v ? v["SYS_UID_MIN"] : 101)
            umax=("SYS_UID_MAX" in v ? v["SYS_UID_MAX"] : ("UID_MIN" in v ? v["UID_MIN"] : 1000)-1)
            gmin=("SYS_GID_MIN" in v ? v["SYS_GID_MIN"] : 101)
            gmax=("SYS_GID_MAX" in v ? v["SYS_GID_MAX"] : ("GID_MIN" in v ? v["GID_MIN"] : 1000)-1)
            if (bad || umin>umax || gmin>gmax || umax>=61184 || gmax>=61184) exit 1
            print umin, umax, gmin, gmax
        }' /etc/login.defs) || die "$identity_error (unsupported system ID ranges in login.defs)"
    # The validated range string is four decimal words, never shell code.
    # shellcheck disable=SC2086
    set -- $ranges
    uid_min=$1 uid_max=$2 gid_min=$3 gid_max=$4
    passwd_rows=$(getent passwd) || die "$identity_error (cannot enumerate passwd)"
    group_rows=$(getent group) || die "$identity_error (cannot enumerate group)"
    # Keyed NSS lookups can return identities omitted by enumeration (for example LDAP).
    # Refuse inconsistent views rather than mistaking a hidden identity for a free name.
    for database in passwd group; do
        case $database in passwd) rows=$passwd_rows ;; group) rows=$group_rows ;; esac
        listed=$(printf '%s\n' "$rows" | awk -F: '$1 == "sco-relay"')
        if keyed=$(getent "$database" "$account"); then
            [ -n "$keyed" ] && [ "$keyed" = "$listed" ] || die "$identity_error (inconsistent $database lookup)"
        else
            lookup_status=$?
            [ "$lookup_status" -eq 2 ] && [ -z "$listed" ] && [ -z "$keyed" ] \
                || die "$identity_error (failed $database lookup)"
        fi
    done
    printf '%s\n' PASSWD "$passwd_rows" GROUP "$group_rows" | awk -F: -v umin="$uid_min" -v umax="$uid_max" -v gmin="$gid_min" -v gmax="$gid_max" '
        $0 == "PASSWD" { table = "p"; next }
        $0 == "GROUP" { table = "g"; next }
        table == "p" {
            if (NF != 7) bad = 1
            pn[++np] = $1; pu[np] = $3; pg[np] = $4
            if ($1 == "sco-relay") {
                users++; uid = $3; primary = $4
                if ($2 != "x" || $3 !~ /^[1-9][0-9]*$/ || length($3) > 10 || $3+0 < umin || $3+0 > umax || $6 != "/nonexistent" ||
                    $7 !~ /^\/(usr\/)?(sbin\/nologin|bin\/false)$/) bad = 1
            }
        }
        table == "g" {
            if (NF != 4) bad = 1
            gn[++ng] = $1; gg[ng] = $3
            if ($1 == "sco-relay") {
                groups++; gid = $3
                if ($3 !~ /^[1-9][0-9]*$/ || length($3) > 10 || $3+0 < gmin || $3+0 > gmax ||
                    ($4 != "" && $4 != "sco-relay")) bad = 1
            }
        }
        END {
            if (users > 1 || groups > 1 || (users && (!groups || primary != gid))) bad = 1
            for (i = 1; i <= np; i++) {
                if (users && pu[i] + 0 == uid + 0 && pn[i] != "sco-relay") bad = 1
                if (groups && pg[i] + 0 == gid + 0 && pn[i] != "sco-relay") bad = 1
            }
            for (i = 1; i <= ng; i++)
                if (groups && gg[i] + 0 == gid + 0 && gn[i] != "sco-relay") bad = 1
            exit bad ? 1 : 0
        }' || die "$identity_error"
    # Name and numeric resolution must agree with the complete local records.
    # This also refuses a systemd identity using our name when no local user exists.
    for database in passwd group; do
        case $database in passwd) rows=$passwd_rows ;; group) rows=$group_rows ;; esac
        listed=$(printf '%s\n' "$rows" | awk -F: '$1 == "sco-relay"')
        local_row=$(getent -s files "$database" "$account" || [ "$?" -eq 2 ]) || die "$identity_error (local lookup failed)"
        [ "$listed" = "$local_row" ] || die "$identity_error (identity is not local)"
        if [ -n "$listed" ]; then
            number=$(printf '%s\n' "$listed" | cut -d: -f3)
            numeric=$(getent "$database" "$number") || die "$identity_error (numeric lookup failed)"
            [ "$numeric" = "$listed" ] || die "$identity_error (inconsistent numeric lookup)"
        fi
    done
    if printf '%s\n' "$passwd_rows" | awk -F: '$1 == "sco-relay" { found = 1 } END { exit !found }'; then
        shadow_row=$(getent shadow "$account") || die "$identity_error (cannot read shadow)"
        local_shadow=$(getent -s files shadow "$account") || die "$identity_error (cannot read local shadow)"
        [ "$shadow_row" = "$local_shadow" ] || die "$identity_error (inconsistent local shadow)"
        printf '%s\n' "$shadow_row" | awk -F: '
            NF != 9 || $1 != "sco-relay" || $2 !~ /^[!*]/ { bad = 1 }
            END { exit (NR != 1 || bad) ? 1 : 0 }' || die "$identity_error (password is not locked)"
        identity_groups=$(id -G "$account") || die "$identity_error (cannot read supplementary groups)"
        identity_gid=$(printf '%s\n' "$group_rows" | awk -F: '$1 == "sco-relay" { print $3 }')
        [ "$identity_groups" = "$identity_gid" ] || die "$identity_error (supplementary groups)"
    fi
    if printf '%s\n' "$group_rows" | awk -F: '$1 == "sco-relay" { found = 1 } END { exit !found }'; then
        gshadow_row=$(getent gshadow "$account") || die "$identity_error (cannot read gshadow)"
        local_gshadow=$(getent -s files gshadow "$account") || die "$identity_error (cannot read local gshadow)"
        [ "$gshadow_row" = "$local_gshadow" ] || die "$identity_error (inconsistent local gshadow)"
        printf '%s\n' "$gshadow_row" | awk -F: '
            NF != 4 || $1 != "sco-relay" || $2 !~ /^[!*]/ || $3 != "" ||
                ($4 != "" && $4 != "sco-relay") { bad = 1 }
            END { exit (NR != 1 || bad) ? 1 : 0 }' || die "$identity_error (group password, administrators or members)"
    fi
}
validate_identity

# ---- config filesystem preflight -------------------------------------------------------------------------------
# chmod changes an ACL mask, not named entries; 0600 -> 0640 can revive an unrelated reader.
# Refuse extended/default ACLs instead of silently repairing operator-managed policy.
validate_config_paths() {
    command -v getfacl > /dev/null 2>&1 || die "getfacl is required to check config ACLs (install the acl package)"
    # The lock lives directly under /run, so its parent needs the same write/ACL safeguards.
    [ ! -L /run ] && [ -d /run ] || die "unsafe lock parent: /run"
    for path in /etc /run "$config_dir" "$config"; do
        [ ! -L "$path" ] || die "unsafe config path: $path"
        [ -e "$path" ] || continue
        case $path in
            "$config") [ -f "$path" ] || die "unsafe config path: $path" ;;
            *) [ -d "$path" ] || die "unsafe config path: $path" ;;
        esac
        metadata=$(stat -c '%u:%h:%a' -- "$path") || die "cannot check config metadata: $path"
        printf '%s\n' "$metadata" | awk -F: -v file="$([ "$path" = "$config" ] && echo yes || echo no)" '
            NF != 3 || $1 != 0 || (file == "yes" && $2 != 1) { bad=1 }
            { mode=$3; g=substr(mode,length(mode)-1,1)+0; o=substr(mode,length(mode),1)+0
              if (int(g/2)%2 || int(o/2)%2) bad=1 }
            END { exit (NR != 1 || bad) ? 1 : 0 }
        ' || die "unsafe config ownership, links or write permissions: $path"
        acl=$(getfacl -cpn -- "$path") || die "cannot check config ACL: $path"
        printf '%s\n' "$acl" | awk '
            /^#/ || /^[[:space:]]*$/ { next }
            /^user::[rwx-][rwx-][rwx-]$/ { u++; next }
            /^group::[rwx-][rwx-][rwx-]$/ { g++; next }
            /^other::[rwx-][rwx-][rwx-]$/ { o++; next }
            { bad=1 }
            END { exit (bad || u != 1 || g != 1 || o != 1) ? 1 : 0 }
        ' || die "unsafe config ACL (extended/default ACLs are unsupported): $path"
    done
}
validate_config_paths

# mkdir is atomic and the checked /run parent is not writable by unprivileged users.
# Do not remove a pre-existing lock: it may belong to another live installation.
# A signal between mkdir and recording ownership can leave an empty stale lock.
if ! (umask 077; mkdir "$lock_dir") 2>/dev/null; then
    die "cannot acquire installation lock $lock_dir; another install may be running or a stale lock needs inspection"
fi
lock_held=yes

# ---- a copy of the tree, checked --------------------------------------------------------------------------------------
# The build folder is made new by mktemp: owned by root, mode 0700, empty. An existing folder is never used, because a folder
# somebody else prepared (or a symlink to one) could hold a CMake cache that makes the root build run their script.
# The copy goes into src/ inside it, and the build output into out/. Everything from here on reads the copy, never the tree
# you ran the script from, so what is checked is what is built, whatever happens to your tree while the packages install.

build_dir=$(mktemp -d /var/tmp/sco-relay-build.XXXXXX) || die "cannot make a build folder under /var/tmp"
src=$build_dir/src
mkdir "$src"
tar -C "$root" --anchored --exclude=./.git --exclude=./build --exclude='./build-*' -cf "$build_dir/tree.tar" . \
    || die "cannot copy the source tree"
tar -C "$src" --same-owner -xpf "$build_dir/tree.tar" || die "cannot unpack the copy of the source tree"
rm -f "$build_dir/tree.tar"

# What the check refuses, in the copy (owners and modes are the ones your tree had):
#   - a symbolic link, or anything else that is neither a regular file nor a folder (the repository has none, and the
#     build would follow a link out of the copy);
#   - a file or folder owned by anyone but root and the user who ran sudo (SUDO_UID);
#   - a file or folder writable by everyone;
#   - a file or folder writable by a group, unless that group is root's (gid 0) or the owner's private group: a group named
#     like the owning user, with no member listed in it, that is no other account's primary group. (A plain git clone on
#     Ubuntu or Fedora has that kind of group; a group shared between users does not pass.)
caller=${SUDO_UID:-0}
find "$src" -printf '%y %U %G %m %p\n' > "$build_dir/files.txt" \
    || die "cannot list the files of the copy (GNU find is needed). Installation stopped."
report=$(awk -v caller="$caller" -v src="$src" '
    {
        type = $1; uid = $2; gid = $3; mode = $4
        path = $0
        sub(/^[^ ]+ [^ ]+ [^ ]+ [^ ]+ /, "", path)
        if (index(path, src) == 1) path = substr(path, length(src) + 2)
        if (path == "") path = "."
        group_bits = substr(mode, length(mode) - 1, 1) + 0
        other_bits = substr(mode, length(mode), 1) + 0
        if (type == "l") { print "HARD a symbolic link: " path; exit }
        if (type != "f" && type != "d") { print "HARD neither a regular file nor a folder: " path; exit }
        if (uid != 0 && uid != caller) { print "HARD owned by another user: " path; exit }
        if (int(other_bits / 2) % 2 == 1) { print "HARD writable by everyone: " path; exit }
        if (int(group_bits / 2) % 2 == 1 && gid != 0 && !(seen[uid " " gid]++)) print "GROUP " uid " " gid " " path
    }' "$build_dir/files.txt") || die "cannot read the file list of the copy (awk is needed). Installation stopped."

hard=$(printf '%s\n' "$report" | sed -n 's/^HARD //p' | head -n 1)
if [ -n "$hard" ]; then
    die "the source tree is not safe to build as root ($hard). Make a copy only you can write (a fresh git clone as that user is fine; no symbolic links) and run the installer from there. Installation stopped."
fi

# gid 0, or the owner's private group: named like the owning user, nobody listed in it, nobody else has it as primary group.
private_group() {   # <uid> <gid>
    [ "$2" -eq 0 ] && return 0
    owner_line=$(getent passwd "$1") || return 1
    owner_name=${owner_line%%:*}
    [ -n "$owner_name" ] || return 1
    group_line=$(getent group "$2") || return 1
    [ "${group_line%%:*}" = "$owner_name" ] || return 1
    [ -z "${group_line##*:}" ] || return 1
    shadow_line=$(getent gshadow "$owner_name") || return 1
    local_shadow=$(getent -s files gshadow "$owner_name") || return 1
    [ "$shadow_line" = "$local_shadow" ] || return 1
    # Numeric lookups can hide aliases; inspect both effective and local views.
    for provider in effective files; do
        if [ "$provider" = files ]; then
            users=$(getent -s files passwd) || return 1
            groups=$(getent -s files group) || return 1
            shadows=$(getent -s files gshadow) || return 1
        else
            users=$(getent passwd) || return 1
            groups=$(getent group) || return 1
            shadows=$(getent gshadow) || return 1
        fi
        printf '%s\n' "$users" | awk -F: -v uid="$1" -v gid="$2" -v owner="$owner_name" -v row="$owner_line" '
            $3 == uid { n++; if ($0 != row) bad=1 }
            $4 == gid && $0 != row { bad=1 }
            END { exit (bad || n != 1) }' || return 1
        printf '%s\n' "$groups" | awk -F: -v gid="$2" -v row="$group_line" '
            $3 == gid { n++; if ($0 != row) bad=1 }
            END { exit (bad || n != 1) }' || return 1
        printf '%s\n' "$shadows" | awk -F: -v owner="$owner_name" -v row="$local_shadow" '
            $1 == owner { n++; if ($0 != row || NF != 4 || $2 !~ /^[!*]/ || $3 != "" || $4 != "") bad=1 }
            END { exit (bad || n != 1) }' || return 1
    done
    return 0
}
groups_found=$(printf '%s\n' "$report" | sed -n 's/^GROUP //p')
if [ -n "$groups_found" ]; then
    while read -r group_uid group_gid group_path; do
        private_group "$group_uid" "$group_gid" ||
            die "the source tree is not safe to build as root (writable by a group that is not the private group of its owner: group $group_gid, $group_path). Make a copy only you can write (a fresh git clone as that user is fine) and run the installer from there. Installation stopped."
    done <<EOF
$groups_found
EOF
fi
example=$src/server/sco-relay.example.conf
unit_source=$src/server/deploy/sco-relay.service

# ---- build tools -------------------------------------------------------------------------------------------------
# The build needs cmake, a C++20 compiler, make, and the CA certificates so that cmake can check the TLS certificate of
# its one download. On Debian and Ubuntu what is missing is installed; anywhere else it is named and nothing is changed.

missing=""
command -v cmake > /dev/null 2>&1 || missing="$missing cmake"
command -v g++ > /dev/null 2>&1 || missing="$missing g++"
command -v make > /dev/null 2>&1 || missing="$missing make"
# The certificate bundle sits in a different place on each family of distributions: Debian, Ubuntu and Alpine; Fedora, RHEL
# and CentOS; openSUSE and SLES; the extracted bundle of newer Fedora and RHEL.
have_certificates=""
for bundle in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt /etc/ssl/ca-bundle.pem \
              /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem /etc/ssl/cert.pem; do
    if [ -s "$bundle" ]; then have_certificates=yes; break; fi
done
[ -n "$have_certificates" ] || missing="$missing ca-certificates"

if [ -n "$missing" ]; then
    if command -v apt-get > /dev/null 2>&1; then
        say "installing the missing build tools with apt-get:$missing"
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -q || die "apt-get update failed. Check the network, or install these yourself and run this again:$missing"
        # The names are known words with no spaces in them, so the unquoted expansion is the list.
        # shellcheck disable=SC2086
        apt-get install -y -q --no-install-recommends $missing || die "apt-get could not install:$missing"
    else
        die "build tools are missing:$missing. Install cmake 3.20 or newer, a C++20 compiler (g++), make and the CA certificates with your package manager (the names above are the Debian ones), then run this again."
    fi
fi
for tool in cmake g++ make; do
    command -v "$tool" > /dev/null 2>&1 || die "$tool is still missing after the install step"
done

cmake_version=$(cmake --version | sed -n '1s/^[^0-9]*\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
cmake_major=${cmake_version%%.*}
cmake_minor=${cmake_version#*.}
if [ -n "$cmake_version" ] && { [ "$cmake_major" -lt 3 ] || { [ "$cmake_major" -eq 3 ] && [ "$cmake_minor" -lt 20 ]; }; }; then
    die "cmake $cmake_version is too old: the build needs 3.20 or newer. Install a newer cmake and run this again."
fi

# ---- build -------------------------------------------------------------------------------------------------------
# Only the relay is built, from the copy: no tests, no plugin. The Unix Makefiles generator is named on purpose: the
# no-game-code check (server/README.md, 'The sco-core boundary') reads the link line that only Makefile generators write.

threads=$(nproc 2> /dev/null || echo 1)
[ "$threads" -le 4 ] || threads=4          # a small server has little memory; four compilers at once is plenty
say "building the relay in $build_dir, which is removed afterwards (every run builds from scratch; the first part downloads the pinned SDK files; a few minutes)"
cmake -S "$src" -B "$build_dir/out" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DSC_MP_BUILD_TESTS=OFF
cmake --build "$build_dir/out" --target sco-relay --parallel "$threads"
[ -x "$build_dir/out/server/sco-relay" ] || die "the build finished, but $build_dir/out/server/sco-relay is not there"

# ---- the account, the program, the config ------------------------------------------------------------------------
# A system account has no password and no login shell: it exists only to run this one service.

validate_identity
nologin=$(command -v nologin || true)
nologin=${nologin:-/usr/sbin/nologin}
command -v groupadd > /dev/null 2>&1 && command -v useradd > /dev/null 2>&1 \
    || die "groupadd and useradd are needed to make the $account account (the shadow-utils package)"
if ! getent group "$account" > /dev/null 2>&1; then
    groupadd --system "$account"
fi
if ! id "$account" > /dev/null 2>&1; then
    useradd --system --gid "$account" --home-dir /nonexistent --no-create-home --shell "$nologin" "$account"
fi
validate_identity
getent passwd "$account" > /dev/null && getent group "$account" > /dev/null \
    || die "account creation did not produce the required identity; installation stopped"

# Recheck after the trusted build and identity creation; no concurrent administrator
# changes to identity/policy/config paths are supported during this installation.
validate_config_paths
# Refuse link/special-file config paths rather than changing a link target as root.
[ ! -L "$config_dir" ] && { [ ! -e "$config_dir" ] || [ -d "$config_dir" ]; } \
    || die "unsafe config path: $config_dir"
[ ! -L "$config" ] && { [ ! -e "$config" ] || [ -f "$config" ]; } \
    || die "unsafe config path: $config"
# root owns the folder and the file; the account may enter the folder and read the file; nobody else may.
install -d -o root -g "$account" -m 0750 "$config_dir"
install -o root -g root -m 0755 "$build_dir/out/server/sco-relay" "$program"

if [ ! -e "$config" ]; then
    # 32 random bytes as 64 hex digits: no character that means something to a shell or to the config reader, and well
    # inside the 12 to 128 bytes the relay accepts.
    passphrase=$(od -An -N 32 -tx1 /dev/urandom | tr -d ' \n')
    [ "${#passphrase}" -eq 64 ] || die "could not read enough random bytes from /dev/urandom"
    temporary=$(mktemp "$config_dir/.sco-relay.conf.XXXXXX")     # mode 0600 and root's, until it is moved into place
    # A line at a time with shell built-ins only, so the passphrase never appears in a command line that another user
    # could list while this runs, and never on the screen.
    while IFS= read -r line || [ -n "$line" ]; do
        case $line in
            "passphrase = "*) printf 'passphrase = %s\n' "$passphrase" ;;
            *) printf '%s\n' "$line" ;;
        esac
    done < "$example" > "$temporary"
    [ "$(grep -c '^passphrase = [0-9a-f]\{64\}$' "$temporary")" -eq 1 ] \
        || die "the example config has no 'passphrase = ' line to fill in"
    chown root:"$account" "$temporary"
    chmod 0640 "$temporary"
    mv "$temporary" "$config"
    temporary=""
    passphrase=""
    say "made $config with a new random passphrase"
else
    say "kept the existing $config, and with it the passphrase"
fi
# An existing file gets the same owner and mode: the service must be able to read it, and other users must not.
chown root:"$account" "$config"
chmod 0640 "$config"

port=$(sed -n 's/^[[:space:]]*port[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$config" | head -n 1)
port=${port:-64091}

# ---- tell the operator, then run it ---------------------------------------------------------------------------------------

systemd_runs=""
if [ -d /run/systemd/system ] && command -v systemctl > /dev/null 2>&1; then
    systemd_runs=yes
fi

if [ -n "$systemd_runs" ]; then
    install -o root -g root -m 0644 "$unit_source" "$unit_target"
    systemctl daemon-reload
    systemctl enable sco-relay.service
    systemctl reset-failed sco-relay.service > /dev/null 2>&1 || true
    # restart, not start: on a second run the service is up and still runs the old program.
    systemctl restart sco-relay.service || die "the service did not start. Read why with: journalctl -u sco-relay -n 30"
    sleep 2
    systemctl is-active --quiet sco-relay.service || die "the service started, but did not stay up. Read why with: journalctl -u sco-relay -n 30"
    say "done. The relay runs as the systemd service sco-relay, as the $account account, and starts at boot."
    say "  status and log:  systemctl status sco-relay    journalctl -u sco-relay -f"
    say "  after editing the config:  systemctl restart sco-relay"
fi

say "  passphrase:  the 'passphrase =' line of $config. Only root and the $account account can read it; it is not shown here. Give it to your players."
say "  port:        UDP $port. Open it in your firewall for your LAN or VPN network only, never to the whole internet. This installer changes no firewall."
say "  who may join: the relay drops every address except loopback and the private ranges (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 169.254.0.0/16)."
say "    Public addresses (a rented server, players at home) need a VPN whose addresses are private, or 'allowed_addresses' in the config naming the ranges players come from."
say "    Players' sc-offline currently blocks CGNAT ranges such as 100.64.0.0/10 (found by reading its source, not run in game), so a VPN that hands out those addresses may not work for them yet."
say "  more:        server/deploy/README.md"

if [ -z "$systemd_runs" ]; then
    # No service without systemd: this command becomes the relay. Changing to the service account needs root; this script is
    # root, but someone starting the relay again later usually is not, so the command printed for later starts with sudo.
    if command -v setpriv > /dev/null 2>&1; then
        later="sudo setpriv --reuid=$account --regid=$account --init-groups $program $config"
    else
        later="sudo su -s /bin/sh $account -c 'exec $program $config'"
    fi
    say "start it again later with: $later"
    say "systemd is not running here, so there is no service. Starting the relay now, in the foreground, as $account. This command does not return: press Ctrl+C or send SIGTERM to stop the relay (it exits 0)."
    # exec does not run the exit trap, so remove the build folder first. The relay replaces this script: same process, so
    # the signals reach it directly.
    cleanup
    trap - EXIT
    if command -v setpriv > /dev/null 2>&1; then
        exec setpriv --reuid="$account" --regid="$account" --init-groups "$program" "$config"
    fi
    exec su -s /bin/sh "$account" -c "exec $program $config"
fi
