# ADR 0004 — volatile authoritative entity session

Status: local prototype; acceptance and exact inputs are recorded in the accompanying
[runbook](handoff/2026-10-31-authoritative-session.md). Not run in game.

## Decision

Extend the bounded receipt experiment with one synthetic integer entity per admitted
participant and real, separate processes. Keep this in `spikes/authority-session`,
outside the production plugin and relay. The authoritative process alone owns the
entity table and serializes validation, mutations, receipts and disconnect cleanup.
The integer is a deliberately small simulation state, not a representation of a
Star Citizen entity, inventory, physics body, mission or economy.

`sco-relay` continues to authenticate membership and forward co-presence traffic.
Forwarding does not confer simulation authority. This experiment is a separate
authority endpoint; it neither embeds the relay nor changes its protocol.

| Composition | Simulation owner | Input path | Process boundary |
| --- | --- | --- | --- |
| Solo | Local authority object | Local participant, same validation/receipt policy | One process; no socket |
| Listen | Authority in the host process | Local participant plus authenticated remote clients | Host and client processes |
| Dedicated | Headless authority | Authenticated remote clients only | Server plus client processes; server is not a participant |

Networking uses the exact development SDK's `sco::net::Core`, its UDP socket and
`ScopedTransport`. The test executable binds and connects only to loopback. There
is no custom handshake, cryptography, account service or raw endpoint-to-player
identity substitution. The fixture's shared test credential and reduced password
stretching are not suitable for a deployment. Authentication is not encryption.

## Identity and authority

A startup allocates a nonzero random 128-bit epoch. The synthetic world's identity
is scoped to that epoch. Random uniqueness is probabilistic, not a durable,
monotonic allocator or a fencing token backed by storage. A context announcement
from the authenticated host gives each participant its current epoch, peer and
server-allocated entity. Clients learn context through the protocol, rather than
sharing an authority object as the earlier fixture did.

The authority accepts a command only for its current epoch and the entity assigned
to the authenticated originating peer. The wire contains no claimed sender:
`Message::from` supplies that identity. Local listen/solo input uses the host's
reserved local participant and the same policy. Knowing another entity's numeric
handle grants no write authority. Neither SDK peer ids nor entity handles are
durable identities. A complete entity reference is `(epoch, entity)`.

Only the owning process changes state. An intent is a bounded integer delta;
the authority validates ownership and limits before applying it. This is not a
remote transform setter. All operations occur on one serialized execution loop;
there is no worker thread, queued game-thread callback or native provider.

## Commands, receipts and state observation

The command key is `(epoch, authenticated peer, command id)`. IDs start at one,
are consecutive and never wrap. The immutable payload includes target entity and
delta. A transport send success means only SDK queue admission. The application
receipt establishes application outcome.

- An identical retained command returns its original terminal receipt, including
  the value/revision observed at that command. It does not mutate again.
- Reusing a retained ID with a different payload returns `Conflict`; this is a
  diagnostic about the variant, not a replacement outcome for the original.
- A future nonconsecutive ID returns `Gap` without consuming the ID.
- A rejected admitted intent has a terminal receipt, so retry cannot turn it
  into a different operation. Stale epochs are never admitted.
- Retention eviction returns `Old`: the earlier outcome is unavailable, and the
  command will not be applied again. This is not proof that it never applied.

A bounded read-only snapshot provides only the requesting participant's current
integer value and the session's mutation revision. It is not a world/roster
snapshot or a late-join replication stream. The revision counts applied commands,
including a zero delta; joining and leaving do not advance it. A receipt is
historical; clients must not substitute its old value for a newer snapshot.
A snapshot supplies neither the command high-water mark nor the next ID. Callers
must retain their immutable command and sequence history; losing that history
requires an explicit new participant session, not guessing the next ID. A snapshot
cannot reconstruct an evicted command outcome. No automatic
retry scheduler or general progress guarantee is supplied. A caller may explicitly
retry the exact command within the current epoch and receipt window.

Receipts/context/snapshots use host-only channels; commands use host-directed
channels. Addressed result fields are routing information, not confidentiality:
other admitted participants may receive shared host-channel data. The prototype
contains only synthetic state.

## Lifecycle, persistence and recovery

Disconnect revokes the peer's entity and removes its active state. Its high-water
mark and bounded receipts remain as a tombstone for the epoch. Explicit reconnect
creates a new transport peer and entity; there is no authority transfer or resume
token. A silent client failure is observed through the SDK timeout, not instantly.
The implementation must not infer disconnect from a missing application receipt.

Process stop or crash loses **all** state, receipts and membership. Recovery is
explicit cold restart with a fresh epoch and empty world. Old epoch commands are
refused even if numeric entity/command IDs repeat. A response lost before the crash
has an unknown outcome in the former world; the new empty world does not prove it
was never applied. A graceful restart has the same persistence boundary.

There is no save/load, write-ahead log, durable receipt, host migration, failover,
world import or cross-machine storage. `sco.storage` offers local plugin storage,
not an authoritative world/receipt transaction. Adding a snapshot file alone would
not provide crash-safe deduplication. A later persistence milestone must commit
world state, receipt/high-water information and recovery generation atomically,
define ownership re-admission and single-writer fencing, and test torn writes,
ambiguous acknowledgements and rollback. It is deliberately excluded here.

## Resource bounds

The policy caps lifetime peer/tombstone slots at 16 and terminal receipts at eight
per peer. Entity slots are bounded by the peer slots. Departure does not recycle a
slot: after lifetime exhaustion, the operator must start a new epoch. Intent deltas
are bounded to ±100 and entity values to ±1,000,000. Counters refuse exhaustion.
Protocol payloads and control lines are limited to 256 bytes; channels are reliable
and scoped ingress is limited to 1,000 datagrams per second. Exhausted lifetime
slots produce an application `Overload` context with no entity grant, even if
transport admission succeeded. Process test deadlines are bounded separately.

SDK reliability buffers and scoped transport limits remain their own layers.
These limits do not establish an exact process RSS ceiling or a denial-of-service
guarantee against a malicious local OS user. Output is a supervised test pipe, not
a production telemetry sink. A full or closed output pipe fails closed by exiting;
it never blocks the simulation loop indefinitely. The process harness drains
normal output and cleans up every child, including on failed assertions.

## SDK and product boundary

No new ABI is introduced. Current service contracts remain intact and no capability
registry is promoted. `game.private_session` owns explicit local consent and
admission, but does not grant a shared world. `game.entities`/`game.owned` own local
plugin resources, not network participant authority. `game.world.control` offers
bounded local queries/camera operations, not a dedicated simulation backend.

A production integration needs separately reviewed session-context and typed-intent
contracts, a host-owned mapping from authenticated peers to provider principals,
thread/lifetime/drain rules, and a provider that can report truthful capability
availability. The engine can supply a synthetic provider first. The offline product
must retain fresh offline admission and explicit Host/Join; a remote packet must
never manufacture local consent. Provider absence remains unavailable. Active core
lifecycle work must land and be reviewed before a consumer builds on its semantics.

## Excluded claims

No Star Citizen simulation, native/game acceptance, LAN/VPN deployment, discovery,
public matchmaking, account identity, real assets, official services, live-world
replication, persistence, automatic reconnect, sharding, prediction/reconciliation,
anti-cheat work or compatibility promise. `sc-world` explicitly refuses. MP #16
provenance/rights decisions remain human decisions. This work does not resolve them.
