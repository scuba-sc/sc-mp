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

## Outside contributions

Code from anyone other than the maintainer is recorded here when it lands, under the terms in
[CONTRIBUTING.md](CONTRIBUTING.md). One row per contribution:

| Contributor (GitHub) | What and where | Source | Terms it came in under | Credit in history | Owner review |
| --- | --- | --- | --- | --- | --- |
| `zarilo88-ui` | The relay-support and ghost-smoothing work on this branch: `plugins/multiplayer/relay_wire.h`, `plugins/multiplayer/motion.h`, the relay and nearby-only paths in `plugins/multiplayer/multiplayer.cpp`, `plugins/multiplayer/.gitattributes`, `tools/multiplayer_motion_test.cpp`, additions to `tools/multiplayer_workflow_test.cpp` and `plugins/multiplayer/README.md`, `docs/relay-roadmap.md`, and the motion test target in `plugins/multiplayer/CMakeLists.txt` | sc-offline pull request #133 (closed), commit `97b0b7a`; `relay_wire.h` was originally described as a byte-for-byte copy of the standalone relay contract; the imported server now shares this header | Pull request to sc-offline; sc-offline's CONTRIBUTING says "By opening a pull request you agree your contribution is licensed under GPL-3.0-only." (see [CONTRIBUTING.md](CONTRIBUTING.md)) | Commit `1ad9e7b` is authored `scubamount` with `Co-authored-by: zarilo88-ui` and a `Co-Authored-By` trailer for an AI assistant; the merge of this branch keeps that commit | pending |
| `zarilo88-ui` | The bounded roster-read approach past 16 members in `plugins/multiplayer/multiplayer.cpp` (`Members`), re-implemented by the maintainer in commit `64c2dbe`, which names it; sc-mp pull request #1 closed without merge | sc-mp pull request #1 (closed) | Pull request to sc-mp; its historical terms and rights remain subject to maintainer review (see [CONTRIBUTING.md](CONTRIBUTING.md)) | Named in the commit message of `64c2dbe` | pending |
| `zarilo88-ui` | The relay server: `server/` (the program, its tests, its checks and the example config) and `cmake/ScoCoreSource.cmake`, with the matching lines in `CMakeLists.txt`, `README.md` and `plugins/multiplayer/README.md`. The imported server shares `plugins/multiplayer/relay_wire.h` with the plugin | `sco-relay` main at commit `c71f92a`, the contributor's standalone relay (described by the submitter as GPL-3.0-only and never published; its history is not part of this change). The submitter says the program was taken as it was, with its paths, includes and a few messages changed to this layout. The two boundary checks were adapted, not only moved: `server/tools/check-no-game-code.sh` reads a facts file that CMake writes, scans an explicit production set, admits only `relay_wire.h` outside `server/` and has two new checks (the kit's own tests are off; the CMake module sets the game pack off); `server/tools/check-net-boundary.sh` takes the kit as an argument and pins 15 kit files instead of 10. One test line was rewritten for GCC 13 (`server/tests/test_relay_wire.cpp`). The build is new: the server is built from the SDK's `core-source` kit instead of a submodule (`cmake/ScoCoreSource.cmake`). [sc-mp pull request #6](https://github.com/scuba-sc/sc-mp/pull/6) (closed; originally draft) | The submitter declared GPL-3.0-only and answered the source, rights and game-material questions in #6; the historical rights and terms remain subject to maintainer review (see [CONTRIBUTING.md](CONTRIBUTING.md)) | New commits authored `zarilo88-ui`, each with a `Co-Authored-By` trailer for an AI assistant | pending |

The maintainer selected GPL-3.0-only contribution checks while preserving
contributor credit. This does not establish the rights or historical terms of
prior submissions. On 2026-11-01, the maintainer confirmed they may contribute
newly generated project code and documentation under GPL-3.0-only without an
employer or client restriction. This is the maintainer's statement, not an
independent legal determination; it does not certify the older copied relay
material. The maintainer reports no other source or permission records for that
material beyond the submitters' statements. Its rights and the adequacy of those
statements remain unresolved.
"Pending" means the maintainer has not yet reviewed the row.
