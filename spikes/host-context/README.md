# Local host context experiment

This is a trusted-host, in-process synthetic adapter, not a plugin service or an
approved SDK ABI. The one runner links the real `sco_authority` into its main
executable. It creates the executable authority, binds, reserves an empty mock
graph, starts and publishes the existing typed mock provider, and finishes
startup. The adapter adds no table slot or production worker and does not load a
plugin, use a socket, or access a game. The separate maintenance test fixture
starts its own owned worker. The core input is the separately extracted source kit
at commit `4aef497723a99a6bb5e19c65f4b9ecf65d30cf19` (#315 HOLD); the literal
inventory hash is checked before CMake adds that source tree.

The trusted host supplies a nonzero opaque 128-bit session namespace. Its
uniqueness is the host's responsibility; this experiment provides no durable
identity, persistence, or cross-process uniqueness guarantee. A world is an
opaque 128-bit value; zero is unknown. Reads require both a known world and a
synthetic provider. Native providers are rejected. Mode and transport are
independent copied metadata, and relay alone never establishes authority. An
optional nonzero authority peer is copied only when explicitly known. These
values are fixture claims, not evidence of native availability or authority.

Successful replacement and leave each increment a 64-bit generation. Replacement
overflow leaves the old state intact; leave overflow faults the session into
FailedRetained so it cannot remain readable. `ReadExpected` fails without modifying output when
the generation differs. Calls preserve the caller's size prefix and require a
full current output structure. All admitted operations keep their actual core
call pin through validation, locking and output copy. Failure to enter does not
modify output. Stop closes and retires without a self-held pin. Both successful
and timed-out retirement retain backing and prohibit new calls; the adapter and
its core binding/session are intentionally never freed. Startup is a single
trusted host control action per process, and no concurrent startup is supported.

Callers supply valid, non-overlapping C++ objects and actual storage for their
advertised size. This is not pointer validation or a portable C ABI. On success,
the known semantic fields are written, the caller's size is preserved, and all
extension bytes beyond the known structure remain untouched. Padding inside the
known prefix is unspecified on success. Every non-Ok result preserves the entire
caller output buffer, including padding, size, extensions and canaries. A failed
leave at generation exhaustion still invalidates the adapter's internal state;
it does not modify the caller's generation output.

`ReadExpected` checks generation when its snapshot is captured under the mutex.
A later replacement, leave or close can finish before that admitted read returns;
the captured copy stays valid for its recorded generation. This is not a guarantee
of latest state at return. New reads follow the current generation and admission
state; preserving an admitted old copy does not authorize a new stale read.

Build with the specified kit and run the independent processes. Direct CMake
verification requires Python 3.8 or newer:

```sh
cmake -S spikes/host-context -B /tmp/host-context-build -DSDK_ROOT=/path/to/extracted/sco-sdk-core-source-2.4.0 -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/host-context-build --target host-context
ctest --test-dir /tmp/host-context-build --output-on-failure
```

`basic` prints the copied context before and after replacement and the leave and
stop outcomes. `overflow` seeds the generation only in the test runner. `drain`
holds a caller thread at a typed atomic test rendezvous after its copy was
captured but before output is written. `drain-now` retires at the current time;
`drain` uses a finite future deadline, so a missing pin would allow quiescence.
Both must report FailedRetained, then the admitted call completes its copied
output. This is a test caller, not an adapter worker. Each CTest entry starts a separate
process because the reservation is terminal. The test-only barrier and seed are
local to this runner; core is built without `SCO_RESERVED_SESSION_TESTING`.

The current candidate supplies `SCO_RESERVED_DRAIN_TESTING`, enabled only for
the test authority and runner. Its monotonic `ReaderDrainObserved()` means an
actual retire call saw Closing with admitted readers while holding the core lock.
It neither reports quiescence nor authorizes cleanup, and installs no callback.
`success-drain` keeps a copied output held until that observation, then releases
it before the retirement deadline and requires `QuiescedRetained`. A retire-thread
launch or a timed sleep is not accepted as evidence of waiting. The early-pin
negative control must fail this rendezvous. `leave-during-copy` invalidates the
current context and refuses fresh reads while a previously captured old copy
completes successfully. That leave-only case does not by itself prove pin lifetime.

Eleven separate CTest scenarios also include `output-contract`, `output-overflow`,
`coherence` and `replace-during-copy`. The output tests use physically extended objects and compare
complete byte snapshots on errors; successful operations check both advertised
and additional guard bytes. Coherence runs 128 replacements with one test caller,
checks correlated fields and stale results, and retains independent returned
copies. This bounded test is not proof of arbitrary concurrency or race freedom.
The deterministic replacement case holds a read after capture, commits a newer
generation, then verifies that the admitted read returns its independent older
snapshot while fresh reads see the replacement. An old expected generation is
subsequently refused without changing the caller output.

`owned-maintenance` separately exercises the candidate's real host-source
`sco::OwnedShutdown` API. Its fixture-owned worker is held through its body and
TLS destruction: a failed drain remains closed, maintenance reports pending
until the worker and TLS complete, and then reports `ReapedRetained` without
restoring readiness or admission. It also checks fresh, successful-drain and
worker self-reentry refusals. Its three owners remain process-rooted. This is a
companion fixture: it does not reap or control the adapter's separate reserved
session, and it creates no production adapter worker or provider slot. Timing
deadlines bound waits but do not establish a hard wall-clock bound for the
core API's scheduling, mutex acquisition or joining.

For repeatable offline qualification, first package the exact clean core
candidate above with its `sdk/package.py --profile core-source`. The expected
archive SHA-256 is
`b4aa9ab7f10f488910334f8e5a64668ac540d2ad2d1b18ed18e0648e84a747ff`.
Then run:

```sh
python3 spikes/host-context/qualify.py --core-archive /path/to/sco-sdk-core-source-2.4.0.zip \
  --output /path/to/new-evidence --cmake /path/to/cmake \
  --cc /path/to/clang-19 --cxx /path/to/clang++-19
```

Use a different fresh output directory with `--sanitize` for Debug ASan/UBSan,
leak detection enabled and UBSan failures fatal. The qualifier snapshots the six
adapter files, both verifier files and the negative-control test source, checks
their hashes, runs the verifier tests, and verifies the archive and extracted
inventory before building. It records tool versions, commands, exit codes,
source/log/binary hashes and JUnit output in `receipt.json` and neighboring logs.
It refuses existing output directories and retains failures. It performs no
downloads or publication; its source hashes also identify an unpacked snapshot
without Git metadata. Generated qualification files are evidence, not a release.

The verifier checks the regular-file inventory and content, rejects `..` root
path components before normalization, and rejects symlinks and special files in
the extracted tree. Empty extra directories are
allowed. The qualifier checks archive entry type and path consistency before
extraction; see [the verifier tests](../../tests/host-context/README.md) for
exact bounds and metadata coverage. Qualification assumes private, stable
output and inputs; it does not defend against hostile concurrent tree mutation
or establish artifact origin. Qualification currently targets Linux/POSIX; all
21 verifier tests must execute without skips, including FIFO and symlink cases.
The newly written verifier and tests were informed
by the earlier `cmake/VerifyExtractedKit.cmake` from commit
`86b8169095b3dea703dbf281b4596cf4a1e92c89` and the previous qualifier; this is
neither a clean-room claim nor a determination of underlying contribution rights.
