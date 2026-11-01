# Isolated kit verification

The new CMake wrapper invokes `cmake/verify_host_context_kit.py` using a Python
3.8-or-newer interpreter and process arguments; it does not invoke a shell. It
runs before `add_subdirectory`. The verifier checks the literal inventory digest,
all regular-file names and hashes, and the exact regular-file set. It refuses
`..` root path components before normalization, symlinks in the root path or
kit tree, and other special files.
Extra empty directories are allowed. The directory must be private and stable
while checking and building: these checks do not defend against concurrent
hostile replacement or prove the origin of an otherwise matching inventory.

The qualifier hashes and inspects the same in-memory archive, capped at 64 MiB;
archive entries are limited to 2,048 and 64 MiB uncompressed in total. It rejects
absolute, noncanonical, parent, backslash, NUL and duplicate paths; case-fold
collisions and file/directory conflicts; and Unix metadata identifying symlinks,
special files, or a file type inconsistent with the trailing slash. Explicit
directory entries with matching metadata are permitted. A ZIP entry with no
Unix mode is accepted based on its name; the verifier does not attest all ZIP
metadata, permissions, timestamps, ownership or compressed contents' origin.
The candidate's literal archive and inventory hashes remain in the qualifier;
test fixture-specific pins are injected only into the imported test process.

The verifier and its tests were newly written for this isolated experiment after
reviewing the earlier `cmake/VerifyExtractedKit.cmake` from sc-mp commit
`86b8169095b3dea703dbf281b4596cf4a1e92c89` and the host-context qualifier at
`deaf7c4b2abf58083d65131734799ad23a43a832`. They are informed by that prior
work; this is not a claim of independent creation or a resolution of the
underlying rights questions. The coordinated core candidate is merged to main
and labeled `sdk-v2.4.0-rc.3`; candidate qualification does not establish final
release promotion or native host-context support.

Run the checks against the exact reviewed candidate archive:

```sh
python3 tests/host-context/test_verify_kit.py --cmake /path/to/cmake \
  --real-archive /path/to/sco-sdk-core-source-2.4.0.zip
```

Fixture-specific expected hashes allow each malformed archive or tree to reach
its intended validation path. The direct-CMake fixtures place a sentinel in the
candidate's `CMakeLists.txt` and require all refusals to happen before it runs.
The qualifier snapshots and pins this test source and both verifier files,
runs the tests before extraction, and rechecks the kit and source after the
build. The runner requires exactly 21 executed test methods, with no skips or
expected failures. Qualification currently targets Linux/POSIX: the FIFO and
symlink cases require platform support and permissions. If a case cannot run,
qualification fails rather than treating a skip as a pass. These tests do not
validate arbitrary concurrent changes or plugin/game behavior.
