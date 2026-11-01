#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Test installer identity and config-path checks without root or system account changes.
set -eu

guard=${1:-}
installer=${2:-}
if [ -z "$guard" ] || [ ! -f "$guard" ] || [ -z "$installer" ] || [ ! -f "$installer" ]; then
    echo "usage: sh test_install_guard.sh <install_guard.sh> <install.sh>" >&2
    exit 2
fi
. "$guard"
work=$(mktemp -d)
trap 'rm -rf "$work"' 0 HUP INT TERM
# Source only the marked function definition; the installer itself is never run.
awk '
    $0 == "# BEGIN PRIVATE_GROUP_CHECK" { if (started++ || inside || ended) exit 1; inside = 1; next }
    $0 == "# END PRIVATE_GROUP_CHECK" { if (!inside || ended++) exit 1; inside = 0; next }
    inside { print }
    END { if (started != 1 || ended != 1 || inside) exit 1 }
' "$installer" > "$work/private_group.sh" || { echo "cannot extract private group check" >&2; exit 2; }
. "$work/private_group.sh"

failures=0
ok() { echo "ok: $*"; }
fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }

mock_group=""
mock_groups_all=""
mock_user=""
mock_passwd_all=""
mock_id_uid=""
mock_id_gid=""
mock_groups=""
mock_passwd_status=""
mock_passwd_exit=0
mock_group_lookup_exit=0
mock_user_lookup_exit=0
mock_group_enum_exit=0
mock_user_enum_exit=0
groupadd_calls=0
useradd_calls=0
usermod_calls=0
adduser_calls=0
chown_calls=0
chmod_calls=0

getent() {
    case "$1" in
        group)
            if [ "$#" -eq 2 ]; then
                if [ "$mock_group_lookup_exit" -ne 0 ]; then
                    [ -z "$mock_group" ] || printf '%s\n' "$mock_group"
                    return "$mock_group_lookup_exit"
                fi
                [ -n "$mock_group" ] || return 2
                printf '%s\n' "$mock_group"
                return 0
            fi
            printf '%s\n' "$mock_groups_all"
            return "$mock_group_enum_exit"
            ;;
        passwd)
            if [ "$#" -eq 2 ]; then
                if [ "$mock_user_lookup_exit" -ne 0 ]; then
                    [ -z "$mock_user" ] || printf '%s\n' "$mock_user"
                    return "$mock_user_lookup_exit"
                fi
                [ -n "$mock_user" ] || return 2
                printf '%s\n' "$mock_user"
                return 0
            fi
            printf '%s\n' "$mock_passwd_all"
            return "$mock_user_enum_exit"
            ;;
    esac
    return 2
}

groupadd() {
    groupadd_calls=$((groupadd_calls + 1))
    [ "$1" = --system ] && [ "$2" = sco-relay ] || return 2
    mock_group=sco-relay:x:64001:
    mock_groups_all="$mock_groups_all
$mock_group"
}

useradd() {
    useradd_calls=$((useradd_calls + 1))
    mock_user='sco-relay:!:64001:64001::/nonexistent:/usr/sbin/nologin'
    mock_passwd_all="$mock_passwd_all
$mock_user"
    mock_id_uid=64001
    mock_id_gid=64001
    mock_groups=64001
    mock_passwd_status='sco-relay L 2026-11-01 0 99999 7 -1'
}
usermod() { usermod_calls=$((usermod_calls + 1)); return 1; }
adduser() { adduser_calls=$((adduser_calls + 1)); return 1; }

id() {
    [ "$2" = sco-relay ] || return 2
    case "$1" in
        -u) printf '%s\n' "$mock_id_uid" ;;
        -g) printf '%s\n' "$mock_id_gid" ;;
        -G) printf '%s\n' "$mock_groups" ;;
        *) return 2 ;;
    esac
}

passwd() {
    [ "$1" = -S ] && [ "$2" = sco-relay ] || return 2
    [ "$mock_passwd_exit" -eq 0 ] || return "$mock_passwd_exit"
    printf '%s\n' "$mock_passwd_status"
}

chown() {
    chown_calls=$((chown_calls + 1))
    [ "$1" = root:sco-relay ]
}

chmod() {
    chmod_calls=$((chmod_calls + 1))
    [ "$1" = 0640 ]
}

