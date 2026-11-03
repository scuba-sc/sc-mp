# Per-user Windows staging for sco-relay

This kit stages an already-built Windows relay in a directory you choose. Run
it from a normal PowerShell session under the Windows account that will own the
files. It does not need administrator rights, build or start the relay, create
a service or scheduled task, change accounts or ACLs, or modify firewall or
network policy.

The script requires an explicit executable path and an existing destination
directory. It copies the executable to `sco-relay.exe` and, only when no config
exists, copies `server/sco-relay.example.conf` to `sco-relay.conf`. The new
config still has the example passphrase placeholder; edit it locally and set a
private passphrase of 12 to 128 bytes and `max_players` before starting the
relay. The kit does not generate or display a passphrase or read the contents
of an existing user config. Choose a directory your account owns, such as a
folder under your user profile; the script does not inspect or change that
directory's ACLs.

## Build and stage

From a Developer PowerShell with CMake and the Visual Studio C++ workload
available, build the relay from the repository root:

```powershell
cmake -S . -B build -DSC_MP_BUILD_RELAY=ON
cmake --build build --config Release --target sco-relay
```

The Visual Studio generator places the executable at
`build/server/Release/sco-relay.exe`. Create the destination folder once,
choosing its location explicitly, then stage the built executable:

```powershell
$relayDirectory = Join-Path $env:USERPROFILE 'sc-relay'
if (-not (Test-Path -LiteralPath $relayDirectory)) {
    New-Item -ItemType Directory -Path $relayDirectory | Out-Null
}

.\server\deploy\windows\stage_user_relay.ps1 `
    -RelayExecutable '.\build\server\Release\sco-relay.exe' `
    -InstallDirectory $relayDirectory
```

Open `$relayDirectory\sco-relay.conf` in a local editor and replace the placeholder passphrase. Review the port, player limit, and `allowed_addresses` against the relay's LAN/VPN rules. This kit leaves those settings as you wrote them; it does not widen them or configure a firewall.

Change `$relayDirectory` in the example if you selected a different folder.

## Updates and rollback

Stop the foreground relay before updating. Run the same staging command with
the new built executable. The script leaves an existing `sco-relay.conf`
byte-for-byte untouched and replaces only `sco-relay.exe`. It stages temporary
files beside the destination so replacement stays on the same volume. A
locked destination executable or a filesystem that cannot replace it causes
the update to fail; it does not fall back to truncating the existing
executable. If first-time staging fails after creating the example config, the
script removes that newly created config and its temporary files. If a
replacement fails after the old executable was backed up, it attempts an
atomic restore and reports the backup path if restore fails. A warning may
report a previous-executable backup left by a cleanup failure.

If replacement fails because the relay is still running, stop it with Ctrl+C
in its foreground console and retry. Existing config files and pre-existing
executables are not deleted by a failed first stage. Review any path the script
reports as a retained rollback backup before removing it.

## Foreground start and clean stop

After editing the config, run the relay in the foreground from PowerShell:

```powershell
$relayDirectory = Join-Path $env:USERPROFILE 'sc-relay'
$relayExecutable = Join-Path $relayDirectory 'sco-relay.exe'
$relayConfig = Join-Path $relayDirectory 'sco-relay.conf'
& $relayExecutable $relayConfig
```

The console displays the relay's operational log. Press **Ctrl+C** in that
same console to request a clean stop. The relay ends its session, writes its
final stats line, and exits with status 0. Avoid force-ending the process
through Task Manager, which can skip that clean shutdown. To start it again,
repeat the foreground command.

The relay forwards authenticated messages; it has no game world or simulation
authority. Existing Windows process tests use synthetic clients and loopback
transport. Neither those checks nor this staging kit establish two-PC LAN
behavior, firewall behavior, or in-game operation.
