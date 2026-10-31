# Provenance

Extracted from scubamount/sc-offline v0.7.8 (commit `4db58d237c5f77ef6b017c135a91970ae7806838`),
paths `plugins/multiplayer/` and `tools/multiplayer_workflow_test.cpp`, unchanged. GPL-3.0-only, as
in sc-offline. Consumes the sco-core SDK 2.4.0, release candidate 2 (`sdk-v2.4.0-rc.2`,
`844e136ccb280ad8cb43f73bc43c9447e8f6799d`), public headers only; the relay server builds from that
release's `core-source` kit.

The `relay-import` branch adds sc-offline PR #133 (commit `97b0b7a`), restricted to the module's own
files; that PR's CHANGELOG and `docs/features.md` hunks belong to sc-offline and were left out.

sc-offline's own lineage and third-party notices:
[THIRD_PARTY_NOTICES.md](https://github.com/scuba-sc/sc-offline/blob/main/THIRD_PARTY_NOTICES.md).

## Experimental bounded medium

`spikes/authority-sdk/bounded_mem.h` adapts `tests/net_mem.h` from the published
sco-core `sdk-v2.4.0-rc.2` core-source kit, source
`844e136ccb280ad8cb43f73bc43c9447e8f6799d`, under GPL-3.0-only. The adapted helper
adds shared queue/payload limits, rejection accounting, and fail-closed receive.
The repository GPL license applies; this attribution does not settle the pending
outside-contribution rights below.

## Outside contributions

Code from anyone other than the maintainer is recorded here when it lands, under the terms in
[CONTRIBUTING.md](CONTRIBUTING.md). One row per contribution:

| Contributor (GitHub) | What and where | Source | Terms it came in under | Credit in history | Owner review |
| --- | --- | --- | --- | --- | --- |
| `zarilo88-ui` | The relay-support and ghost-smoothing work on this branch: `plugins/multiplayer/relay_wire.h`, `plugins/multiplayer/motion.h`, the relay and nearby-only paths in `plugins/multiplayer/multiplayer.cpp`, `plugins/multiplayer/.gitattributes`, `tools/multiplayer_motion_test.cpp`, additions to `tools/multiplayer_workflow_test.cpp` and `plugins/multiplayer/README.md`, `docs/relay-roadmap.md`, and the motion test target in `plugins/multiplayer/CMakeLists.txt` | sc-offline pull request #133 (closed), commit `97b0b7a`; `relay_wire.h` states it is a byte-for-byte copy of the contract of a relay server (sco-relay) whose source is not in this repository | Pull request to sc-offline; sc-offline's CONTRIBUTING says "By opening a pull request you agree your contribution is licensed under GPL-3.0-only." (see [CONTRIBUTING.md](CONTRIBUTING.md)) | Commit `1ad9e7b` is authored `scubamount` with `Co-authored-by: zarilo88-ui` and a `Co-Authored-By` trailer for an AI assistant; the merge of this branch keeps that commit | pending |
| `zarilo88-ui` | The idea of the bounded roster read past 16 members in `plugins/multiplayer/multiplayer.cpp` (`Members`), re-implemented by the maintainer in commit `64c2dbe`, which names it; no code of sc-mp pull request #1 was merged | sc-mp pull request #1 (open) | Pull request to sc-mp; no licence text of sc-mp's own applies yet (see the gaps in [CONTRIBUTING.md](CONTRIBUTING.md)) | Named in the commit message of `64c2dbe` | pending |
| `zarilo88-ui` | The relay server: `server/` (the program, its tests, its checks and the example config) and `cmake/ScoCoreSource.cmake`, with the matching lines in `CMakeLists.txt`, `README.md` and `plugins/multiplayer/README.md`. It is the source of the relay that the `relay_wire.h` row above says is not in this repository | `sco-relay` main at commit `c71f92a`, the contributor's standalone relay (GPL-3.0-only, never published; its history is not part of this change). The program is taken as it was, with its paths, includes and a few messages changed to this layout. The two boundary checks were adapted, not only moved: `server/tools/check-no-game-code.sh` reads a facts file that CMake writes, scans an explicit production set, admits only `relay_wire.h` outside `server/` and has two new checks (the kit's own tests are off; the CMake module sets the game pack off); `server/tools/check-net-boundary.sh` takes the kit as an argument and pins 15 kit files instead of 10. One test line was rewritten for GCC 13 (`server/tests/test_relay_wire.cpp`). The build is new: the server is built from the SDK's `core-source` kit instead of a submodule (`cmake/ScoCoreSource.cmake`). sc-mp pull request (draft) | Pull request to sc-mp, GPL-3.0-only, with the answers to the source, rights and game-material questions in the pull request description (see [CONTRIBUTING.md](CONTRIBUTING.md)) | New commits authored `zarilo88-ui`, each with a `Co-Authored-By` trailer for an AI assistant | pending |
| `zarilo88-ui` | Text chat in the multiplayer panel: the `multiplayer.chat` v1 layout in `plugins/multiplayer/multiplayer_wire.h`, the chat paths and panel nodes 20-23 in `plugins/multiplayer/multiplayer.cpp`, additions to `tools/multiplayer_workflow_test.cpp` and `server/tests/test_udp.cpp`, the chat text in `plugins/multiplayer/README.md`, `README.md` and `docs/session-modes.md`, and the "Admission in the production shell" paragraph of `plugins/multiplayer/README.md` (last commit) | sc-mp pull request "multiplayer: text chat in the panel (multiplayer.chat v1)" | Pull request to sc-mp, GPL-3.0-only, with the answers to the source, rights and game-material questions in the pull request description (see [CONTRIBUTING.md](CONTRIBUTING.md)) | New commits authored `zarilo88-ui`, each with a `Co-Authored-By` trailer for an AI assistant | pending |
| `zarilo88-ui` | The include and link-line reading of the relay's no-game-code check (`server/tools/check-no-game-code.sh`, `server/tests/test_no_game_code.sh`, comments in `server/tests/CMakeLists.txt`) and the relay install kit (`server/deploy/install.sh`, `server/deploy/sco-relay.service`, `server/deploy/README.md`), with the matching sections of `server/README.md` | sc-mp pull request "relay: a one-command Linux install kit, and a tighter no-game-code check" | Pull request to sc-mp, GPL-3.0-only, with the answers to the source, rights and game-material questions in the pull request description (see [CONTRIBUTING.md](CONTRIBUTING.md)) | New commits authored `zarilo88-ui`, each with a `Co-Authored-By` trailer for an AI assistant | pending |

Not recorded as settled: the rights in the relay-support work, and whether the terms quoted above are the ones
that govern sc-mp. Both are for the maintainer. "Pending" in the last column means the maintainer has not yet
reviewed the row.
