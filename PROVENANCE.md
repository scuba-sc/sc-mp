# Provenance

Extracted from scubamount/sc-offline v0.7.8 (commit `4db58d237c5f77ef6b017c135a91970ae7806838`),
paths `plugins/multiplayer/` and `tools/multiplayer_workflow_test.cpp`, unchanged. GPL-3.0-only, as
in sc-offline. Consumes the immutable sco-core SDK 2.4.0 release (`sdk-v2.4.0`,
source commit `6106fa3639cbcc28f78a1ded8de2707de019f4d3`), public headers only; the relay server builds from that
release's `core-source` kit.

The `relay-import` branch adds sc-offline PR #133 (commit `97b0b7a`), restricted to the module's own
files; that PR's CHANGELOG and `docs/features.md` hunks belong to sc-offline and were left out.

sc-offline's own lineage and third-party notices:
[THIRD_PARTY_NOTICES.md](https://github.com/scuba-sc/sc-offline/blob/main/THIRD_PARTY_NOTICES.md).

## Outside contributions

Code from anyone other than the maintainer is recorded here when it lands, under the terms in
[CONTRIBUTING.md](CONTRIBUTING.md). One row per contribution:

| Contributor (GitHub) | What and where | Source | Terms it came in under | Credit in history | Owner review |
| --- | --- | --- | --- | --- | --- |
| `zarilo88-ui` | The relay-support and ghost-smoothing work on this branch: `plugins/multiplayer/relay_wire.h`, `plugins/multiplayer/motion.h`, the relay and nearby-only paths in `plugins/multiplayer/multiplayer.cpp`, `plugins/multiplayer/.gitattributes`, `tools/multiplayer_motion_test.cpp`, additions to `tools/multiplayer_workflow_test.cpp` and `plugins/multiplayer/README.md`, `docs/relay-roadmap.md`, and the motion test target in `plugins/multiplayer/CMakeLists.txt` | sc-offline pull request #133 (closed), commit `97b0b7a`; `relay_wire.h` was originally described as a byte-for-byte copy of the standalone relay contract; the imported server now shares this header | Pull request to sc-offline; sc-offline's CONTRIBUTING says "By opening a pull request you agree your contribution is licensed under GPL-3.0-only." (see [CONTRIBUTING.md](CONTRIBUTING.md)) | Commit `1ad9e7b` is authored `scubamount` with `Co-authored-by: zarilo88-ui` and a `Co-Authored-By` trailer for an AI assistant; the merge of this branch keeps that commit | owner-attested by maintainer 2026-11-01; attribution and source record retained |
| `zarilo88-ui` | The bounded roster-read approach past 16 members in `plugins/multiplayer/multiplayer.cpp` (`Members`), re-implemented by the maintainer in commit `64c2dbe`, which names it; sc-mp pull request #1 closed without merge | sc-mp pull request #1 (closed) | Pull request to sc-mp; its historical terms and rights remain subject to maintainer review (see [CONTRIBUTING.md](CONTRIBUTING.md)) | Named in the commit message of `64c2dbe` | pending |
| `zarilo88-ui` | The relay server: `server/` (the program, its tests, its checks and the example config) and `cmake/ScoCoreSource.cmake`, with the matching lines in `CMakeLists.txt`, `README.md` and `plugins/multiplayer/README.md`. The imported server shares `plugins/multiplayer/relay_wire.h` with the plugin | `sco-relay` main at commit `c71f92a`, the contributor's standalone relay (described by the submitter as GPL-3.0-only and never published; its history is not part of this change). The submitter says the program was taken as it was, with its paths, includes and a few messages changed to this layout. The two boundary checks were adapted, not only moved: `server/tools/check-no-game-code.sh` reads a facts file that CMake writes, scans an explicit production set, admits only `relay_wire.h` outside `server/` and has two new checks (the kit's own tests are off; the CMake module sets the game pack off); `server/tools/check-net-boundary.sh` takes the kit as an argument and pins 15 kit files instead of 10. One test line was rewritten for GCC 13 (`server/tests/test_relay_wire.cpp`). The build is new: the server is built from the SDK's `core-source` kit instead of a submodule (`cmake/ScoCoreSource.cmake`). [sc-mp pull request #6](https://github.com/scuba-sc/sc-mp/pull/6) (closed; originally draft) | The submitter declared GPL-3.0-only and answered the source, rights and game-material questions in #6; see the maintainer's owner statement below (the submitter's attribution and source record are unchanged) | New commits authored `zarilo88-ui`, each with a `Co-Authored-By` trailer for an AI assistant | owner-attested by maintainer 2026-11-01; attribution and source record retained |
| `zarilo88-ui` | The relay no-game-code checker, install kit and their tests: `server/tools/check-no-game-code.sh`, `server/tests/test_no_game_code.sh`, `server/tests/CMakeLists.txt`, `server/deploy/install.sh`, `server/deploy/sco-relay.service`, `server/deploy/README.md`, and the corresponding `server/README.md` sections | sc-mp pull request #15, “relay: a one-command Linux install kit, and a tighter no-game-code check” | The submitter declares GPL-3.0-only and answered the source, rights and game-material questions in that pull request (see [CONTRIBUTING.md](CONTRIBUTING.md)) | Original contributor commits remain authored by the submitter with existing AI co-author trailers; maintainer hardening is in separate commits | owner-attested by maintainer 2026-11-01; attribution and source record retained |
| `zarilo88-ui` | Text chat in the multiplayer panel: the `multiplayer.chat` v1 layout in `plugins/multiplayer/multiplayer_wire.h`, the chat paths and panel nodes 20-23 in `plugins/multiplayer/multiplayer.cpp`, additions to `tools/multiplayer_workflow_test.cpp` and `server/tests/test_udp.cpp`, and chat/admission status documentation in `plugins/multiplayer/README.md`, `README.md` and `docs/session-modes.md` | sc-mp pull request "multiplayer: text chat in the panel (multiplayer.chat v1)" | Pull request to sc-mp under GPL-3.0-only; maintainer owner statement recorded below (see [CONTRIBUTING.md](CONTRIBUTING.md)) | Original commits remain authored by the contributor with existing AI co-author trailers | owner-attested by maintainer 2026-11-01; attribution and source record retained |

The maintainer selected GPL-3.0-only contribution checks while preserving
contributor credit. On 2026-11-01, the maintainer stated that they own the rights
to the code involved in sc-mp PRs #14–#20. This is recorded as a user statement,
not an independent legal determination. Contributor attribution, sources and
terms above remain as recorded; this entry does not claim separate verification
of the underlying history. "Pending" means the maintainer has not yet reviewed
that row or recorded an applicable owner statement.
