# ADR 0002 — Published-SDK synthetic authority experiment

Status: **PARTIAL — scratch experiment, not product integration approval**.

## Exact inputs

Fresh additive root: `$SCRATCH/sc-mp-authority-sdk-20261030`.
Fresh clones verified against remote main:
- sc-mp `57ac2fd3e6fd3e23a974d58c33fbd4935b9d12f0`.
- sco-core `5b010dc0697535f10e9264235494b667ea850ea6` (reference only, not linked).
- Latest published SDK `sdk-v2.4.0-rc.2`, dereferenced tag and release.json source `844e136ccb280ad8cb43f73bc43c9447e8f6799d`.

Current sc-mp `cmake/ScoSdk.cmake:13–19` pins 2.4.0 rc.2, public archive SHA-256 `51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e`. `cmake/ScoCoreSource.cmake:26–28` pins core-source `ddd1d75055f6b0efd69af4ac2eff6a18a5ff177f87cb28c938af3a625befbbb8`. Both archives downloaded and verified against release.json; all downloaded asset digests and exact paths are in `../evidence/manifest.json` relative to clone root (the manifest is at the scratch root's evidence/). SDK source is extracted under `sdk/sco-sdk-core-source-2.4.0` at the scratch root. Nothing links the fresh sco-core clone.

Previous `$SCRATCH/sc-mp-authority-foundation/docs/adr-0001-synthetic-authority-spike.md` was read as historical reference only; its old baseline/integration claims are not inherited. It remains untouched. Read-only chat candidate `02edf442479e3bb0857ef062d6c61f9b0d05124a` and installer candidate `917b3fd3201a9325c56efbf048d94ecf66bef322` have the same rc.2 public/core-source pins. This additive spike changes none of their files; no runtime composition/integration test with those candidates has been performed. No historical engine checkout substitutes for them. No failed owner-drain dependency is used.

## Decision and boundary

Use the published `sco::net::Core`, not a datagram endpoint identity surrogate. The actual archive's CMake `sco_net` target compiles core.cpp, reliable.cpp, wire.cpp and sha2.c. Only the datagram medium is in memory: the archive's unchanged `tests/net_mem.h`. Actual PBKDF2 proof handshake, per-link keys, tag verification, replay checks, ordered reliable delivery and shared host receive dispatch execute. Test PBKDF2 iterations are 2 on both sides, an explicit test option rather than production strength. No real LAN, UDP sockets, encryption/confidentiality claim, account identity or Windows evidence.

Authoritative input origin comes only from Core's authenticated `Message::from`, never a payload-supplied peer or raw endpoint lookup. Published `include/sco/net/core.h:51–71,106–134` defines message/peer callbacks and session operations. Published `src/net/core.cpp:610–631,676–726` checks source link, sender, tag/replay and originating identity before reliable delivery. Multiple remote peers feed one host Core callback and one bounded authority queue. No transport/reliability reimplementation.

The synthetic authority is intentionally a bounded integer counter, not the earlier two-cell protocol or a game world. Local and remote inputs reach the same submit function. Fixed trace +8,+4 produces value 12 for solo/direct, listen (one direct, one remote) and dedicated (both remote). All fixtures currently establish the same three-Core session even in solo; these are input-composition modes, **not separate executable lifecycle topologies**. The CLI mode names do not establish a standalone server process.

## Incarnation scheme

A serialized, process-local non-reusing counter allocates each authority incarnation; exhaustion refuses instead of wrapping. Queue entries capture that epoch; restart invalidates older queued entries. Restart must be coordinated with Core Leave/Host, as exercised. On peer Left, queued entries for its SDK peer ID are revoked. Core assigns never-reused IDs within a session; reconnect and restarted sessions negotiate fresh link keys. Captured old DATA is injected after reconnect/restart and rejected; fresh authenticated intent then succeeds.

This is **not durable uniqueness**: allocator state is volatile, unsynchronized and not persisted, process restart uniqueness is not proven, SDK random freshness is probabilistic, not durable storage. No restart-safe persistent snapshots or crash recovery claim. Restarting the authority without restarting its Core session is unsupported. In-process tests establish only the coordinated boundary described here.

## Executed evidence

`evidence/run-*.log` at scratch root records actual build, CTest, named scenarios and CLI outputs; `evidence/asan.log` records ASan+UBSan build of both SDK and spike and all scenarios. One CTest entry runs the native scenario suite. Actual observed development red→green cycles: missing direct submit, absent dispatch, stale epoch acceptance, disconnect queue retention and reused process-local incarnation all failed then passed. See separate mutation evidence for reproducible red controls. Existing SDK auth/reliability tests passing first are verification of supplied primitives, not newly implemented TDD features.

- Real SDK session reaches three peers; one callback receives both clients.
- Wrong passphrase does not join; captured authenticated DATA from unknown endpoint increments spoof rejection; modified tag increments badTag.
- Seeded 25% loss, 30% duplication and 1–80 ms delay reaches the fixed final state and observes replay rejection.
- Forced first DATA loss while later DATA arrives before retransmission: actual SDK retransmit counter increases; delivered per-sender deltas remain [1,2]. Bounded synthetic schedules only, not arbitrary partition progress or cross-peer total ordering.
- Old wire/queued messages rejected after reconnect and coordinated restart; fresh input works afterwards.
- Reliable send queue reaches QueueFull; Leave makes Send return NoSession. Authority ingress caps at 16 and counts overflow; 17 SDK-delivered intents produce 16 queued plus one explicit overflow count.
- All CLI modes return synthetic-only JSON, value 12, sc_world_available false. `sc-world` and unknown mode exit 1.

## Remaining blockers and limits

The SDK supplies the required core primitives; no missing SDK primitive currently blocks this experiment. This is **not completion of a reliable gameplay command protocol**: ingress overflow is counted locally, not acknowledged to the sender; transport reliability does not imply authority acceptance. A next design needs explicit application acceptance/backpressure or bounded fail-closed session policy. The archive test medium itself has an unbounded in-flight vector; only exercised schedules are bounded, not arbitrary hostile datagram memory growth. A bounded transport seam plus saturation/shutdown tests is still owed for a full end-to-end queue-bound claim. Authority input has no entity ownership, sequence/idempotence receipts, snapshot replication, late join, reconciliation, physics or persistence. No game capability is inferred from an authenticated peer.

Independent reviewer ran the artifact and identified missing fresh-input recovery checks, weak probabilistic loss coverage, ignored arguments and silent queue overflow. Fresh-input checks, forced DATA loss/ordered retransmission, CLI validation and explicit overflow counters were subsequently added and rerun. Remaining lifecycle-topology, sender acceptance/backpressure and test-medium bound findings are disclosed above; no final approval is claimed.

SC world provider remains unavailable. No distributed workers, gameplay, Windows, deployment, push, merge, release, source-owner edits or production integration occurred.

## Reproduce

From the scratch root:

```sh
cmake -S sc-mp/spikes/authority-sdk -B build -DSDK_ROOT="$PWD/sdk/sco-sdk-core-source-2.4.0"
cmake --build build --target authority-cli -j4
ctest --test-dir build --output-on-failure
build/authority-cli solo
build/authority-cli listen
build/authority-cli dedicated
build/authority-cli sc-world # deliberately exits 1
```

Next bounded work should close application queue admission/backpressure and distinct topology lifecycle coverage before broadening this into a gameplay architecture.
