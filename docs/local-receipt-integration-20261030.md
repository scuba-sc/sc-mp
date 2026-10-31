# Local receipt integration — synthetic-only

Additive clone: `<LOCAL_HOME>/.hermes/cache/scratch/sc-mp-receipt-integration-20261030`.
Base: `8ac7468cf425f41543feeca144bd09f7b0b797de`. Original integration tree unchanged.
Increment patch SHA256: `841f8d9d7a8f01cf07aa5e334615b54edeadf8b0a6eb61e11745cf824bf3a0eb`.
The increment manifest names its older `57ac2fd3e6fd3e23a974d58c33fbd4935b9d12f0` parent and 15 scenarios; these are historical, not this integration's base or count. Patch applies cleanly; final differences from the increment are intentional and recorded in the source manifest.

## Result

Receipt-aware CLI uses a shared SDK fixture, not the old counter-only CLI branch. Solo admits two local receipt commands; listen one local and one SDK remote; dedicated two SDK remote. All are synthetic in-memory compositions, **not real process or socket topologies**. Applied is only reported after application results; permanent Invalid is refused, Old is unknown prior outcome. CLI retries cap at four attempts. The old authority remains only as the preserved regression fixture: **16 scenarios**, including deterministic authenticated DATA replay rejection.

Integration fixes reject noncanonical delta/status/value encodings before narrowing; conflicting variants cannot poison the original result. Old observations cannot freeze later definitive results. Gap/queue refusals do not consume peer tombstones. Bounded medium receive refuses truncation. GPL source attribution for the adapted SDK test medium is recorded in PROVENANCE.md; pending contributor-rights rows remain pending. Existing chat and installer source is untouched.

## Verified evidence

Evidence root: `evidence/receipt-integration/`.

- Final Linux Debian 12 source snapshot: `final-linux/source-manifest.json`, `snapshot-verification.json`, `source-post-verification.json`.
- Combined CTest **10/10**: `final-linux/ctest.log`. Includes actual built-module workflow, relay UDP/program, installer hermetic tests, and source/network boundaries.
- Four spike executables, **5/5** CTest entries (extra Python CLI gate): `final-linux/spike-ctest.log`.
- Legacy **16** checks: `final-linux/legacy.log`.
- Actual CLI stdout and exit files in `final-linux/`: solo/listen/dedicated applied value12 exit0; rejected refused/invalid exit1; old unknown/old exit1; sc-world unavailable exit1.
- Actual linked published-SDK consumer: receipt-integration runs authenticated Core command admission/application/results with DATA loss, retransmission, retry, duplicates, receipt eviction and queue/medium saturation. Production module workflow loads the freshly built plugin. Artifact hashes: `final-linux/artifacts-sha256.log`.
- Whole-SDK ASan+UBSan: `final-review-approved-sanitizer-ctest.log`, **5/5** after final peer-cap fix; global C/C++/link flags instrument SDK and consumers. macOS does not support leak detection here; passing runs use detect_leaks=0 and do not establish leak coverage.
- Independent review after fixes: `final-review-approved.md`, no blockers within synthetic-only scope. Prior findings and failed reproductions are retained, not presented as approval.

Initial Linux runs failed when Git was absent from the container; final clean run installed Git. Initial sanitizer invocation failed with unsupported detect_leaks=1; corrected runs are separately identified.

## Limits

No production capability enabled, owner checkout edits, registry prototype, Windows, game/SC world, LAN/distributed processes, remote writes, deployment, push or merge. Epoch discovery is fixture-shared, not negotiated; incarnation uniqueness is process-local. No persistence, restart durability or distributed worlds. At-most-once mutation and receipt recovery are bounded by incarnation/admission/retention; Old refuses reapplication without claiming a known original outcome. Retained peer tombstones cap at16 admitted peers; receipts8 per peer, ingress16, client observations32, shared medium256 datagrams/262144 bytes. No general liveness or total-memory ceiling claim.
