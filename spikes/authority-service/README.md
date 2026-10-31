# Synthetic authority over the SDK service

This local fixture runs the bounded integer policy from `../authority-session/session.h`
as an `authority` built-in loaded by the packaged `sco::app` host. The plugin
uses `sco_api.h` and `sco_net.h`, registers `authority.in` and `authority.out`,
and takes remote peer identity only from the network callback. The host owns
Start/Tick/Stop and explicit loopback Host/Join. Its private-session broker is
disabled; these test sessions are not game admission. The SDK's normal network
service and authentication work factor are used. The private, copied control
table is a fixture seam, not a published or proposed SDK service.

The CLI, line-oriented control commands, and JSON event schema match the prior
fixture: `solo`, `listen PORT`, `dedicated PORT`, `client PORT`, and fail-closed
`sc-world`. Inputs are `snapshot`, `command EPOCH ID ENTITY DELTA`, `quit`,
and `disconnect`. Session state and receipts remain volatile and bounded as in
ADR 0004. A discarded application receipt is retried only by the same live peer.
No native or game provider is present; this was not run in game. The test
passphrase is synthetic and traffic is loopback only.

Configure using a checked SDK extraction; the build verifies the complete pinned
inventory before adding SDK sources. For example:

```sh
cmake -S spikes/authority-service -B /path/to/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DSDK_ROOT=/path/to/sco-sdk-core-source-2.4.0
cmake --build /path/to/build
ctest --test-dir /path/to/build --output-on-failure
```

The process test reuses the prior fixture's harness. Additional tests reject
missing, truncated, and incomplete network tables; exercise loader owner release
and host-stop invalidation; and deliberately discard an application receipt
before retrying it over the same live peer. This does not simulate packet loss or
prove a production provider, deployment, or game integration.
