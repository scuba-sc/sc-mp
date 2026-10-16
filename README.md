# sc-mp

Private co-presence for [sc-offline](https://github.com/scubamount/sc-offline): an optional
`multiplayer` plugin that lets players who all run sc-offline see each other as ghosts in their own
offline games, over a LAN or VPN.

This module used to live in the sc-offline repository. It now has its own home so it can move at its
own pace; sc-offline does not ship it.

## Status

Development code. **It has not been run in game.** The plugin builds against the sco-core SDK's public
headers only (SDK 2.2.0 or newer, which ships `sc_private_session.h`), and joining a session needs the
host to positively admit it as offline. Treat everything here as unverified until a release says
otherwise.

## Layout

The files keep the paths they had in sc-offline, so history and patches carry over unchanged:

| Path | What it is |
| --- | --- |
| `plugins/multiplayer/` | the plugin: `multiplayer.cpp`, `multiplayer_wire.h`, `plugin.ini`, `CMakeLists.txt`, and its own `README.md` (protocol, settings, consent) |
| `tools/multiplayer_workflow_test.cpp` | the module's workflow test |

## Build

The repo root is a standalone CMake project. It downloads the published SDK archive `sco-sdk-2.3.0.zip`
from the sco-core `sdk-v2.3.0` release, refuses it unless its SHA-256 matches the pin in
[cmake/ScoSdk.cmake](cmake/ScoSdk.cmake), and builds against the extracted `include/`. It never reads a
sibling sco-core checkout.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`ctest` runs the module workflow test (`tools/multiplayer_workflow_test.cpp`, synthetic services, no sockets,
no game) and, on Windows, the SDK's `sco-plugin-check` on the plugin folder. For an offline build, put the
release asset in a directory and pass `-DSCO_SDK_ARCHIVE_DIR=<dir>`; the hash is still checked. CI:
[.github/workflows/build.yml](.github/workflows/build.yml) (Windows and Linux). To move to a newer SDK,
change `SCO_SDK_VERSION` and `SCO_SDK_SHA256` together (from the release `SHA256SUMS`). A green build is
not "verified in game": nothing here has been run in game.

## Branches

- `main`: the module as it was in sc-offline v0.7.8.
- `relay-import`: the relay work from sc-offline PR #133 (a relay server that is never a player,
  nearby-only forwarding, smoother ghosts), applied on top. Not merged; see
  [docs/relay-roadmap.md](https://github.com/scubamount/sc-mp/blob/relay-import/docs/relay-roadmap.md) on that branch.

## Licence

GPL-3.0-only ([LICENSE](LICENSE)). Where it came from: [PROVENANCE.md](PROVENANCE.md).
