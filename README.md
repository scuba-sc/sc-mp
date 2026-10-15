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

Building it standalone is not set up yet: in sc-offline it was configured as a subdirectory of
`plugins/CMakeLists.txt`. A standalone CMake project and CI are the next step.

## Branches

- `main`: the module as it was in sc-offline v0.7.8.
- `relay-import`: the relay work from sc-offline PR #133 (a relay server that is never a player,
  nearby-only forwarding, smoother ghosts), applied on top. Not merged; see
  [docs/relay-roadmap.md](https://github.com/scubamount/sc-mp/blob/relay-import/docs/relay-roadmap.md) on that branch.

## Licence

GPL-3.0-only ([LICENSE](LICENSE)). Where it came from: [PROVENANCE.md](PROVENANCE.md).