set_safe_identity() {
    mock_group='sco-relay:x:64001:'
    mock_groups_all="root:x:0:
$mock_group"
    mock_user='sco-relay:!:64001:64001::/nonexistent:/usr/sbin/nologin'
    mock_passwd_all="root:x:0:0:root:/root:/bin/sh
$mock_user"
    mock_id_uid=64001
    mock_id_gid=64001
    mock_groups=64001
    mock_passwd_status='sco-relay L 2026-11-01 0 99999 7 -1'
    mock_passwd_exit=0
    mock_group_lookup_exit=0
    mock_user_lookup_exit=0
    mock_group_enum_exit=0
    mock_user_enum_exit=0
    groupadd_calls=0
    useradd_calls=0
    usermod_calls=0
    adduser_calls=0
}

expect_identity_rejected() {
    label=$1
    if ensure_service_identity sco-relay /usr/sbin/nologin 2>/dev/null; then
        fail "$label was accepted"
    else
        ok "$label was refused"
    fi
}
expect_identity_rejected_without_creation() {
    label=$1
    if ensure_service_identity sco-relay /usr/sbin/nologin 2>/dev/null; then
        fail "$label was accepted"
    elif [ "$groupadd_calls" -eq 0 ] && [ "$useradd_calls" -eq 0 ] &&
         [ "$usermod_calls" -eq 0 ] && [ "$adduser_calls" -eq 0 ]; then
        ok "$label was refused before account/group mutation"
    else
        fail "$label invoked an account/group mutation"
    fi
}

set_private_identity() {
    set_safe_identity
    mock_user='owner:x:1001:1001:Owner:/home/owner:/bin/sh'
    mock_passwd_all="root:x:0:0:root:/root:/bin/sh
$mock_user"
    mock_group='owner:x:1001:'
    mock_groups_all="root:x:0:
$mock_group"
}
expect_private_rejected() {
    label=$1
    if private_group 1001 1001; then
        fail "$label was accepted as a private group"
    else
        ok "$label was refused as a private group"
    fi
}

set_private_identity
if private_group 1001 1001; then
    ok "matching, fully enumerated owner and private group are accepted"
else
    fail "matching, fully enumerated owner and private group were refused"
fi
set_private_identity
mock_user_lookup_exit=3
mock_group_lookup_exit=3
if private_group 1001 0; then
    ok "the explicitly allowed root group does not require NSS lookups"
else
    fail "the explicitly allowed root group was refused"
fi
set_private_identity
mock_user_lookup_exit=3
expect_private_rejected "failed owner lookup with partial output"
set_private_identity
mock_group_lookup_exit=3
expect_private_rejected "failed group lookup with partial output"
set_private_identity
mock_user_enum_exit=3
expect_private_rejected "failed user enumeration with partial output"
set_private_identity
mock_group_enum_exit=3
expect_private_rejected "failed group enumeration with partial output"
set_private_identity
mock_user='owner:x:1001'
expect_private_rejected "incomplete targeted owner record"
set_private_identity
mock_group='owner:x:1001'
expect_private_rejected "incomplete targeted group record"
set_private_identity
mock_passwd_all='root:x:0:0:root:/root:/bin/sh'
expect_private_rejected "owner missing from successful enumeration"
set_private_identity
mock_groups_all='root:x:0:'
expect_private_rejected "group missing from successful enumeration"
set_private_identity
mock_passwd_all="$mock_passwd_all
partial:x:1002"
expect_private_rejected "incomplete record in user enumeration"
set_private_identity
mock_groups_all="$mock_groups_all
partial:x"
expect_private_rejected "incomplete record in group enumeration"
set_private_identity
mock_groups_all="$mock_groups_all
alias:x:1001:"
expect_private_rejected "a group ID shared by another group"
set_private_identity
mock_passwd_all="$mock_passwd_all
other:x:1002:1001:Other:/home/other:/bin/sh"
expect_private_rejected "a group used by another user's primary identity"

