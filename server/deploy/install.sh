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

say() { echo "install: $*"; }
die() { echo "install: $*" >&2; exit 1; }

# What this script makes and must not leave behind: the half-made config and the build folder (which holds the copy).
build_dir=""
temporary=""
cleanup() {
    [ -z "$temporary" ] || rm -f "$temporary"
    [ -z "$build_dir" ] || rm -rf "$build_dir"
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
    || die "cannot list the files of the copy (GNU find is needed). Nothing was changed."
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
    }' "$build_dir/files.txt") || die "cannot read the file list of the copy (awk is needed). Nothing was changed."

hard=$(printf '%s\n' "$report" | sed -n 's/^HARD //p' | head -n 1)
if [ -n "$hard" ]; then
    die "the source tree is not safe to build as root ($hard). Make a copy only you can write (a fresh git clone as that user is fine; no symbolic links) and run the installer from there. Nothing was changed."
fi

# gid 0, or the owner's private group: named like the owning user, nobody listed in it, nobody else has it as primary group.
# Keep this check inline: the source copy is not yet cleared for group-writable files, so it must not supply executable code.
# BEGIN PRIVATE_GROUP_CHECK
private_group() {   # <uid> <gid>
    _private_uid=$1
    _private_gid=$2
    case "$_private_uid" in ''|*[!0-9]*) return 1 ;; esac
    case "$_private_gid" in ''|*[!0-9]*) return 1 ;; esac
    [ "$_private_gid" -eq 0 ] && return 0
    _private_owner_line=$(getent passwd "$_private_uid" 2>/dev/null) || return 1
    _private_owner=$(printf '%s\n' "$_private_owner_line" | awk -F: -v uid="$_private_uid" -v gid="$_private_gid" \
        'NR == 1 && NF == 7 && $1 != "" && $3 == uid && $4 == gid { name = $1 } END { if (NR != 1 || name == "") exit 1; print name }') || return 1
    _private_group_line=$(getent group "$_private_gid" 2>/dev/null) || return 1
    printf '%s\n' "$_private_group_line" | awk -F: -v owner="$_private_owner" -v gid="$_private_gid" \
        'NR == 1 && NF == 4 && $1 == owner && $3 == gid && $4 == "" { ok = 1 } END { exit !(NR == 1 && ok) }' || return 1
    _private_users=$(getent passwd 2>/dev/null) || return 1
    printf '%s\n' "$_private_users" | awk -F: -v owner="$_private_owner" -v uid="$_private_uid" -v gid="$_private_gid" -v expected="$_private_owner_line" '
        NF != 7 || $1 == "" || $3 !~ /^[0-9]+$/ || $4 !~ /^[0-9]+$/ { bad = 1 }
        $1 == owner { named++; if ($0 == expected) matched++ }
        ($3 == uid || $4 == gid) && $1 != owner { bad = 1 }
        END { exit !(NR > 0 && !bad && named == 1 && matched == 1) }' || return 1
    _private_groups=$(getent group 2>/dev/null) || return 1
    printf '%s\n' "$_private_groups" | awk -F: -v owner="$_private_owner" -v gid="$_private_gid" -v expected="$_private_group_line" '
        NF != 4 || $1 == "" || $3 !~ /^[0-9]+$/ { bad = 1 }
        $1 == owner { named++; if ($0 == expected) matched++ }
        $3 == gid && $1 != owner { bad = 1 }
        END { exit !(NR > 0 && !bad && named == 1 && matched == 1) }' || return 1
    return 0
}
# END PRIVATE_GROUP_CHECK
groups_found=$(printf '%s\n' "$report" | sed -n 's/^GROUP //p')
if [ -n "$groups_found" ]; then
    while read -r group_uid group_gid group_path; do
        private_group "$group_uid" "$group_gid" ||
            die "the source tree is not safe to build as root (writable by a group that is not the private group of its owner: group $group_gid, $group_path). Make a copy only you can write (a fresh git clone as that user is fine) and run the installer from there. Nothing was changed."
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
# Password authentication is locked and the account has no login shell: it exists only to run this service.

nologin=$(command -v nologin || true)
nologin=${nologin:-/usr/sbin/nologin}
command -v groupadd > /dev/null 2>&1 && command -v useradd > /dev/null 2>&1 \
    || die "groupadd and useradd are needed to make the $account account (the shadow-utils package)"
command -v getent > /dev/null 2>&1 && command -v id > /dev/null 2>&1 \
    || die "getent and id are needed to verify the $account account and group"
command -v passwd > /dev/null 2>&1 \
    || die "passwd is needed to verify that password authentication is locked for $account"
. "$src/server/deploy/install_guard.sh"
ensure_service_identity "$account" "$nologin" \
    || die "the existing or newly created $account identity is not isolated and safe to use"

# root owns the folder and the file; the account may enter the folder and read the file; nobody else may.
validate_config_paths "$config_dir" "$config" || die "the config path or one of its ancestors is not a safe regular path"
install -d -o root -g "$account" -m 0750 "$config_dir"
validate_config_paths "$config_dir" "$config" || die "the config path or one of its ancestors changed to an unsafe path"
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
    set_config_permissions "$config_dir" "$temporary" "$account" \
        || die "could not safely set permissions on the new config"
    validate_config_paths "$config_dir" "$config" \
        || die "the config path changed to an unsafe path during installation"
    [ ! -e "$config" ] || die "a config file appeared during installation; nothing was replaced"
    mv "$temporary" "$config"
    temporary=""
    passphrase=""
    say "made $config with a new random passphrase"
else
    say "kept the existing $config, and with it the passphrase"
fi
# An existing file gets the same owner and mode: the service must be able to read it, and other users must not.
set_config_permissions "$config_dir" "$config" "$account" \
    || die "could not safely set permissions on the config"

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
say "    Current sc-offline source accepts 100.64.0.0/10 as a private-range admission input, but native multiplayer startup remains refused; this relay setting does not establish that clients can join in game."
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
