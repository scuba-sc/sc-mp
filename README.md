# sc-mp

Private co-presence for [sc-offline](https://github.com/scuba-sc/sc-offline): an optional
`multiplayer` plugin that lets players who all run sc-offline see each other as ghosts in their own
offline games, over a LAN or VPN.

This module used to live in the sc-offline repository. It now has its own home so it can move at its
own pace; sc-offline does not ship it.

## Status

Development code. **It has not been run in game.** The plugin builds against public
headers from the source-bound development SDK described below; the headers it needs, such as
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

For the current source-first development integration, generate the legacy SDK and
core-source archives from the clean sco-core commit
`e2ad885aa3027ed1162793d30292ca103c2f5b28` into a separate directory. They are
development inputs, not RC2 release assets. Verify their SHA-256 values and use a
fresh build directory:

```sh
git -C <sco-core-checkout> worktree add --detach <clean-core-worktree> e2ad885aa3027ed1162793d30292ca103c2f5b28
python3 <clean-core-worktree>/sdk/package.py --profile legacy --out <development-assets>
python3 <clean-core-worktree>/sdk/package.py --profile core-source --out <development-assets>
cmake -S . -B build-development -DSCO_SDK_ARCHIVE_DIR=<development-assets>
cmake --build build-development
ctest --test-dir build-development --output-on-failure
```

These hashes identify the locally generated archives used for this integration,
not a published release. An appropriate supported stable release can become the
normal baseline once it contains the required contracts and passes consumer
checks; explicit development inputs remain available for ongoing work.
The expected legacy archive SHA-256 is
`ff4fb3295fc6c0740f813e325c2882913be309e123deaf5fca3c9af095f2b789`;
the core-source archive SHA-256 is
`b7677073137867314a505711c24da97b9b3bf6d3a5e56650f071dd80a84ae1aa`.
The build checks both archive hashes before extraction. The plugin uses the SDK's
public headers; the relay builds from `core-source`. CI generates these archives
from the exact commit and tests Windows and Linux. To reproduce the earlier
published RC2 configuration, use sc-mp commit
`57ac2fd3e6fd3e23a974d58c33fbd4935b9d12f0` and its published-asset recipe.

## Relay support

The plugin works with a relay server that is never a player (sc-offline PR #133, from an outside contributor;
see [PROVENANCE.md](PROVENANCE.md)): it reads the server's `multiplayer.relay` roster, follows nearby-only
forwarding, and moves ghosts from a buffer of recent poses. The relay server is in [server/](server/README.md):
a Windows/Linux program built from the SDK's `core-source` kit (`cmake/ScoCoreSource.cmake`, locally supplied and
hash-checked like the plugin SDK archive), with its socket behind the kit's `ScopedTransport`, so the LAN rule
applies. Relay and players must use the same multiplayer protocol generation, currently 2; upgrade them together
before starting a session. The generation is separate from binary and SDK versions; see the
[protocol instructions](plugins/multiplayer/README.md). What a relay must meet: [CONTRIBUTING.md](CONTRIBUTING.md).
Background: [docs/relay-roadmap.md](docs/relay-roadmap.md). Not run in game.

Current single-player, listen-host, relay-only, and future authoritative-server boundaries are
summarized in [docs/session-modes.md](docs/session-modes.md).

## Licence

GPL-3.0-only ([LICENSE](LICENSE)). Where it came from: [PROVENANCE.md](PROVENANCE.md). How to contribute, and
where the relay server lives: [CONTRIBUTING.md](CONTRIBUTING.md).
