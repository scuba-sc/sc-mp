# Authoritative session prototype: acceptance and continuation

This is a local Linux prototype of a volatile integer entity session. It is
separate from the production relay and plugin. Not run in game. Draft publication
is authorized; deployment, native-provider enablement and rights decisions are
outside this change. Architecture and exclusions:
[ADR 0004](../adr-0004-volatile-authoritative-session.md).

## Exact development inputs

| Input | Identity |
| --- | --- |
| sc-mp base / MP #16 head | `70c895ded2d612b1b14933600f0825a5a3556392` |
| sco-core merged source | `7ee83fa95680aa482d8b160987b472547a67b888` |
| Local core-source archive SHA-256 | `865b26f6416474ffdb061ced49d1a0fb9d2fe31ac4857f30c46db45351c6c6c5` |
| Extracted `SHA256SUMS` SHA-256 | `908bd2674577dc435fce4bbf97c2cbbb75637ccba00de9375dafd3949cb09d0a` |
| Inventory | 388 listed files plus `SHA256SUMS`; exact file set and every content hash checked |
| Offline integration reference | `772129e09e7838d56165bd3613f43ac49d373bb8` |
| Engine integration reference | `f005372bd74967e53fdb8ed4cf3f4707ab3b20fa` |

The archive was packaged locally from the exact clean core source. It is not a
published SDK and its hash does not independently authenticate its producer.
The new spike checks the literal inventory hash and complete extraction before
executing the kit's CMake project. It does not relax the existing consumer pins.

All 16 network source Git blobs pinned by MP #16 are identical between core
`1b6782bc5c8b7d189cdff4c83e81957f09af35d7` and merged `7ee83fa`. The old root
`dev-1b` profile nevertheless rejects the merged kit's changed
`COMPATIBILITY.json` identity. That rejection is expected and preserved: source
equivalence is not permission to silently rewrite a whole archive profile.
This separate prototype selects the merged input explicitly.

## Reproduce

Use a clean, isolated core checkout at the exact SHA above. Package its public
`core-source` profile with `python3 sdk/package.py --profile core-source --out
<artifacts>`, verify the archive hash above, and extract it into a fresh directory.
Then run from this sc-mp checkout:

```sh
cmake -S spikes/authority-session -B build-authority \
  -DSDK_ROOT=<extracted>/sco-sdk-core-source-2.4.0
cmake --build build-authority --parallel 4
ctest --test-dir build-authority --output-on-failure
```

The target uses POSIX process I/O and is a Linux-tested fixture, not a Windows
server deliverable. CMake registers `authority-model` and `authority-processes`;
the latter runs 15 Python unittest cases. The harness creates actual server and
client child processes, drains their output, bounds waits and cleans up on failure.
Its solo socket inspection uses Linux `/proc`; a skip elsewhere is not socket-free
evidence for that platform.

Manual fixture commands (all are synthetic; choose an unused loopback UDP port):

```text
authority-session solo
authority-session listen <port>
authority-session dedicated <port>
authority-session client <port>
authority-session sc-world             # deliberately exits nonzero
```

Each process accepts bounded newline-delimited stdin commands. Read its JSON
context before issuing `command <epoch32hex> <id> <entity> <delta>`. IDs start at
one. `snapshot` reads only that participant's counter. `disconnect` and `quit`
end the process; neither reconnects. A dedicated server rejects local participant
commands. A context with `Overload` and entity zero grants no ownership. Output
is structured synthetic state; no credentials or peer-supplied names are logged.
The fixture uses a fixed test credential with PBKDF2 work factor two and cannot
be exposed as an operational service.

## Executed acceptance

- The existing MP #16 `spikes/authority-sdk` baseline passes all five CTest
  entries against the same merged-core kit. It remains the bounded in-memory
  authentication/replay/loss/receipt baseline; its transport results are not
  relabeled as new multi-process fault-injection evidence.
