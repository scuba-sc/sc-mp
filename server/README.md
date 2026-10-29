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
generation (currently 2). The generation is an application compatibility contract, not a binary
version; different builds implementing the same contract are compatible. The relay announces its
generation and role on `multiplayer.protocol` alongside its roster and forwards player announcements
on that reliable channel, including in nearby-only mode. A current plugin excludes peers without
matching evidence and shows their pending or incompatible status. The existing sco.net handshake
cannot refuse a peer based on this application generation or remove an individual peer; an older
client can still establish a transport session and report it connected. See the plugin's
[protocol instructions](../plugins/multiplayer/README.md) for the local gate and its limits.

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
[cmake/ScoSdk.cmake](../cmake/ScoSdk.cmake); today SDK 2.4.0 release candidate 2, `sdk-v2.4.0-rc.2`), refuses it
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
UDP port for your LAN or VPN network only. How the plugin joins and what it shows: the "Joining a relay server"
section of [plugins/multiplayer/README.md](../plugins/multiplayer/README.md).

## Tests

Five tests, registered with CTest, run on both Windows and Linux with the rest of sc-mp:

| Test | What it proves |
| --- | --- |
| `test_server` | the unit tests: the config reader, the address rule, the wire bytes (golden vectors for `multiplayer.relay`, `multiplayer.near_up` and `multiplayer.near.<slot>`), the roster, nearby-only forwarding, the session over the kit's in-memory test network, the run loop, and a real session over loopback UDP |
| `sco_relay_program` | the real program: how it starts, what it says for a bad config or a taken port, and that it exits 0 after SIGTERM or SIGINT |
| `net_boundary_checks` | that the checks below fire when they should, on altered copies of the kit's files |
| `relay_no_game_code` | that the server holds no game code and touches the kit's network library in one file |
| `relay_no_game_code_checks` | that the source gate permits the guarded Windows SDK headers only at their use sites and rejects other Windows headers or parent paths |

`test_server` includes `tests/net_mem.h` from the extracted kit (a seeded lossy in-memory network); the server
itself does not.

## The sco-core boundary

The server is built on the kit's network library, and on its class `Core`, which sco-core's author never promised to
keep stable. So the server touches the library in one place only.

- `server/net_boundary.cpp` is the only file of `server/` that includes the library's headers or names its types.
  Its header, `server/net_boundary.h`, gives the rest of the server small types of its own and includes nothing from
  sco-core. The relay's own messages come from `plugins/multiplayer/relay_wire.h`, which includes nothing either.
- `server/tools/check-no-game-code.sh` (the test `relay_no_game_code`) reads the production set only,
  `server/*.cpp`, `server/*.h` and `cmake/ScoCoreSource.cmake`. It fails when any file but the boundary file
  includes one of the library's headers (the `sco/net/` ones or `sc_net.h`) or writes `sco::net::`, even in a
  comment; when an `#include` is not one of our own `server/<name>` headers (a header in a subfolder such as
  `server/tests/` is not one), `plugins/multiplayer/relay_wire.h` (the only file outside `server/` that is allowed), or a
  standard or POSIX header (plus `windows.h` in `main.cpp` under `_WIN32` and `sal.h` in `log.h` under `_MSC_VER`;
  other Windows headers and uses outside those files need explicit allowances; a name with `..` in it is never
  allowed, even `server/../...`); when the kit was not configured with `SCO_GAME_SC=OFF`; and when the program links
  anything but the server library, the kit's network library and the two socket files, or those libraries hold other
  objects. It finds an `#include` wherever it hides: behind a comment between the `#` and `include`, after a
  backslash-newline splice, or spelt with the `%:` digraph.
  The link is read twice: from what CMake declares for each target, including the libraries a target passes on to
  whatever links it (`INTERFACE_LINK_LIBRARIES`), and from the program's real link line, the `link.txt` that Makefile
  generators write. The check fails when that file is missing: Ninja writes none, so the relay is checked with a
  Makefile generator (the default on Linux). It splits the words of the link line on commas, so a library inside a
  `-Wl,...` list counts like any other, and it fails on an `@response` file, which would hide the words from it.
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
and its tests, the 50-player test build, the deploy kit (install script, systemd unit, container image), the
measurement tools, and the long architecture and report documents. Windows relay builds and tests are already
supported.

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
- `tests/`, `tools/`: the tests and the two checks above.
