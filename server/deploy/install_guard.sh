#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Checks for privileged installation state that must not be adopted blindly.
# These checks reject known unsafe account/group and symlink configurations.
# They are not race-free: callers must keep the path's parent directories root-owned
# and non-writable by untrusted users while changing ownership or mode.

_install_guard_fail() {
    printf 'install guard: %s\n' "$*" >&2
    return 1
}

ensure_service_identity() {   # <account> <nologin path>
    # Identity comes from NSS getent/id and password-lock status from passwd -S;
    # no password hash or shadow contents are parsed or logged here. The helper
    # cannot see entries NSS backends do not enumerate or stop an admin change.
    _guard_account=$1
    _guard_nologin=$2
    command -v passwd > /dev/null 2>&1 || {
        _install_guard_fail "passwd is required to verify that password authentication is locked"
        return 1
    }
    _guard_all_groups=$(getent group 2>/dev/null) || {
        _install_guard_fail "cannot enumerate groups through getent"
        return 1
    }
    _guard_passwd_all=$(getent passwd 2>/dev/null) || {
        _install_guard_fail "cannot enumerate users through getent"
        return 1
    }
    if ! printf '%s\n' "$_guard_all_groups" | awk -F: \
        'NF != 4 || $1 == "" || $3 !~ /^[0-9]+$/ { bad = 1 } END { exit !(NR > 0 && !bad) }' ||
       ! printf '%s\n' "$_guard_passwd_all" | awk -F: \
        'NF != 7 || $1 == "" || $3 !~ /^[0-9]+$/ || $4 !~ /^[0-9]+$/ { bad = 1 } END { exit !(NR > 0 && !bad) }'; then
        _install_guard_fail "user or group enumeration is incomplete or malformed"
        return 1
    fi

    # A status of 2 means a missing key for getent; any other failure or output
    # accompanying a missing-key status is unverifiable and must not create an identity.
    _guard_group_status=0
    _guard_group_line=$(getent group "$_guard_account" 2>/dev/null) || _guard_group_status=$?
    _guard_user_status=0
    _guard_user_line=$(getent passwd "$_guard_account" 2>/dev/null) || _guard_user_status=$?
    case "$_guard_group_status" in
        0) printf '%s\n' "$_guard_group_line" | awk -F: -v account="$_guard_account" \
             'NR == 1 && NF == 4 && $1 == account { ok = 1 } END { exit !(NR == 1 && ok) }' || {
                 _install_guard_fail "the $_guard_account group lookup is ambiguous or malformed"; return 1; } ;;
        2) [ -z "$_guard_group_line" ] && printf '%s\n' "$_guard_all_groups" | awk -F: -v account="$_guard_account" \
             '$1 == account { found = 1 } END { exit found }' || {
                 _install_guard_fail "the $_guard_account group lookup disagrees with enumeration"; return 1; } ;;
        *) _install_guard_fail "cannot look up the $_guard_account group"; return 1 ;;
    esac
    case "$_guard_user_status" in
        0) printf '%s\n' "$_guard_user_line" | awk -F: -v account="$_guard_account" \
             'NR == 1 && NF == 7 && $1 == account { ok = 1 } END { exit !(NR == 1 && ok) }' || {
                 _install_guard_fail "the $_guard_account user lookup is ambiguous or malformed"; return 1; }
           printf '%s\n' "$_guard_passwd_all" | awk -F: -v account="$_guard_account" -v expected="$_guard_user_line" \
             '$1 == account { named++; if ($0 == expected) matched++ } END { exit !(named == 1 && matched == 1) }' || {
                 _install_guard_fail "the $_guard_account user lookup disagrees with enumeration"; return 1; } ;;
        2) [ -z "$_guard_user_line" ] && printf '%s\n' "$_guard_passwd_all" | awk -F: -v account="$_guard_account" \
             '$1 == account { found = 1 } END { exit found }' || {
                 _install_guard_fail "the $_guard_account user lookup disagrees with enumeration"; return 1; } ;;
        *) _install_guard_fail "cannot look up the $_guard_account user"; return 1 ;;
    esac
    if [ "$_guard_group_status" -eq 2 ] && [ "$_guard_user_status" -eq 0 ]; then
        _install_guard_fail "the $_guard_account user already exists without its dedicated group"
        return 1
    fi
    if [ "$_guard_group_status" -eq 2 ]; then
        groupadd --system "$_guard_account" || {
            _install_guard_fail "could not create the $_guard_account group"
            return 1
        }
        _guard_group_line=$(getent group "$_guard_account" 2>/dev/null) || {
            _install_guard_fail "cannot read the newly created $_guard_account group"; return 1; }
        _guard_all_groups=$(getent group 2>/dev/null) || {
            _install_guard_fail "cannot enumerate groups after creating $_guard_account"; return 1; }
    fi
    printf '%s\n' "$_guard_group_line" | awk -F: -v account="$_guard_account" \
        'NR == 1 && NF == 4 && $1 == account { ok = 1 } END { exit !(NR == 1 && ok) }' || {
            _install_guard_fail "the $_guard_account group entry is missing or malformed"; return 1; }
    _guard_group_rest=${_guard_group_line#*:}
    _guard_group_rest=${_guard_group_rest#*:}
    _guard_group_gid=${_guard_group_rest%%:*}
    _guard_group_members=${_guard_group_rest#*:}
    case "$_guard_group_gid" in
        ''|*[!0-9]*) _install_guard_fail "the $_guard_account group has an invalid GID"; return 1 ;;
    esac
    [ "$_guard_group_gid" -ne 0 ] || {
        _install_guard_fail "the $_guard_account group must not be group 0"
        return 1
    }
    [ -z "$_guard_group_members" ] || {
        _install_guard_fail "the $_guard_account group already has supplementary members"
        return 1
    }
    if ! printf '%s\n' "$_guard_all_groups" | awk -F: -v account="$_guard_account" -v gid="$_guard_group_gid" -v expected="$_guard_group_line" \
        'NF != 4 || $1 == "" || $3 !~ /^[0-9]+$/ { bad = 1 } $1 == account { named++; if ($0 == expected) matched++ } $3 == gid && $1 != account { other = 1 } END { exit !(NR > 0 && !bad && named == 1 && matched == 1 && !other) }'; then
        _install_guard_fail "the $_guard_account group name or GID is duplicated or missing from group enumeration"
        return 1
    fi

    if [ "$_guard_user_status" -eq 2 ]; then
        # Refuse a pre-existing shared primary group before adding the service user.
        if ! printf '%s\n' "$_guard_passwd_all" | awk -F: -v gid="$_guard_group_gid" \
            '$4 == gid { used = 1 } END { exit used }'; then
            _install_guard_fail "the $_guard_account group is already another user's primary group"
            return 1
        fi
        useradd --system --gid "$_guard_account" --home-dir /nonexistent --no-create-home --shell "$_guard_nologin" "$_guard_account" || {
            _install_guard_fail "could not create the $_guard_account user"
            return 1
        }
        _guard_user_line=$(getent passwd "$_guard_account" 2>/dev/null) || {
            _install_guard_fail "cannot read the newly created $_guard_account user"; return 1; }
        _guard_passwd_all=$(getent passwd 2>/dev/null) || {
            _install_guard_fail "cannot enumerate users after creating $_guard_account"; return 1; }
    fi
    printf '%s\n' "$_guard_user_line" | awk -F: -v account="$_guard_account" \
        'NR == 1 && NF == 7 && $1 == account { ok = 1 } END { exit !(NR == 1 && ok) }' || {
            _install_guard_fail "the $_guard_account user entry is missing or malformed"; return 1; }
    _guard_user_rest=${_guard_user_line#*:}
    _guard_user_rest=${_guard_user_rest#*:}
    _guard_uid=${_guard_user_rest%%:*}
    _guard_user_rest=${_guard_user_rest#*:}
    _guard_user_gid=${_guard_user_rest%%:*}
    _guard_user_rest=${_guard_user_rest#*:}
    _guard_user_rest=${_guard_user_rest#*:}
    _guard_home=${_guard_user_rest%%:*}
    _guard_shell=${_guard_user_rest#*:}
    case "$_guard_uid" in
        ''|*[!0-9]*) _install_guard_fail "the $_guard_account user has an invalid UID"; return 1 ;;
    esac
    [ "$_guard_uid" -ne 0 ] || {
        _install_guard_fail "the $_guard_account user must not have UID 0"
        return 1
    }
    [ "$_guard_user_gid" = "$_guard_group_gid" ] || {
        _install_guard_fail "the $_guard_account user must use its dedicated primary group"
        return 1
    }
    [ "$_guard_home" = /nonexistent ] || {
        _install_guard_fail "the $_guard_account user must have /nonexistent as its home"
        return 1
    }
    case "$_guard_shell" in
        "$_guard_nologin"|/usr/sbin/nologin|/sbin/nologin|/bin/false|/usr/bin/false) ;;
        *) _install_guard_fail "the $_guard_account user must have a non-login shell"; return 1 ;;
    esac

    _guard_id_uid=$(id -u "$_guard_account" 2>/dev/null) || {
        _install_guard_fail "cannot read the $_guard_account UID through id"
        return 1
    }
    case "$_guard_id_uid" in
        ''|*[!0-9]*) _install_guard_fail "id returned an invalid UID for $_guard_account"; return 1 ;;
    esac
    [ "$_guard_id_uid" = "$_guard_uid" ] || {
        _install_guard_fail "id and getent disagree about the $_guard_account UID"
        return 1
    }
    _guard_id_gid=$(id -g "$_guard_account" 2>/dev/null) || {
        _install_guard_fail "cannot read the $_guard_account primary GID through id"
        return 1
    }
    case "$_guard_id_gid" in
        ''|*[!0-9]*) _install_guard_fail "id returned an invalid primary GID for $_guard_account"; return 1 ;;
    esac
    [ "$_guard_id_gid" = "$_guard_user_gid" ] || {
        _install_guard_fail "id and getent disagree about the $_guard_account primary GID"
        return 1
    }

    _guard_passwd_status=$(passwd -S "$_guard_account" 2>/dev/null) || {
        _install_guard_fail "cannot verify the $_guard_account password status"
        return 1
    }
    if ! printf '%s\n' "$_guard_passwd_status" | awk -v account="$_guard_account" \
        'NR == 1 && NF >= 2 && $1 == account && $2 == "L" { ok = 1 } END { exit !(NR == 1 && ok) }'; then
        _install_guard_fail "passwd -S must report exactly one locked (L) status for $_guard_account"
        return 1
    fi

    if ! printf '%s\n' "$_guard_passwd_all" | awk -F: -v account="$_guard_account" -v uid="$_guard_uid" -v gid="$_guard_group_gid" -v expected="$_guard_user_line" \
        'NF != 7 || $1 == "" || $3 !~ /^[0-9]+$/ || $4 !~ /^[0-9]+$/ { bad = 1 } $1 == account { named++; if ($0 == expected && $3 == uid && $4 == gid) matched++ } $3 == uid && $1 != account { other_uid = 1 } $4 == gid && $1 != account { other_gid = 1 } END { exit !(NR > 0 && !bad && named == 1 && matched == 1 && !other_uid && !other_gid) }'; then
        _install_guard_fail "the $_guard_account passwd record is duplicated, missing, inconsistent, or shared with another user"
        return 1
    fi

    _guard_id_groups=$(id -G "$_guard_account" 2>/dev/null) || {
        _install_guard_fail "cannot verify the $_guard_account user's group membership"
        return 1
    }
    if ! printf '%s\n' "$_guard_id_groups" | awk -v gid="$_guard_group_gid" 'NR == 1 && NF == 1 && $1 == gid { ok = 1 } END { exit !(NR == 1 && ok) }'; then
        _install_guard_fail "the $_guard_account user must belong only to its dedicated group"
        return 1
    fi
}

_install_guard_no_symlink_components() {   # absolute path; rejects symlink components and dot traversal
    _guard_path=$1
    case "$_guard_path" in
        /*) ;;
        *) return 1 ;;
    esac
    _guard_rest=${_guard_path#/}
    _guard_current=
    while [ -n "$_guard_rest" ]; do
        case "$_guard_rest" in
            */*) _guard_component=${_guard_rest%%/*}; _guard_rest=${_guard_rest#*/} ;;
            *) _guard_component=$_guard_rest; _guard_rest= ;;
        esac
        [ -n "$_guard_component" ] || continue
        case "$_guard_component" in
            .|..) return 1 ;;
        esac
        _guard_current=$_guard_current/$_guard_component
        [ ! -L "$_guard_current" ] || return 1
    done
}