- The new model test covers ownership, stable retained receipts, conflict,
  stale epoch, gaps, eviction, disconnect, lifetime capacity and revision
  exhaustion. Checks remain active under `NDEBUG`.
- Fifteen process cases cover dedicated two-client ownership, listen local and
  remote policy, duplicates/conflicts, gap retry, invalid deltas, receipt eviction,
  explicit reconnect, killed-server cold restart with stale-epoch refusal,
  socket-free solo, unavailable SC world, malformed/overflowing control input,
  dedicated local refusal, output backpressure, lifetime capacity and graceful
  host-down without automatic rejoin. These are actual local UDP processes.
- Independent architecture review found no blocking defect in this volatile
  integer-session scope. Review led to explicit lifetime-cap refusal, revision
  exhaustion, client epoch invalidation, nonblocking fail-closed output,
  quit-before-buffered-mutation and always-active model checks. The independent
  reviewer ran the model plus the initial ten process cases; the separate test
  owner ran the expanded fifteen.
- Default GCC 14 build and both CTest entries pass. An ASan+UBSan Debug build
  instruments the SDK and fixture and passes both entries with leak detection
  enabled and undefined-behavior failures fatal. The intentional `_exit` on
  output backpressure bypasses teardown/leak reporting for that one child.
- A separate GCC 14 `Release` (`-O3`) build fails in the unchanged SDK
  `src/net/core.cpp:946` with `-Werror=free-nonheap-object` during vector inlining.
  No warnings were disabled and the SDK was not modified. The Release model
  executable itself builds and passes. A bounded follow-up isolates the warning
  to a valid `std::vector` reserve/append pattern: GCC 14 passes O0/O1/O2 and
  rejects O3; Clang 19.1.7 passes O0/O1/O2/O3 using the same libstdc++ 14 headers.
  This supports a compiler false positive for this diagnostic, not an SDK
  invalid-free defect. No source fix is proposed on this evidence.
- Full **Clang 19.1.7 Release** build (`-O3 -DNDEBUG`, warnings-as-errors retained
  for the SDK and prototype) passes both CTest entries, including all 15 process
  cases. Clang is a documented supported C++20 toolchain in core's
  `docs/building.md`. GCC 14 O3 qualification remains blocked by the diagnostic;
  Linux Clang Release qualification is complete for this bounded prototype.

Evidence bundle filenames: `input-manifest.json`, `source-manifest.json`,
`source-map-review.md`, `independent-review.md`, `configure.log`, `build.log`,
`ctest.log`, `process-tests.log`, `asan-configure.log`, `asan-build.log`,
`asan-ctest.log`, `release-build.log`, and `baseline-receipts/ctest.log`.
The bundle is local and supplied separately, not referenced as a public CI run.
An early capacity test incorrectly required `Ok` for the intentionally refused
17th participant; its failed logs are retained separately from corrected results.
The follow-up compiler bundle adds `compiler-diagnosis/report.md`, the standalone
reproducer, `compiler-matrix.json`, exact baseline build comparison, Clang Release
logs and `coverage-review.md`. It preserves the original failure evidence.

To reproduce the qualified Release path, use a fresh build directory with
`-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-19
-DCMAKE_CXX_COMPILER=clang++-19` and the same exact `SDK_ROOT`. No warning
suppression, optimization downgrade, SDK edit or changed archive pin is needed.

The separate `authority-session` workflow also validates the exact source/archive
pair on Ubuntu with Clang 18 Release. It runs for this draft's head and PR merge
candidate, including a stacked PR base. Its compiler version and complete CTest
output are recorded in the job; local Clang 19 evidence does not substitute for
that hosted result. The production relay workflow and pins are unchanged.

## Contract and provider map

All references below are to the exact input commits, not timestamp-selected trees.