set_safe_identity
mock_group=
mock_groups_all='root:x:0:'
mock_user='sco-relay:!:64001:10::/nonexistent:/usr/sbin/nologin'
mock_passwd_all="root:x:0:0:root:/root:/bin/sh
$mock_user"
expect_identity_rejected_without_creation "an existing account without its dedicated group"
set_safe_identity
mock_user=
mock_passwd_all='root:x:0:0:root:/root:/bin/sh
other:x:1002:64001:Other:/home/other:/bin/sh'
expect_identity_rejected_without_creation "an existing group used by another account"
set_safe_identity
mock_group=
mock_groups_all='root:x:0:'
mock_user_lookup_exit=3
expect_identity_rejected_without_creation "a failed existing-user lookup with partial output"
set_safe_identity
mock_group_lookup_exit=3
expect_identity_rejected_without_creation "a failed existing-group lookup with partial output"
set_safe_identity
mock_group=
mock_groups_all='root:x:0:'
mock_user_lookup_exit=2
expect_identity_rejected_without_creation "a missing-user status carrying partial output"
set_safe_identity
mock_group_lookup_exit=2
expect_identity_rejected_without_creation "a missing-group status carrying partial output"
set_safe_identity
mock_user_enum_exit=3
expect_identity_rejected_without_creation "failed service-user enumeration with partial output"
set_safe_identity
mock_group_enum_exit=3
expect_identity_rejected_without_creation "failed service-group enumeration with partial output"
set_safe_identity
mock_user='sco-relay:!:64001'
expect_identity_rejected_without_creation "an incomplete targeted service-user record"
set_safe_identity
mock_group='sco-relay:x:64001'
expect_identity_rejected_without_creation "an incomplete targeted service-group record"
set_safe_identity
mock_passwd_all='root:x:0:0:root:/root:/bin/sh
sco-relay:!:64001:64001:Different:/nonexistent:/usr/sbin/nologin'
expect_identity_rejected_without_creation "a targeted service-user record that disagrees with enumeration"
set_safe_identity
mock_user=
mock_passwd_all='root:x:0:0:root:/root:/bin/sh'
mock_groups_all='root:x:0:
sco-relay:x:64001:other'
expect_identity_rejected_without_creation "a targeted service-group record that disagrees with enumeration"

set_safe_identity
if ensure_service_identity sco-relay /usr/sbin/nologin; then
    [ "$groupadd_calls" -eq 0 ] && [ "$useradd_calls" -eq 0 ] \
        && ok "a pre-existing isolated service identity is accepted" \
        || fail "a valid existing identity was unexpectedly recreated"
else
    fail "a pre-existing isolated service identity was refused"
fi
mock_user='sco-relay:!:64001:64001:Relay service:/nonexistent:/usr/sbin/nologin'
mock_passwd_all=$mock_user
if ensure_service_identity sco-relay /usr/sbin/nologin; then
    ok "a valid GECOS field is parsed without changing the account checks"
else
    fail "a valid GECOS field confused the account parser"
fi

mock_group='sco-relay:x:64001:another-user'
mock_groups_all=$mock_group
expect_identity_rejected "a group with listed members"
set_safe_identity
mock_groups_all="$mock_group
sco-relay:x:64002:"
expect_identity_rejected "duplicate service-group name records"
set_safe_identity
mock_groups_all="$mock_group
alias-group:x:64001:another-user"
expect_identity_rejected "a shared numeric group ID"
set_safe_identity
mock_passwd_all="$mock_user
another-user:x:1002:64001::/home/another-user:/bin/sh"
expect_identity_rejected "a group used as another user's primary group"
set_safe_identity
mock_passwd_all="$mock_user
other-uid:x:64001:1002::/home/other-uid:/bin/sh"
expect_identity_rejected "a UID shared with another account"
set_safe_identity
mock_user='sco-relay:!:0:64001::/nonexistent:/usr/sbin/nologin'
mock_passwd_all=$mock_user
expect_identity_rejected "a UID-0 service account"
set_safe_identity
mock_user='sco-relay:!:64001:10::/nonexistent:/usr/sbin/nologin'
mock_passwd_all=$mock_user
expect_identity_rejected "a service account with a different primary group"
set_safe_identity
mock_passwd_all="$mock_user
$mock_user"
expect_identity_rejected "duplicate service-account passwd records"
set_safe_identity
mock_id_uid=64002
expect_identity_rejected "an id UID that disagrees with getent"
set_safe_identity
mock_id_gid=64002
expect_identity_rejected "an id GID that disagrees with getent"
set_safe_identity
mock_user='sco-relay:!:64001:64001::/nonexistent:/bin/sh'
mock_passwd_all=$mock_user
expect_identity_rejected "a service account with a login shell"
set_safe_identity
mock_groups='64001 10'
expect_identity_rejected "a service account with supplementary groups"
set_safe_identity
mock_passwd_status='sco-relay P 2026-11-01 0 99999 7 -1'
expect_identity_rejected "an unlocked password status"
set_safe_identity
mock_passwd_status='sco-relay NP 2026-11-01 0 99999 7 -1'
expect_identity_rejected "a no-password status that is not verifiably locked"
set_safe_identity
mock_passwd_status='sco-relay X 2026-11-01 0 99999 7 -1'
expect_identity_rejected "an unknown password status"
set_safe_identity
mock_passwd_status="sco-relay L 2026-11-01 0 99999 7 -1
sco-relay L 2026-11-01 0 99999 7 -1"
expect_identity_rejected "multiple passwd status records"
set_safe_identity
mock_passwd_exit=127
expect_identity_rejected "passwd status command failure"
set_safe_identity
saved_path=$PATH
unset -f passwd
PATH=/nonexistent
if ensure_service_identity sco-relay /usr/sbin/nologin 2>/dev/null; then
    fail "a missing passwd command was accepted"
