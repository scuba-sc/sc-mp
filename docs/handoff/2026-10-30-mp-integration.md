# sc-mp handoff — 2026-10-30 (draft)

Branch `handoff/mp-integration-20261030` = main `57ac2fd` + 4 commits:

| Commit | Change |
|---|---|
| `364ea34` | Full chat series (#14 concern: ambiguous sends show "delivery unconfirmed") + hardened relay installer series (#15 concern: same-name/different-UID group bypass fixed; config ACL + source identity preflights) |
| `618f010` | Opt-in published-SDK authority experiment (`spikes/authority-sdk/`, ADR-0002) |
| `8ac7468` | Local integration receipts and retained limits |
| `82a70dc` | Bounded application receipts in the synthetic authority CLI (ADR-0003) |

This branch supersedes the code in open PRs #14 and #15 for review purposes;
those PRs were not modified. Canonical cross-repo runbook: sco-core branch
`handoff/core-integration-20261030`, `docs/handoff/2026-10-30-cloud-handoff.md`.

## Verified (Linux Debian 12 container + macOS sanitizers)
- Combined CTest 10/10 (module workflow, relay UDP/program, 42 installer hermetic tests, source/network boundaries).
- Spike: 4 executables, 5/5 CTest entries; 16 legacy authority scenarios; whole-SDK ASan+UBSan 5/5 (leak detection unsupported on macOS).
- CLI: solo/listen/dedicated applied value 12 exit 0; invalid refused exit 1; old unknown exit 1; `sc-world` unavailable exit 1.
- Links the **published** rc.2 `sco_net` (archive hashes in `evidence/manifest` files); real SDK handshake, origin auth, reliable delivery over the SDK in-memory datagram fixture.
- Independent post-fix review: no blockers within synthetic-only scope.

## Not established
- The single-authority architecture is a **recommendation, not owner-approved**. Spike is experimental, opt-in, synthetic-only.
- All modes share one in-process fixture; no real sockets/LAN/process topologies.
- Incarnation uniqueness is process-local; no persistence, crash recovery, durable outcomes.
- No entity ownership, replicated snapshots, late join, or SC world.
- Chat production text-widget handoff and installer on a real privileged VM unverified.
- Windows not run for this branch.

## Next increments (in order)
1. Separate-process listen-host and headless topologies over real loopback sockets.
2. Negotiated/durable incarnation identity with storage proof.
3. Entity ownership + snapshot/late-join.
4. Only then bounded distributed compute (peer results untrusted, never grant ownership/inventory/outcomes).