validate_config_paths() {   # <config directory> <config file>
    _guard_config_dir=$1
    _guard_config=$2
    case "$_guard_config" in
        "$_guard_config_dir"/*) ;;
        *) _install_guard_fail "the config file must be inside the config directory"; return 1 ;;
    esac
    _install_guard_no_symlink_components "$_guard_config_dir" || {
        _install_guard_fail "the config directory or one of its ancestors is a symlink or has dot traversal"
        return 1
    }
    _install_guard_no_symlink_components "$_guard_config" || {
        _install_guard_fail "the config file path or one of its ancestors is a symlink or has dot traversal"
        return 1
    }
    if [ -e "$_guard_config_dir" ] && [ ! -d "$_guard_config_dir" ]; then
        _install_guard_fail "the config directory exists but is not a directory"
        return 1
    fi
    if [ -e "$_guard_config" ] && [ ! -f "$_guard_config" ]; then
        _install_guard_fail "the config path exists but is not a regular file"
        return 1
    fi
}

set_config_permissions() {   # <config directory> <regular config file> <service group>
    _guard_config_dir=$1
    _guard_config=$2
    _guard_account=$3
    validate_config_paths "$_guard_config_dir" "$_guard_config" || return 1
    [ -f "$_guard_config" ] && [ ! -L "$_guard_config" ] || {
        _install_guard_fail "the config file must already be a regular, non-symlink file"
        return 1
    }
    # The validation is followed by path-based chown/chmod. It prevents static symlink redirection,
    # but cannot stop a concurrent privileged process from replacing a path between these operations.
    chown root:"$_guard_account" "$_guard_config" || return 1
    chmod 0640 "$_guard_config" || return 1
}
