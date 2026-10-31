# Local chat, installer and opt-in authority integration

This branch is local-only, not a publication or product acceptance.

## Inputs

Read-only refreshed remote main: `57ac2fd3e6fd3e23a974d58c33fbd4935b9d12f0`.
Complete intended linear series included:
- Nine chat commits after main through `02edf442479e3bb0857ef062d6c61f9b0d05124a`.
- Fourteen boundary/installer commits after main through `917b3fd3201a9325c56efbf048d94ecf66bef322`.
- Experimental `authority-sdk.patch` from the separately reviewed published rc2 spike. Its manifest, independent review and ADR were read before application.

Both contribution provenance rows and pending rights review remain intact. No failed registry-owner-drain work is included. No sibling source-owner checkout is linked or changed.

## Integration corrections

Independent review identified that the published rc2 service returns `SCO_TOO_MANY` before command admission. This refusal now preserves the draft and local allowance; unspecified potentially ambiguous errors retain an unconfirmed history row. Transport admission remains different from application acceptance.

The source-preflight installer tests are now registered with CTest. The existing runtime-library gate now also recognizes the standard aarch64 Linux dynamic loader. The spike's probabilistic replay assertion was replaced with a separate deterministic authenticated DATA replay check; loss/reorder/duplication coverage remains.

## Reproduce

Use a Linux C++20 toolchain, Python 3, CMake and Unix Makefiles. The default configuration verifies the pinned published public SDK and core-source archive hashes; `SCO_SDK_ARCHIVE_DIR` can point to downloaded release assets. Use a writable disposable source copy: installer hermetic tests create temporary fake-system directories below their source root.

```sh
cmake -S . -B build -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The authority experiment is deliberately absent from the production build and packaging. Opt in separately after extracting the hash-verified core-source archive:

```sh
cmake -S spikes/authority-sdk -B build-spike -DSDK_ROOT=/path/to/sco-sdk-core-source-2.4.0
cmake --build build-spike -j4
ctest --test-dir build-spike -V
```

Actual integration receipts are local ephemeral files under sibling `sc-mp-integration-evidence-20261030`; original spike ADR paths are historical local observations, not durable downloadable artifacts.

## Scope retained

This is synthetic service/module testing and real UDP loopback relay testing, not real LAN, game, Windows or deployment acceptance. Installer tests execute against simulated command/system seams, not a privileged installation or real NSS/ACL service deployment. No production-shell composition with this synthetic authority is established.

The spike uses the published SDK's actual authenticated net Core with an in-memory datagram medium. Solo/listen/dedicated use the same three-Core session topology, not distinct executable lifecycles. Authority ingress overflow has no sender application-acceptance acknowledgement. Its ingress is bounded, but the archive test medium's in-flight vector is not; whole-path bounded-medium claims remain unproved. Epoch allocation is volatile, not durable across process restarts. No entity ownership, replication, snapshots, persistence, reconciliation or SC world provider is implemented.

Independent review also noted a pre-existing checker limitation: versioned shared-library link tokens and foreign paths reusing approved static archive basenames are not comprehensively identified. Existing no-game-code checks passing is not proof against every malicious linker input. Contributor rights remain pending maintainer review.
