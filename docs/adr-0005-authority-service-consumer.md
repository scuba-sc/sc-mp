# ADR 0005 — synthetic authority as an SDK service consumer

Status: implemented local functional milestone. Base: sc-mp
`a5b1efc1d170f8dfed3c1b28b53a56d610b5b129`; SDK input: merged core
`7ee83fa95680aa482d8b160987b472547a67b888`. Not run in game.

The previous process prototype drives `sco::net::Core` directly. That proves a
bounded synthetic authority policy over authenticated transport, but does not
prove that a plugin can implement the same policy using the packaged service ABI.
The next milestone puts the policy inside a built-in plugin loaded by the real
packaged host. It uses `sco_api` service lookup, live plugin ownership, `sco.net`
channels, and host-tick event delivery. The host alone starts/stops the network.

## Composition

| Component | Owns | Boundary |
| --- | --- | --- |
| Fixture host process | Host startup/tick/stop and explicit loopback Host/Join | Packaged host-source APIs; no game process or provider |
| Built-in synthetic authority plugin | Bounded entity policy, receipts and context messages | Public `sco_api.h` and `sco_net.h`, project-authored synthetic policy |
| Local fixture control bridge | Copied test inputs/results between runner and built-in | Experimental local test seam, not an approved SDK authority contract |
| SDK network service | Authenticated peer origin, channels, delivery queues and events | Actual packaged implementation; no substitute transport/authentication |

Solo uses the same local policy with no active transport session. Listen adds a
local participant to a host that receives remote SDK callbacks. Dedicated has no
local participant. A client receives context/results from the host and submits
intents on its plugin-owned host-directed channel. The existing relay continues
to forward co-presence messages and has no simulation role.

Configure and test-control operations originate in the local fixture, never a
peer message. The synthetic host does not claim game admission. The private-session
broker must be disabled when the fixture uses direct host session APIs: there
must be one session-control owner. The socket remains loopback only. No security
policy, authentication implementation or product admission behavior is changed.

## Existing contracts to exercise

- `sco_api.h`: full member-extent and pointer checks before service lookup and
  subscription; built-in load receives a real live owner. Stop/unload withdraws
  plugin work through the actual host lifecycle.
- `sco_net.h`: full v1 extent and required-member checks; plugin-id channel
  namespace; authenticated callback sender rather than claimed payload identity;
  reliable host-directed and host-only channels; peer/state event extent checks.
  A send success is queue admission, not an applied receipt.
- `sco/app.h`, `sco/plugins.h`, `sco/net/session.h`: host-only composition,
  tick delivery, loader-owned plugin handles and explicit session teardown.
  These are not headers a plugin may use to acquire socket/control authority.
- Existing synthetic session policy: 16 lifetime peer slots, eight receipts per
  peer, immutable payload identity, bounded deltas, stale epochs and ownership
  refusals, same-live-peer-only receipt retrieval. All state remains volatile.

Negative fixtures should distinguish a missing/short service table from an active
transport session. No available `sco.net` table alone implies connected peers,
an authority provider, or a game capability. A configured fixture role is not a
provider readiness decision.

## Concrete SDK gaps exposed by the consumer

1. `game.private_session` returns request acceptance and scalar state; `sco.net`
   exposes session-local peer IDs. Neither supplies copied session/world identity,
   authority role, provider principal or invalidation generation. This experiment
   must negotiate its own synthetic context; products cannot reuse it as a native
   context service. A production context contract needs a named owner and lifecycle.
2. `game.entities` and `game.owned` express local plugin ownership and removal
   initiation. They do not grant remote principals authority over local game
   entities. An explicit host-owned peer-to-principal/entity mapping and typed
   intent provider are needed before networking gameplay mutations.
3. Channel reliability does not report provider admission or completion. The
   experiment's receipts are application policy. A production intent contract
   needs bounded admission, completion, duplicate/conflict semantics and provider
   absence, thread and teardown rules grounded in an executing provider.
4. Per-plugin `sco.storage` is not an atomic shared-world/deduplication transaction.
   Saving preferences cannot make a lost receipt recoverable after disconnect or
   restart. Durable recovery remains a separate milestone with single-writer
   fencing, atomic state/receipt commit and crash tests.

These are requirements, not new ABI declarations. Do not create duplicate
transport/session APIs or add a nominal authority header without a working
provider. The synthetic plugin first establishes whether existing public service
composition is sufficient for this small policy; actual failures must determine
any prerequisite SDK fix. Active core lifecycle work is independently owned and
is not assumed complete or modified here.

## Acceptance and continuation

Build against the exact checked core-source extraction, with no sibling SDK source
include path. Exercise solo/listen/dedicated/client through real separate process
and host-tick delivery, then verify foreign ownership, stale epoch, duplicate and
evicted receipts, explicit reconnect and stop. Add missing/short-table refusal
and owner cleanup checks that identify whether inputs are stand-ins or actual
host operations. Carry exact source, archive, binary and test identities in the
local evidence manifest.

Clang 19 Release with strict warnings builds the full packaged host and both
fixture executables. Independent execution passes three CTest entries: all 15
existing process cases, ABI/partial-load/owner-lifecycle fixtures, and one
additional application-receipt discard/retry process case. Missing/short-table
and selected failure-injection inputs are stand-ins; built-in loader startup,
owner release, host stop and process networking use actual SDK implementations.
No new SDK API was required for this synthetic service consumer.

Full SDK/fixture ASan+UBSan validation also passes 3/3. The initial instrumented
run exceeded the inherited three-second context wait: a measured normal SDK
authentication took 4.426 seconds. A service-specific wrapper retains the same
15 cases and assertions with finite ten-second event/exit waits. Its original
three-second `sc-world` refusal bound remains intact. Independent full Release
validation passes 3/3 on the final wrapper. The full instrumented run loaded the
wrapper just before that unrelated global deadline was narrowed back; a separate
fresh-module check verifies the final instrumented wrapper's original refusal
bound. Authentication and SDK settings are unchanged. Initial
failed logs and corrected results remain separately identified in the evidence.

The runner's test-only control bridge is local code, not an SDK-discovered
authority service. The plugin is a real built-in receiving a loader-owned
`sco_plugin` handle, not a dynamically loaded module or an extracted plugin DLL.
No native module reload or Windows qualification is established. Full `sco::app`
startup also initializes other generic services; this milestone is not a
minimal production dedicated-host resource profile. GCC 14 O3 retains the
previously isolated SDK vector diagnostic; no warning was suppressed.
The owner-release fixture does not establish arbitrary concurrent unload or
callback-in-flight safety. The backpressure test intentionally exits without
normal teardown, and a crash test kills a child; their passes do not establish
leak reporting for those children. No Windows/native module qualification is made.

Keep the engine's transport sandbox as an integration customer of this seam. Its
state-only private-session mock is not authenticated multiplayer evidence.
Offline integration still needs explicit local consent, positive game admission
and an executing provider. Native/game validation is deferred to the human; no
registry capability is promoted by these synthetic tests. MP #16's inherited
provenance decisions remain pending and independent of this experiment.
