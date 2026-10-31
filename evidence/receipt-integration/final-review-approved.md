# Final independent focused review — receipt integration

**Verdict: no blockers found within the reviewed synthetic local spike scope.** This is not a production-readiness or activation claim. Reviewed the dirty working tree at HEAD `8ac7468cf425f41543feeca144bd09f7b0b797de`; source hashes before and after the runs are recorded in `final-review-source-before.sha256` and `final-review-source-after.sha256` and match.

## Findings and prior fixes

- The P2 in `final-review.md` is resolved for normal admission: `receipt_protocol.h:25–29` rejects a new peer's noninitial ID or a full pending queue, and checks the peer cap, before inserting its peer record. Existing admitted peers retain their tombstones. `receipt_tests.cpp:14–15` exercise gap and saturated-queue rejection without consuming slots, followed by valid admission. The earlier pre-fix red evidence is `peer-cap-red.log`.
- `Old` remains unknown rather than refused in the CLI; the accumulator allows it to yield to a definitive result without replacing an existing definitive result. Tests cover synthetic delivery orders, not actual eviction-to-delivered-Old ordering end to end.
- Conflicts are discarded before they can overwrite an original outcome; pending conflict followed by application is tested. Standalone conflict reporting and correlation of host results to outstanding commands remain absent.
- Wire command delta and result fields are checked before narrowing; tests cover high-bit noncanonical input. This is not exhaustive wire validation.
- CLI tests preserve distinct synthetic compositions: solo local/local, listen local/remote, dedicated remote/remote; all require applied outcomes. They do not prove actual multi-process topology.

## Executed verification

- `cmake --build build-spike -j 4` succeeded; `ctest --test-dir build-spike --output-on-failure`: **5/5 passed**. `build-spike/authority-cli test`: **16 PASS lines**, legacy-path coverage distinct from receipt-aware tests.
- `cmake --build evidence/receipt-integration/asan-ubsan-final/build -j 4` succeeded; CTest there: **5/5 passed** with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. Its CMake cache specifies global C and C++ `-fsanitize=address,undefined` and executable linker sanitizer flags. Leak detection was disabled; this is macOS-only evidence.
- Full command outputs are in `final-review-approved-native-build.log`, `final-review-approved-native-ctest.log`, `final-review-approved-legacy.log`, `final-review-approved-sanitizer-build.log`, and `final-review-approved-sanitizer-ctest.log`. Source hashes matched across the runs; no concurrent CTest/build was observed at the initial process check.

## Scope and limitations

No real topology, SC world, persistence, restart durability, Windows/Linux, production activation, or remote operation was tested. Finite tombstone capacity, bounded receipt retention, process-local non-negotiated epochs, absence of automatic retry/liveness and result correlation remain limitations. Oversized receive testing checks rejection and zero length, not buffer preservation or subsequent packet recovery. No source files, commits, or remote state were modified by this review.
