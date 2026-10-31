# Provisional network boundary review — 2026-10-31

This is a source and synthetic Linux review, not approval of a production SDK,
Windows build, LAN session, or game behavior. The coordinated core #304 head
below is still a draft; another core source requires a fresh review and pins.

## Compared inputs

- Published RC2 source: `844e136ccb280ad8cb43f73bc43c9447e8f6799d`;
  core-source archive SHA-256
  `ddd1d75055f6b0efd69af4ac2eff6a18a5ff177f87cb28c938af3a625befbbb8`.
- Provisional core #304 source: `1166c9f12d7327dd50a58b1d9261e7d6cc21bef6`;
  locally built core-source archive SHA-256
  `c2cce3d6a0347af5450eaa44e1cebd5a64d957f222aefaf68d1584881fd173bc`
  and legacy SDK archive SHA-256
  `8fa4097adf980e182695083fe8a164716ae7200fd1715f19af35a98e0f40a9d5`.
  These artifacts exercise the development path; their origin and the final
  core inputs must be confirmed independently by the core integration owner.
  A second packaging from the same clean checkout and environment produced
  byte-identical copies of both archives; this is not independent verification.
- Coordinated core #304 head: `571fe8e2c911d7c63c372103724896feaabe58f2`.
  The 16 reviewed network files are unchanged from `1166c9f`. Locally packaged
  legacy SDK SHA-256 is `67f4f837879867ac6d953a2953c448ce7913a9da924d6e04931a48988f23b9c1`;
  core-source SHA-256 is `35242e00989de3afd61f24a7a67dfd4958c3331222bea9e121f952452477d1ab`.
  The `dev-571` profile binds this pair. These local hashes do not authenticate
  the source or establish publication of an SDK.
- Historical sc-mp source-first branch `f91582b20fc9ada848ce4223e4e408d460ff2cb7`
  was context. Its 16 checked network blob IDs were independently compared
  with #304 source `1166c9f`: all 16 matched.

## Reviewed differences

The 16-file set covers the previous 15 checked files plus `src/net/udp_win.cpp`,
which is compiled on Windows but was missing from the old boundary list. Only
these three files differ between the compared sets:

| File | RC2 git blob | Provisional git blob |
| --- | --- | --- |
| `include/sco/net/udp.h` | `15d4bd57f5f3be2246be16d3d7ddb8da62c19675` | `a61e895e059f864a7d84231fb00eb654d5a2e7b2` |
| `src/net/udp_posix.cpp` | `62402b44d666a6e1a5d7a79adce5c76d2fa3be22` | `86244e35725136ec0ecfdf432ed7c891f6356591` |
| `src/net/udp_win.cpp` | `69df8298fd693f34686176293f34db5562c3a422` | `9ebdbff02d1f74d638599dfd2b04daff72f780fa` |

The header adds `RecvBufBytes()`. Both socket implementations request a larger
receive buffer when opening and add a getter. Reviewed bind, send, receive,
scope, authentication and replay code was otherwise unchanged in the checked
files. The relay still wraps its UDP transport in `ScopedTransport`; filtering
occurs after kernel receipt, so unwanted datagrams can still occupy socket
buffer space before they are dropped.

The requested buffer is best effort: calls ignore errors and OS limits can
clamp the result. On Linux the code tries `SO_RCVBUFFORCE` and then
unconditionally `SO_RCVBUF`; a successful privileged force can be superseded
by the latter. The larger kernel queue consumes more memory and does not prove
lossless reception. An unprivileged local Linux socket reported 4,194,304 bytes
after the request in this environment; this is an observation, not a portable
minimum. Windows compilation and runtime, including MSVC `/W4 /WX`, remain
unverified for the provisional source.

The boundary's blob checks detect changes to the reviewed files, not the
provenance of an entire SDK. Archive hashes and source metadata must still be
verified against independently obtained expected inputs. A claimed source or
a locally generated hash is not independent evidence of origin.

Relay configuration binds each profile to its exact archive pair and checks
the extracted SDK and core-source inventories before consuming those trees,
including with tests disabled. This checks extraction consistency at configure
time; it does not authenticate the archive producer or prevent subsequent
changes to a writable build directory.