| Existing public source | Actual boundary | Required next integration |
| --- | --- | --- |
| core `include/sco/net/core.h`, `scope.h`, `udp.h`; `docs/net-wire.md` | Authenticated session-local peers, reliable channels and scoped transport; no durable principals | Preserve `Message::from`; map it to provider principals in the trusted host |
| core `include/sc_private_session.h`; `docs/game/private-session.md` | Explicit Host/Join/Leave, host admission/bind policy, scalar state | New copied session/world context with role, authority, epoch and truthful capabilities |
| core `include/sc_entities.h`, `include/sc_owned.h` | Local plugin ownership and removal-initiation receipts | Separate network ownership grant/revocation mapping; never reinterpret local entity IDs |
| core `include/sc_world_control.h`; `docs/design/world-control.md` | Filtered rays and bounded camera lease; unavailable native paths stay unavailable | No existing dedicated simulation provider to attach here |
| core `include/sco_storage.h`; `docs/storage.md` | Per-plugin local SQLite state | Atomic authority state/receipt persistence and fencing need their own design |
| core `docs/agent-workflows.md`, authority section | Documents the shared-world authority gap | Implement versioned typed context/intent contracts before enabling game mutations |
| offline `src/dllmain.cpp`, `src/offline_host.cpp` | Product-owned bind/peer policy and offline admission | Explicit consent, positive admission, provider-thread validation and lifecycle teardown |
| engine `src/hostd/mock_game/private_session.cpp`, `mock_game.cpp`; `src/hostd/net_sim/` | State-only private-session mock and separate transport test processes | First integrate a new synthetic provider; UI can consume copied context/results later |

The active core lifecycle change (#305) and its dedicated owner are not modified
or assumed complete. A future provider integration depends on its reviewed final
source and downstream migration, not an observed intermediate head. Existing
chat, installer and provenance lanes are unchanged.

## Next bounded steps

1. Review this isolated draft and its exact-source evidence against the reviewed
   MP #16 head. If that base changes, review the delta and rerun. Merge ordering
   remains coordinated by the maintainer; MP #16 human provenance/rights gates
   remain pending independently.
2. Use the qualified Clang 19 Release path for continued local prototype work.
   Keep GCC 14 O3 compatibility as a compiler-specific limitation. The minimal
   reproducer and exact-source comparison support a false positive, so do not
   patch or weaken the SDK through this consumer. A future upstream source fix
   requires a separately verified actual defect.
3. Define one size-prefixed session-context contract and one bounded typed-intent
   contract in core, with size-tail/ABI tests and a synthetic provider. Include
   session/world identity, authenticated principal mapping, entity ownership,
   accepted-versus-completed results and fail-closed absence. Establish the final
   owner-drain/unload semantics first. No native adapter is implied.
4. Add that provider to an isolated engine host profile and drive two separate
   processes through the actual service tables. Verify unload, absent provider,
   stale handles and reconnect; keep the present production mock behavior intact
   until deliberately migrated. Consume one exact coordinated source/archive set.
5. Add offline composition only after explicit consent/admission and owning-thread
   lifecycle gates are satisfied. Game verification remains a separate human run.
6. If a saved world is actually required, scope a separate persistence milestone:
   atomic state plus deduplication commits, recovery generation, single-writer
   fencing, torn-write tests and unacknowledged-outcome semantics. Do not treat
   this fixture's cold restart as saved-world recovery.

## Draft publication scope

Exactly eight new files, including isolated synthetic CI; no tracked production
implementation modifications:

```text
.github/workflows/authority-session.yml
docs/adr-0004-volatile-authoritative-session.md
docs/handoff/2026-10-31-authoritative-session.md
spikes/authority-session/CMakeLists.txt
spikes/authority-session/session.h
spikes/authority-session/main.cpp
spikes/authority-session/model_tests.cpp
spikes/authority-session/process_tests.py
```

Draft title: `Prototype volatile authoritative sessions across local processes`.
The local patch and source manifest make this scope reviewable. The authoring
agent publishes the authorized draft and owns its CI follow-through. Final review
and merge ordering remain with the maintainer. No release, deployment, provenance
attestation or game execution is part of this change.
