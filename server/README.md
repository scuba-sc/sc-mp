# sco-relay: the relay server

A small server for the `multiplayer` plugin. It hosts one sco.net session that players join and passes their
messages on to each other. It is **not a player**: it gets no ghost, holds no game code and no world, and the
plugin's roster message (`multiplayer.relay`) says so. It is for a LAN or a VPN.

**Not run in game.** What is tested is the server itself: unit tests, the real program, real UDP on loopback, and
the boundary checks below. No test ran it together with the game. A green CI run is a build and a synthetic test.

The relay lives next to the plugin so that the program and the wire it speaks sit in one repository. The two share one
header, [`plugins/multiplayer/relay_wire.h`](../plugins/multiplayer/relay_wire.h) (standard headers only): the server
includes it, the plugin includes it, and neither has a copy. A new layout gets a new wire version, in a change of its
own; an old version is never reinterpreted.

## Synchronized upgrades

Upgrade the relay and all players together to builds implementing the same multiplayer protocol
generation (which one it is, and what it covers, is in the plugin's
[protocol instructions](../plugins/multiplayer/README.md#co-presence-not-shared-simulation)). The generation is an application compatibility contract, not a binary
version; different builds implementing the same contract are compatible. The relay announces its
generation and role on `multiplayer.protocol` alongside its roster and forwards player announcements
on that reliable channel, including in nearby-only mode. A current plugin excludes peers without
matching evidence and shows their pending or incompatible status. The existing sco.net handshake
cannot refuse a peer based on this application generation or remove an individual peer; an older
client can still establish a transport session and report it connected. The protocol instructions
describe the local gate and its limits.

## Guardrails, and where the server meets them

These are the six rules of [CONTRIBUTING.md](../CONTRIBUTING.md), "Guardrails any relay must meet".

1. **Offline only.** The server opens one UDP port and answers the players that talk to it. It makes no other
   connection: none to Star Citizen's servers or online services, none to a telemetry or update service, no
   accounts (the passphrase is a shared secret, not an account). It does nothing to the game, so there is nothing
   to bypass.
2. **The LAN rule, through `ScopedTransport`.** The socket (`udp::Socket`) is wrapped in the kit's `ScopedTransport`
   before `Core` sees it (`server/net_boundary.cpp`). The default `Scope` is the LAN rule: loopback and the private
   ranges. The operator can add ranges with `allowed_addresses` (each `/8` or narrower), and nothing switches the
   rule off. A datagram from outside it is dropped and counted, and nothing is sent to an address outside it. The
   socket binds every IPv4 interface, which is why the rule matters: a host built without `ScopedTransport` would
   accept datagrams from, and send to, any address.
3. **No unauthenticated remote commands between peers.** Traffic is authenticated with the passphrase but not
   encrypted. Players who need privacy connect through a VPN. The server reads no command from a player.
4. **Public SDK surface only.** The server is built from the published SDK's `core-source` kit, downloaded and
   hash-checked by [cmake/ScoCoreSource.cmake](../cmake/ScoCoreSource.cmake). It uses `Core`, `udp::Socket` and
   `ScopedTransport` and the wire constants of the kit's network headers, in one file (see
   [The sco-core boundary](#the-sco-core-boundary)). Nothing of the kit is copied into this repository.
5. **The relay is never a player.** It is peer 1 of its session, registers no player channel, and sends
   `multiplayer.relay`, whose roster lists the players only.
6. **Evidence.** The tests are synthetic. "Not run in game" stays true until a recorded in-game run says otherwise.

## Build

Windows and Linux. The relay uses the published kit's platform-specific UDP
transport. Windows uses console control or an existing named stop event; POSIX
uses its existing signal handlers. Other platforms leave the relay disabled.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The root build turns the relay on on Windows/Linux when SDK fetching is enabled (`SC_MP_BUILD_RELAY`;
`-DSC_MP_BUILD_RELAY=OFF` leaves it out). It then downloads `sco-sdk-core-source-<version>.zip` from the same
sco-core release as the SDK archive (`SCO_SDK_VERSION` and `SCO_SDK_RELEASE_TAG` in
[cmake/ScoSdk.cmake](../cmake/ScoSdk.cmake); SDK 2.4.0 stable release, `sdk-v2.4.0`), refuses it
unless its SHA-256 matches `SCO_CORE_SOURCE_SHA256` in `cmake/ScoCoreSource.cmake`, unpacks it under the build folder
and adds it with `add_subdirectory` (`EXCLUDE_FROM_ALL`, the Star Citizen game pack off, the kit's own tests off).
Only the kit's network library is built, plus `src/net/udp_win.cpp` on Windows or `src/net/udp_posix.cpp` on Linux and
`src/net/scope.cpp`, compiled as they are. For an offline build, put the release assets in a folder and pass
`-DSCO_SDK_ARCHIVE_DIR=<folder>` (it needs `sco-sdk-<version>.zip` and `sco-sdk-core-source-<version>.zip`); the hash
is still checked. To move to a newer release, change `SCO_SDK_VERSION`, `SCO_SDK_RELEASE_TAG`,
`SCO_SDK_SHA256` and `SCO_CORE_SOURCE_SHA256` together (from the release `SHA256SUMS`), then read
[The sco-core boundary](#the-sco-core-boundary).

The program is `build/server/sco-relay` on a single-config POSIX build, or
`build/server/Release/sco-relay.exe` with the Visual Studio generator.

The native Windows suite passes five registered tests, including the real relay
child with two authenticated loopback clients, routing, wrong-passphrase refusal
and clean event-driven shutdown. This is local process/transport evidence, not
two-PC LAN or game acceptance. POSIX-only shell boundary checks remain separate.

## Run

```
cp server/sco-relay.example.conf my.conf     # set passphrase (12 to 128 bytes) and max_players, at least
build/server/sco-relay my.conf
```

The example file lists every setting with one comment line each. It refuses to start while the passphrase is still
the placeholder. The server takes one seat of the session, so 15 players is the most (sco-core's limit of 16 peers
counts the server). It stops on SIGTERM or Ctrl+C: it tells the players, logs a last stats line and exits 0. Exit
status 1 means it could not start (the message says why), 2 means it was started the wrong way.

If your players join through a VPN, add its range to `allowed_addresses`. If the machine has a firewall, allow the
UDP port for your LAN or VPN network only. What players' sc-offline accepts is said under
[Install on a Linux server](#install-on-a-linux-server). How the plugin joins and what it shows: the "Joining a relay
server" section of [plugins/multiplayer/README.md](../plugins/multiplayer/README.md).

## Install on a Linux server

```
sudo sh server/deploy/install.sh
```

Run from your own copy of this repository on a plain Linux machine, it copies the tree into a new root-owned folder and
checks the copy as a safeguard against common mistakes (it refuses symbolic links, files owned by anyone but root and the
user who ran `sudo`, and files writable by everyone or by a group that is not root's or the owner's private group; it
does not defend against someone who already controls your copy, so run it only from a copy you trust). Then it installs the
missing build tools (on Debian and Ubuntu; elsewhere it names them), builds only the copy's relay with the default Unix
Makefiles generator from the pinned SDK download (it makes no other network call), and installs it as
`/usr/local/bin/sco-relay` with an account of its own. It makes `/etc/sco-relay/sco-relay.conf` (mode 0640, owner root,
group `sco-relay`) with a fresh random passphrase that it never prints and keeps when you run it again. With systemd it
starts the relay as a service that comes back after a reboot and returns. Without systemd the same command stays in the
foreground as the relay (Ctrl+C or SIGTERM stops it, exit 0), after printing the command for starting it again later.
[deploy/README.md](deploy/README.md) lists exactly what is checked, every step, every file it changes, and how to remove it.

- **Passphrase.** The `passphrase =` line of `/etc/sco-relay/sco-relay.conf`. Give it to your players.
- **Port.** UDP 64091 (the `port` setting). Open it in your firewall for your LAN or VPN network only; the installer
  changes no firewall.
- **Who can join.** The LAN rule (guardrail 2) applies: the relay drops every address except loopback and the private
  ranges. A rented server reached over public addresses therefore needs a VPN whose addresses are private, or
  `allowed_addresses` naming the ranges the players come from. The relay's allow list is separate from sc-offline's
  plugin and admission policy. Current sc-offline source includes shared `100.64.0.0/10` in its network ranges, but its
  product still refuses native startup; neither that source policy nor relay tests establish that players can host or
  join in game. See the admission and activation status in the multiplayer README.

No container image is shipped. **Not run on a real VM. Not run in game.** The kit was run from a committed copy in fresh
`ubuntu:24.04` and `debian:12` containers and in an `ubuntu:24.04` container running systemd: install, start, a join with
the generated passphrase, stop, start again, a second install that keeps the passphrase, and (with systemd) a restart of
the container with the enabled service coming back.

## Tests

The relay tests are registered with CTest alongside the rest of sc-mp:

| Test | What it proves |
| --- | --- |
| `test_server` | the unit tests: the config reader, the address rule, the wire bytes (golden vectors for `multiplayer.relay`, `multiplayer.near_up` and `multiplayer.near.<slot>`), the roster, nearby-only forwarding, the session over the kit's in-memory test network, the run loop, and a real session over loopback UDP |
| `sco_relay_program` | the real program: how it starts, what it says for a bad config or a taken port, and that it exits 0 after SIGTERM or SIGINT |
| `net_boundary_checks` | that the checks below fire when they should, on altered copies of the kit's files |
| `relay_no_game_code` | that the server and shared `plugins/multiplayer/relay_wire.h` hold no game code, and that the server touches the kit's network library in one file |
| `relay_no_game_code_checks` | that the source gate permits the guarded Windows SDK headers only at their use sites and rejects other Windows headers or parent paths |
| `relay_no_implicit_link_directives` | that only `#pragma once` is allowed in production sources; direct and macro-expanded `#pragma`, `%:pragma`, `__pragma`, `_Pragma` and token-pasted pragma inputs are rejected on Windows and Linux |
| `relay_compiler_environment_checks` | that implicit compiler paths/options and changed CMake compiler/linker flags are rejected while standard MSVC `INCLUDE` and `LIB` paths stay within the selected compiler, Windows SDK, Visual Studio auxiliary and unit-test library, and installed .NET Framework SDK roots |
| `relay_msvc_pragma_link` | on MSVC, that `_Pragma("comment(lib, ...)")` creates a library search record, and what the compiler reports for a macro-expanded `#pragma` name |
| `relay_install_guard_checks` | mocked account-identity and config-path guard cases using temporary files; the installer is not executed and system accounts are not changed |

`test_server` and `relay_no_implicit_link_directives` run on Windows and Linux. The process and binary/link-boundary
tests run on Linux except for `sco_relay_windows_process`, which runs on Windows.

`test_server` includes `tests/net_mem.h` from the extracted kit (a seeded lossy in-memory network); the server
itself does not.

## The sco-core boundary

The server is built on the kit's network library, and on its class `Core`, which sco-core's author never promised to
keep stable. So the server touches the library in one place only.

- `server/net_boundary.cpp` is the only file of `server/` that includes the library's headers or names its types.
  Its header, `server/net_boundary.h`, gives the rest of the server small types of its own and includes nothing from
  sco-core. The relay's own messages come from `plugins/multiplayer/relay_wire.h`, which includes nothing either.
- `server/tools/check-no-game-code.sh` (the test `relay_no_game_code`) reads the production set only,
  `server/*.cpp`, `server/*.h`, the shared `plugins/multiplayer/relay_wire.h` and `cmake/ScoCoreSource.cmake`. It fails when any file but the boundary file
  includes one of the library's headers (the `sco/net/` ones or `sc_net.h`) or writes `sco::net::`, even in a
  comment; when an `#include` is not a same-directory server header, the shared wire header at its fixed relative
  path, or a standard or POSIX header (plus `windows.h` in `main.cpp` under `_WIN32` and `sal.h` in `log.h` under
  `_MSC_VER`; other Windows headers and uses outside those files need explicit allowances); an SDK header that is
  shadowed before the pinned kit path; any explicit product include path other than the kit's exact `include/` folder;
  or an actual compile command that adds another include path, a forced include, a PCH, a compiler plugin or a response
  file. A repository-root include directory is absent from the product targets, so nested files such as
  `bits/stl_vector.h` cannot shadow transitive system headers. A name with `..` is refused except the shared wire
  header's one fixed relative include.
  The server's compiled source list must match the reviewed
  `server/*.cpp` set, not just archive member names. The gate also checks that the kit was configured with
  `SCO_GAME_SC=OFF`, the program links only the server library, the kit's network library and the two socket files,
  and the network archives contain only their expected objects. It finds an `#include` wherever it hides: behind a
  comment between the `#` and `include`, after a
  backslash-newline splice, or spelt with the `%:` digraph. The adjacent source check also refuses direct `#pragma`
  and `%:pragma` linker comments, `__pragma`, `_Pragma`, and macro token pasting that could synthesize one.
  Environment overrides such as `CPATH`, `CFLAGS`, `CXXFLAGS`, `LDFLAGS` and `LIBRARY_PATH`, custom CMake toolchain or
  rules hooks, and compiler launchers are refused. Generic and per-configuration CMake compiler/linker flags must
  match the selected CMake platform defaults at configure time and stay unchanged through the build. The standard
  MSVC `INCLUDE` and `LIB` lists are allowed only within the selected compiler and Windows SDK version directories
  or the installed .NET Framework SDK root. CMake validates those paths at configuration. The Visual Studio
  generator uses project include properties; Microsoft's compiler documentation says it ignores `INCLUDE` in that
  build mode, so the build-time check uses the validated snapshot instead of MSBuild's changed child environment
  ([include-search documentation](https://learn.microsoft.com/en-us/cpp/preprocessor/hash-include-directive-c-cpp?view=msvc-170)). LINK searches its working directory before `LIB`. A configure-time MSVC probe checks that plain and empty-component `LIB` forms preserve default-library resolution from that already-searched directory. The safety rule for dropping empty components relies on the documented search order rather than claiming the probe distinguishes a repeated working-directory lookup. The build-time gate discards empty `LIB`
  components only for the probed Visual Studio generator and checks every remaining path against the exact versioned
  compiler and SDK roots selected at configuration, together with the selected Visual Studio installation's
  `VC/Auxiliary/VS/lib` and `VC/Auxiliary/VS/UnitTest/lib` subtrees, even when configure did not export `LIB`; an
  empty list reduces search paths. Other
  generators must keep the environment unchanged through the build. The Windows configure step removes inherited
  `LIB` only during CMake's compiler-identification probes, then restores and validates it before generating product
  targets. LINK's search order is documented [here](https://learn.microsoft.com/en-us/cpp/build/reference/dot-lib-files-as-linker-input?view=msvc-170). On Linux, compile input is checked from CMake's generated `compile_commands.json`,
  which includes the global, target and source-specific compiler options used for the relay and its two kit libraries.
  The Windows Visual Studio generator does not produce that database in this workflow; Windows instead runs the
  environment/default-flag guards, source-level pragma scanner, and the MSVC `_Pragma` linker-record probe. The link is read twice: from what
  CMake declares for each target, including the libraries a target passes on to whatever links it
  (`INTERFACE_LINK_LIBRARIES`), and from the program's real link line, the `link.txt` that Makefile generators write.
  The check fails when either generated file is missing: Ninja writes no `link.txt`, so the relay is checked with a
  Makefile generator (the default on Linux). It splits the words of the link line on commas, so a library inside a
  `-Wl,...` list counts like any other, and it fails on an `@response` file, which would hide the words from it. It
  also refuses linker scripts, linker plugins and input remapping, library search paths, raw `.obj` files, duplicate or
  substituted archives, unknown forwarded linker options and positional inputs.
- `server/tools/check-net-boundary.sh <extracted kit>` needs no compiler. It checks the declarations the boundary
  uses, the constants, the lines of behaviour no compiler can see (what `Core` relays and to whom, who checks the seat
  limit, the private ranges, which interface a socket binds), and the blob id of each kit file the server compiles
  or includes (15 files, line endings ignored: the 14 the program compiles or includes, and `tests/net_mem.h`,
  which only `test_server` includes). The `static_assert`s in `server/net_boundary.cpp` say the same at
  build time.
- `server/tests/test_net_boundary_checks.sh` (the test `net_boundary_checks`) proves that those checks fire, on altered
  copies of the kit's files, and that a changed session limit or `Core::Host` does not compile.
- The session limit is one definition, `SCO_RELAY_EXPECTED_SESSION_LIMIT`, which is 16: it counts the server, so the
  shipped server takes 15 players.

When `check-net-boundary.sh` or the build fails after a kit change, do this in order:

1. Read the message. It names what moved: a declaration, a constant, a line of behaviour or a file.
2. Read the change in sco-core between the two releases, for the file it names.
3. Change `server/net_boundary.cpp` if the server needs it. Nothing else in `server/` should need to change.
4. Change the matching line or blob id in `server/tools/check-net-boundary.sh` in the same commit that moves
   `SCO_SDK_VERSION`, `SCO_SDK_RELEASE_TAG` and the two hashes.
5. Run `ctest`. Only when it passes, move on.

## What is not here

These stay in the contributor's standalone relay and can come in later, separate pull requests: the fake-player rig
and its tests, the 50-player test build, the measurement tools, and the long architecture and report documents.
The install kit for Linux (an install script and a systemd unit) is in [deploy/](deploy/README.md); there is no
container image, no release binary (the kit builds from source on the machine that runs the relay) and no Windows
installer. Windows relay builds and tests are already supported.

## Files

- `main.cpp`: the program. Reads the config file, opens the UDP socket, loops until its platform shutdown event
  (console control on Windows, SIGINT or SIGTERM on POSIX), stops the session and exits 0.
- `config.h`, `config.cpp`: the settings and the reader of the config file. A bad file stops the server and the message
  names the line.
- `address_rule.h`, `address_rule.cpp`: which addresses may talk to the server: the private ranges plus `allowed_addresses`.
- `net_boundary.h`, `net_boundary.cpp`: the boundary with the kit's network library.
- `traffic.h`, `traffic.cpp`: counting traffic.
- `session.h`, `session.cpp`: hosts the sco.net session, sends `relay.info` and the roster and repeats them, hands
  nearby poses on. The library does the relaying.
- `roster.h`, `roster.cpp`: who is in the session, and in which slot (a slot is reused; its generation goes up each
  time someone new takes it).
- `nearby.h`, `nearby.cpp`: nearby-only forwarding, off by default.
- `relay_info.h`, `relay_info.cpp`: the bytes of `relay.info`, the first message of the server's own.
- `run_loop.h`, `run_loop.cpp`: one pass of the loop and the stats line.
- `log.h`, `log.cpp`: the operator log.
- `sco-relay.example.conf`: every setting, one comment line each.
- `deploy/`: the install kit for Linux: `install.sh`, the systemd unit `sco-relay.service`, and its README.
- `tests/`, `tools/`: the tests and the two checks above.
