# Installing sco-relay on a Linux server

This folder is the install kit for the relay server described in [../README.md](../README.md). One command builds the
relay from this repository, installs it on a plain Linux machine and runs it. The relay is for a LAN or a VPN: it drops
every address outside them (see [Who can join](#who-can-join)). This project runs no relays and keeps no list of them, so
the kit helps you run your own for your own players.

**Not run on a real VM. Not run in game.** What was run is listed under [Evidence](#evidence).

## The command

```
sudo sh server/deploy/install.sh
```

Run it from your own copy of this repository (a clone or an unpacked archive) on the machine that will host the relay. It
needs root, because it makes an account, writes `/etc` and `/usr/local/bin`, and sets up a service.

With systemd, the command installs a service that starts at boot, starts it, and returns. **Without systemd** (a container, a
minimal image) there is no service to start, so the command itself becomes the relay: after printing where the passphrase is,
which port to open and the command for starting the relay again later, it runs the relay in the foreground as the
`sco-relay` account. It does not return. Press Ctrl+C, or send it SIGTERM, to stop the relay; it tells the players and exits 0.

**A safeguard, not a defence.** Root runs cmake and the compiler on the tree, so the installer never builds the tree you ran
it from. It copies the tree (without `.git` and build folders) into a new folder under `/var/tmp` that it makes for this run
(owned by root, closed to everyone else, removed when the installer ends), keeping owners and modes, and checks the copy.
It builds only the copy. The check refuses, and changes nothing:

- a symbolic link, or anything that is neither a regular file nor a folder (the repository has none, and the build would follow
  a link out of the copy);
- a file or folder owned by anyone but root and the user who ran `sudo`;
- a file or folder writable by everyone;
- a file or folder writable by a group, unless the group is root's (gid 0) or the owner's private group: a group named like the
  owning user, with nobody listed in it, that is no other account's primary group. A plain `git clone` made by the user who
  then runs `sudo` passes on Ubuntu and Fedora; a group shared between users does not.

This catches common mistakes: a clone with loose permissions, a stray symlink. It does not protect you from someone who
already controls your copy, because root still builds and runs what is in it. So run the installer only from a copy you made
and trust. This is not an atomic snapshot: no other process may modify the source during copying.
Source hardlinks are copied into the private extraction tree, not linked back to the original inodes;
GNU tar's default here does not preserve source ACLs. The checks describe the extracted copy, not
who could have changed the original tree earlier. Numeric aliases and gshadow membership/passwords
are checked when a source group is treated as private.

### Trust boundary and config filesystem policy

The operator must trust root, the sudo caller and source, executable search path/build tools,
OS/NSS implementations, and root-controlled authorization policy. A locked system UID is not a
proof of provenance or lack of privilege: a reused UID may retain processes, open descriptors,
owned files, capabilities, sudoers/polkit grants or application access. Check its history yourself;
use a clean dedicated account/host if that history is unknown. The installer deliberately does not
attempt to certify all host authorization state or defend against compromised root.

Config checks run before the build directory/package/account mutations and again before file
installation. `/etc` and existing config paths must be root-owned, not group/other writable, not
symlinks, and contain only base POSIX ACL entries; the config must have exactly one hardlink.
GNU `stat` and `getfacl` are required (`getfacl` comes from the `acl` package); missing ACL tooling
fails closed before mutation, not by installing it first. Extended ACLs, including masked named
readers, and default ACLs are refused rather than silently rewritten. In particular, chmod 0640
would otherwise revive a masked named reader; inherited default ACLs could give new config files
unintended readers. Existing secrets already exposed are not made secret again by this check:
rotate them after correcting permissions.

Before building or changing accounts or installed files, the installer atomically creates
`/run/sco-relay-installer.lock`; another instance refuses while that directory exists. The `/run`
parent must be a real root-owned directory, not group/other writable, with only base ACL entries.
On normal exit and handled signals after the script records ownership it attempts to remove the
lock. A cleanup failure, a signal during the brief acquisition window, or an uncatchable termination
such as SIGKILL can leave it behind: first verify no installer is running, then an administrator
may remove the empty lock directory or reboot. The installer never removes an existing lock on
another run's behalf.

Serialize installation with administrative policy/account/config edits. The installer lock does
not lock NSS, sudoers, userdb or filesystem policy and does not eliminate a privileged check/use
race. A concurrent administrator can change these after the last check; that actor is inside this
trust boundary. Failures after installation starts can leave a partially updated installation and
require an operator to inspect it before retrying. No cross-host policy scan is represented as a
guarantee.

## What it does, in this order

1. **Validates existing identities before any mutation**, including temporary build files or package installation.
   Reuse requires `sco-relay` UID/GID inside the configured `SYS_UID_MIN/MAX` and `SYS_GID_MIN/MAX`
   ranges in `/etc/login.defs`, no numeric aliases, matching primary group, no supplementary groups,
   a locked password, `/nonexistent` home and a `nologin` or `false` shell. The group must have no other explicit or
   primary members, no administrators and a locked group password. Unsafe collisions are refused, never repaired.
   Supported NSS policy is exactly `files` or `files systemd` for passwd/group/shadow/gshadow
   (and initgroups when declared). This includes common plain Ubuntu/Debian configurations, **not**
   arbitrary configurations on those distributions. LDAP, SSSD, compat, action clauses (including
   `[SUCCESS=merge]`) and other mixed sources are refused. With `systemd`, only its PID 1 DynamicUser
   provider is allowed: static userdb records and other sockets in the documented userdb directories
   are refused, including userdbd/homed/machined. The complete local records must match NSS resolution
   by both name and number; local shadow/gshadow records must match too.
   System range defaults follow shadow-utils: minima 101, maxima `UID_MIN - 1`/`GID_MIN - 1`
   (regular minima default 1000). Explicit settings must be unique canonical decimal numbers;
   octal/hex and malformed or inverted ranges fail closed. All accepted ranges must stay below 61184,
   the beginning of systemd's DynamicUser range. Changing ranges can therefore block a later upgrade.
   See [login.defs(5)](https://man7.org/linux/man-pages/man5/login.defs.5.html) and
   [nss-systemd(8)](https://www.freedesktop.org/software/systemd/man/latest/nss-systemd.html).
   The check runs again after the build and after account creation, before file installation.
   Refusal after creation can leave the new account/group; there is no rollback or repair.
   Account databases, NSS configuration and userdb providers must not be edited concurrently.
   These shape checks cannot certify arbitrary sudoers/polkit rules, ACLs or previous use
   of a UID: run only on a host whose root-controlled authorization policy you trust.
2. **Copies and checks the tree** (above), so that what is checked is what is built.
3. **Checks the tools.** The build needs cmake 3.20 or newer, a C++20 compiler (`g++`), `make`, and the CA certificates
   (so that cmake can check the certificate of its download; it looks for the bundle where Debian, Ubuntu, Alpine, Fedora,
   RHEL, openSUSE and SLES keep it). On Debian and Ubuntu it installs what is missing with `apt-get`. On any other
   distribution it names what is missing and stops without changing anything; install those with your package manager and
   run it again.
4. **Builds only the relay** from the copy, with cmake's Unix Makefiles generator, in the new folder (every run builds from
   scratch; a few minutes). No tests and no plugin are built. The build downloads the sco-core SDK files that
   [cmake/ScoSdk.cmake](../../cmake/ScoSdk.cmake) and [cmake/ScoCoreSource.cmake](../../cmake/ScoCoreSource.cmake) pin, and
   refuses them unless their SHA-256 matches the one kept in this repository. That download and `apt-get` are the only
   network use; the relay itself opens one UDP port and makes no other connection.
5. **Installs** the program, the account and the config (the table below).
6. **Tells you** where the passphrase is, which UDP port to open, and who can join. It never prints the passphrase.
7. **Runs it.** With systemd running, it enables the service so that it comes back after a reboot, and starts it (a second
   run restarts it, so the new program is the one that runs). Without systemd it runs the relay in the foreground, as said above.

| Path | What | Who |
| --- | --- | --- |
| `/usr/local/bin/sco-relay` | the program | root, mode 0755 |
| `sco-relay` (system account and group) | runs the relay; no password, no login shell, no home folder | |
| `/etc/sco-relay/sco-relay.conf` | the one config file: [the example](../sco-relay.example.conf) with a fresh random passphrase | root, group `sco-relay`, mode 0640 |
| `/etc/systemd/system/sco-relay.service` | the unit ([sco-relay.service](sco-relay.service)); only where systemd is running | root, mode 0644 |

It changes nothing else, and leaves nothing in your copy of the repository or in `/var/tmp`. In particular it does not touch
the firewall.

## The passphrase

The first install writes a random 64-digit hexadecimal passphrase into the `passphrase =` line of
`/etc/sco-relay/sco-relay.conf`. Players type it to join (the plugin takes 12 to 128 bytes). Only root and the `sco-relay`
account can read the file, and the installer does not print the passphrase. Read it with `sudo grep '^passphrase' /etc/sco-relay/sco-relay.conf`
and send it to your players over a channel you trust.

Running the installer again keeps the config, and with it the passphrase, so your players do not need a new one. To change
it, edit the file and restart the relay.

Every other setting (the port, the player limit, the server name, the timers, nearby-only forwarding) is in the same file,
one comment line each, and changing one never needs a rebuild.

## The port and the firewall

The relay listens on one UDP port, 64091 unless `port` in the config says otherwise. Open that port in your firewall for the
network your players use (your LAN, or your VPN), never for the whole internet. The installer does not open it for you.

## Who can join

The relay applies the LAN rule of [CONTRIBUTING.md](../../CONTRIBUTING.md) (guardrail 2) through the kit's `ScopedTransport`:
it answers loopback and the private ranges, `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16` and `169.254.0.0/16`, and drops
every datagram from any other address. That has a plain consequence for a rented server:

- A server reached over its public address, with players connecting from their home internet addresses, **does not work**.
  The relay drops those addresses, and the players' own sc-offline blocks every public address too (found by reading its
  source, not run in game).
- Put the server and the players on a **VPN whose addresses are private** (a WireGuard network, for example, with addresses in
  `10.x.x.x`), and have the players join the server's VPN address.
- If your VPN uses other addresses, name them in `allowed_addresses` in the config (each range `/8` or narrower), then restart
  the relay. The relay then accepts them; what the players' game does with them is the next point.
- Players' sc-offline currently blocks CGNAT ranges such as `100.64.0.0/10`, the range Tailscale uses (found by reading
  its source, not run in game). Until that changes, a VPN that hands out those addresses may not work for them even when the
  relay allows the range. This is about the players' side; the relay's `allowed_addresses` cannot fix it.

Traffic is signed with the passphrase but **not encrypted**. Players who need privacy use a VPN, as for the rest of the traffic.

## Running it

With systemd:

| To | Run |
| --- | --- |
| see whether it runs | `systemctl status sco-relay` |
| read the log | `journalctl -u sco-relay -f` |
| apply a config change | `sudo systemctl restart sco-relay` |
| stop it | `sudo systemctl stop sco-relay` (the relay tells the players, logs a last stats line and exits 0) |
| start it again | `sudo systemctl start sco-relay` |

Upgrade by updating your copy of the repository and running the installer again. Players and relay must use the same
multiplayer protocol generation, so upgrade them together; the generation and what it covers are in the plugin's
[protocol instructions](../../plugins/multiplayer/README.md#co-presence-not-shared-simulation).

The unit retries a crash after five seconds. Three failures within one minute make systemd refuse to start the service
again ("start request repeated too quickly"), so that a bad config does not loop. That lasts until the minute has passed
or until you run `sudo systemctl reset-failed sco-relay`. So fix the file, then run `sudo systemctl reset-failed sco-relay`
and `sudo systemctl restart sco-relay`. (The installer does the `reset-failed` itself.) A clean stop is not restarted.

Without systemd, the installer's run of the relay is the foreground run described above. To start the relay again later
without running the installer, use the command the installer printed. Changing to the `sco-relay` account needs root, so it
starts with `sudo` (leave the `sudo` off in a root shell). For example:

```
sudo setpriv --reuid=sco-relay --regid=sco-relay --init-groups /usr/local/bin/sco-relay /etc/sco-relay/sco-relay.conf
```

It also runs until you press Ctrl+C or send it SIGTERM, then exits 0. If systemd is installed but not running (a chroot, an
image build), run the installer on the machine once it has booted, or accept the foreground run.

## Remove it

Not covered by the evidence below, and plain commands:

```
sudo systemctl disable --now sco-relay
sudo rm /etc/systemd/system/sco-relay.service /usr/local/bin/sco-relay
sudo systemctl daemon-reload
sudo rm -r /etc/sco-relay          # this deletes the passphrase
sudo userdel sco-relay             # the group goes with it
```

## What is not here

- **No container image.** The relay runs as a plain service, or in the foreground. The kit builds on the machine that runs it.
- **No release binary.** sc-mp publishes none, so the kit builds from source.
- **No Windows installer.** On Windows, build the relay and start it by hand (see [../README.md](../README.md), "Run").
- **No firewall changes, no service manager other than systemd.**

## Evidence

The kit was run on a build machine, from a committed copy of the repository, by a driver script that is not part of this
repository. In fresh `ubuntu:24.04` and `debian:12` containers (no systemd) an ordinary user ran `sudo sh server/deploy/install.sh`
once, from a clone-like copy with group-writable files in a private group whose number differs from the user's number. That
one command installed the missing tools, built and installed the relay and stayed in the foreground as it. The driver checked
that the relay's own process, running as `sco-relay`, held the UDP port and logged that its session is up, and joined it with
a probe player using the generated passphrase (a wrong passphrase was refused). SIGTERM ended the command with exit status 0
and released the port. The relay was then started again with the printed command, run through `sudo` by that user, and the
installer was run a second time, which kept the passphrase. The driver also checked that the installer refuses, before it
installs anything, a copy with a symbolic link, a file writable by everyone, a folder writable by everyone, a file writable
by a group that is not its owner's private group (a shared group whose number equals the user's, a foreign group, and a group
named like the owner that lists a member) and a file owned by another user; and that a `build-relay` folder reached through a
symlink, holding a CMake cache, is never read. In an `ubuntu:24.04` container running systemd, with the build tools already
in the image so that nothing was installed while it ran, it did the same through the service, and the enabled service came
back after the container was restarted.

That is all. It was **not run on a real VM**, and nothing here was **run in game**: no Star Citizen process was involved.
The sandbox lines of the unit were exercised in a privileged container only. The `su` fallback of the foreground run (for a
machine without `setpriv`) and distributions other than Ubuntu and Debian were not run. The identity preflight has a separate hermetic regression test:
`python3 server/tests/test_install_identity.py`. It runs the actual installer with synthetic identity databases and a
closed command-stub PATH. Every mutating command is a failing trap; refused cases must leave the mutation log absent
and a sentinel unchanged. Safe identities stop at the first temporary-directory command. This does not exercise a
real install, the post-build recheck or privilege dropping. CTest registers `relay_install_identity` when Python 3 is
available. A separate full-shell hermetic suite relocates absolute paths into a temporary root:

```
python3 -m unittest server.tests.test_install_identity server.tests.test_install_full server.tests.test_install_source
```

The followup ran 42 tests, including files-only/systemd and foreground success, config-preserving
upgrade, private-source-group numeric aliases, ACL refusal and existing config hardlinks. The
masked-reader/default-ACL and hardlink cases returned success before the fixes and refusal after.
The ACL output and privileged metadata are fixtures, not an actual Linux ACL/ownership experiment.
These tests do not establish real NSS/ACL utility compatibility, service activation or a privileged
installation of this followup. The full installer/container workflow still needs root and network
and is not a CTest test.
