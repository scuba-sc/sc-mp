# sc-mp

Private co-presence for [sc-offline](https://github.com/scuba-sc/sc-offline): an optional
`multiplayer` plugin that lets players who all run sc-offline see each other as ghosts in their own
offline games, over a LAN or VPN.

This module used to live in the sc-offline repository. It now has its own home so it can move at its
own pace; sc-offline does not ship it.

## Status

Development code. **It has not been run in game.** The plugin builds against the sco-core SDK's public
headers only (pinned to the latest published SDK 2.4.0 release candidate 2,
[`sdk-v2.4.0-rc.2`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2),
whose source commit is `844e136ccb280ad8cb43f73bc43c9447e8f6799d`); the headers it needs, such as
`sc_private_session.h`, have shipped since SDK 2.2.0. Joining a session needs the
host to positively admit it as offline. Treat everything here as unverified until a release says
otherwise.

## Layout

The files keep the paths they had in sc-offline, so history and patches carry over unchanged:

| Path | What it is |
| --- | --- |
| `plugins/multiplayer/` | the plugin: `multiplayer.cpp`, `multiplayer_wire.h`, `plugin.ini`, `CMakeLists.txt`, and its own `README.md` (protocol, settings, consent) |
| `tools/multiplayer_workflow_test.cpp` | the module's workflow test |
| `server/` | the Windows/Linux relay server (`sco-relay`): a program that carries a session and is never a player, with its own `README.md`, tests and checks. It shares `plugins/multiplayer/relay_wire.h` with the plugin |
| `cmake/` | `ScoSdk.cmake` (the SDK archive) and `ScoCoreSource.cmake` (the SDK's `core-source` kit, for the relay) |

## Build

The repo root is a standalone CMake project. It downloads the published SDK archive `sco-sdk-2.4.0.zip`
from the latest sco-core release [`sdk-v2.4.0-rc.2`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2)
(SDK 2.4.0 release candidate 2; the final `sdk-v2.4.0` tag is still pending), refuses it unless its SHA-256 matches the pin in
[cmake/ScoSdk.cmake](cmake/ScoSdk.cmake), and builds against the extracted `include/`. It never reads a
sibling sco-core checkout.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`ctest` runs the module workflow test (`tools/multiplayer_workflow_test.cpp`, synthetic services, no sockets,
no game) and, on Windows, the SDK's `sco-plugin-check` on the plugin folder. With the default
`SC_MP_BUILD_RELAY=ON`, Windows and Linux also build the relay server and run its five tests (below).
On Linux, use the `Unix Makefiles` generator for the full suite: the relay's no-game-code checks inspect
the actual link line in `link.txt`, which Ninja does not generate.
For an offline Windows or Linux build, put the release assets in a directory and pass
`-DSCO_SDK_ARCHIVE_DIR=<dir>`; the hash is still checked. The directory needs both
`sco-sdk-<version>.zip` and `sco-sdk-core-source-<version>.zip` (the relay's kit), or pass
`-DSC_MP_BUILD_RELAY=OFF`. On other platforms the relay defaults off. CI:
[.github/workflows/build.yml](.github/workflows/build.yml) (Windows and Linux). To move to a newer SDK,
change `SCO_SDK_VERSION`, `SCO_SDK_RELEASE_TAG` and `SCO_SDK_SHA256` together (from the release `SHA256SUMS`), and
`SCO_CORE_SOURCE_SHA256` in `cmake/ScoCoreSource.cmake` with them. A green build is
not "verified in game": nothing here has been run in game, and nothing new in SDK 2.4.0 has been run in game either.

### Coordinated development SDK inputs

For local cross-repository testing, obtain both archives and their expected SHA-256 hashes from the core
integration owner, together with the full coordinated core commit and SDK version. Verify the pair before
configuring. The verifier checks both hashes, archive profiles, SDK and ABI versions, and the core commit in
each archive's compatibility metadata. This checks consistency against the supplied values; it does not
authenticate their source or turn a development artifact into a published SDK.

```sh
python3 tools/verify_dev_sdk.py --archive-dir "$ARCHIVES" --version "$VERSION" \
  --source-commit "$CORE_COMMIT" --sdk-sha256 "$SDK_SHA256" --core-source-sha256 "$SOURCE_SHA256" &&
cmake -S . -B build-dev -G 'Unix Makefiles' -DSCO_SDK_ARCHIVE_DIR="$ARCHIVES" \
  -DSCO_SDK_VERSION="$VERSION" -DSCO_SDK_SHA256="$SDK_SHA256" \
  -DSCO_CORE_SOURCE_SHA256="$SOURCE_SHA256" -DSC_MP_NET_BOUNDARY_PROFILE=dev-1b &&
cmake --build build-dev &&
ctest --test-dir build-dev --output-on-failure
```

The CMake build checks the supplied hashes again. Use a fresh build directory and record the exact source and
archive hashes with results. The relay boundary checks deliberately pin reviewed SDK source files: when a new
kit changes one, review the difference before updating those checks, and do not bypass a failure. The provisional
`dev-1b` profile accepts only the exact pair described in the
[network boundary review](docs/handoff/2026-10-31-network-boundary-review.md); a new source and its artifacts require
a new review and pins. Published SDK pins remain the default until a separately reviewed change updates them.
Development input results are not release, Windows, LAN or game evidence.
The hosted Linux and Windows development jobs build and test the `dev-1b`
pair from that exact source. A separate hosted Windows job tests the published
RC2 pair. Check each job's result for its actual platform evidence.

## Relay support

The plugin works with a relay server that is never a player (sc-offline PR #133, from an outside contributor;
see [PROVENANCE.md](PROVENANCE.md)): it reads the server's `multiplayer.relay` roster, follows nearby-only
forwarding, and moves ghosts from a buffer of recent poses. The relay server is in [server/](server/README.md):
a Windows/Linux program built from the SDK's `core-source` kit (`cmake/ScoCoreSource.cmake`, downloaded and
hash-checked like the plugin SDK archive), with its socket behind the kit's `ScopedTransport`, so the LAN rule
applies. Relay and players must use the same multiplayer protocol generation, currently 2; upgrade them together
before starting a session. The generation is separate from binary and SDK versions; see the
[protocol instructions](plugins/multiplayer/README.md). What a relay must meet: [CONTRIBUTING.md](CONTRIBUTING.md).
Background: [docs/relay-roadmap.md](docs/relay-roadmap.md). Not run in game.

Current single-player, listen-host, relay-only, and future authoritative-server boundaries are
summarized in [docs/session-modes.md](docs/session-modes.md).

## Text chat

Players in a session can send each other short lines of text from the plugin's panel, in a session a player hosts and
through a relay server alike. It is plain text and **not encrypted** (use a VPN, as for the rest of the traffic), and
a player on a plugin build without it never sees it. It adds one channel, `multiplayer.chat`, and leaves protocol
generation 2 unchanged, so the relay is the same program. **Not run in game**: only synthetic tests have run, and
how it looks in game is left to testers. Rules and limits: [plugins/multiplayer/README.md](plugins/multiplayer/README.md#text-chat).

## Licence

GPL-3.0-only ([LICENSE](LICENSE)). Where it came from: [PROVENANCE.md](PROVENANCE.md). How to contribute, and
where the relay server lives: [CONTRIBUTING.md](CONTRIBUTING.md).