else
    ok "a missing passwd command is refused"
fi
PATH=$saved_path
passwd() {
    [ "$1" = -S ] && [ "$2" = sco-relay ] || return 2
    [ "$mock_passwd_exit" -eq 0 ] || return "$mock_passwd_exit"
    printf '%s\n' "$mock_passwd_status"
}

mock_group=
mock_user=
mock_groups_all='root:x:0:'
mock_passwd_all='root:x:0:0:root:/root:/bin/sh'
mock_groups=
groupadd_calls=0
useradd_calls=0
if ensure_service_identity sco-relay /usr/sbin/nologin; then
    [ "$groupadd_calls" -eq 1 ] && [ "$useradd_calls" -eq 1 ] \
        && ok "a missing identity is created through mocked account commands and then validated" \
        || fail "the mock account creation path did not run exactly once"
else
    fail "the mocked account creation path was refused"
fi

safe_dir=$work/etc/sco-relay
safe_config=$safe_dir/sco-relay.conf
mkdir -p "$safe_dir"
printf '%s\n' 'port = 64091' > "$safe_config"
chown_calls=0
chmod_calls=0
if set_config_permissions "$safe_dir" "$safe_config" sco-relay; then
    [ "$chown_calls" -eq 1 ] && [ "$chmod_calls" -eq 1 ] \
        && ok "a regular config file receives the requested permission operations" \
        || fail "safe config permission operations were not called once"
else
    fail "a regular config file was refused"
fi

outside=$work/outside.conf
printf '%s\n' 'unrelated=secret' > "$outside"
rm -f "$safe_config"
ln -s "$outside" "$safe_config"
chown_calls=0
chmod_calls=0
if set_config_permissions "$safe_dir" "$safe_config" sco-relay 2>/dev/null; then
    fail "a symlinked config file was accepted"
else
    [ "$chown_calls" -eq 0 ] && [ "$chmod_calls" -eq 0 ] \
        && ok "a symlinked config is refused before ownership/mode changes" \
        || fail "permission commands ran for a symlinked config"
fi

rm -f "$safe_config"
mkdir "$safe_config"
if validate_config_paths "$safe_dir" "$safe_config" 2>/dev/null; then
    fail "a directory at the config path was accepted"
else
    ok "a non-regular config path is refused"
fi
rmdir "$safe_config"
ln -s "$work/missing-target" "$safe_config"
if validate_config_paths "$safe_dir" "$safe_config" 2>/dev/null; then
    fail "a dangling config symlink was accepted"
else
    ok "a dangling config symlink is refused"
fi

real_dir=$work/real-dir
mkdir -p "$real_dir"
printf '%s\n' 'port = 64091' > "$real_dir/sco-relay.conf"
ln -s "$real_dir" "$work/linked-dir"
chown_calls=0
chmod_calls=0
if set_config_permissions "$work/linked-dir" "$work/linked-dir/sco-relay.conf" sco-relay 2>/dev/null; then
    fail "a symlinked config directory was accepted"
else
    [ "$chown_calls" -eq 0 ] && [ "$chmod_calls" -eq 0 ] \
        && ok "a symlinked config directory is refused before ownership/mode changes" \
        || fail "permission commands ran through a symlinked config directory"
fi
ln -s "$real_dir" "$real_dir/alias"
chown_calls=0
chmod_calls=0
if set_config_permissions "$real_dir" "$real_dir/alias/sco-relay.conf" sco-relay 2>/dev/null; then
    fail "an ancestor symlink in the config file path was accepted"
else
    [ "$chown_calls" -eq 0 ] && [ "$chmod_calls" -eq 0 ] \
        && ok "an ancestor symlink in the config path is refused before ownership/mode changes" \
        || fail "permission commands ran through an ancestor symlink"
fi
if validate_config_paths "$real_dir/../real-dir" "$real_dir/../real-dir/sco-relay.conf" 2>/dev/null; then
    fail "dot traversal in a config path was accepted"
else
    ok "dot traversal in config paths is refused"
fi

if [ "$failures" -ne 0 ]; then
    echo "test_install_guard: $failures failure(s)" >&2
    exit 1
fi
echo "test_install_guard: all checks passed"
