# Session modes and backend boundary

This note separates the current co-presence implementation from the broader multiplayer backend
work. sc-mp consumes the published sco-core SDK and keeps the relay and player protocol on the same
generation. The current implementation is protocol generation 2. It has not been run in game.

## Modes

| Mode | Current behavior | Authority and limits |
| --- | --- | --- |
| Local single-player | No `game.private_session` is active. The multiplayer plugin has no session to join and does not open a socket. | The local game process owns its own world. sc-mp adds no network state. |
| Listen-host co-presence | The game host requests a private session through `sc_private_session_v1`; its `sco.net` peer id is 1. Other game processes join with explicit local consent. | The host owns admission and the session transport. Every game still simulates its own world. sc-mp sends bounded poses, spawn/despawn notices and short plain-text chat lines (not encrypted) so each process can render local ghosts and show what players say. It does not make the listen host authoritative for another player's physics, inventory, missions, or damage. |
| Headless dedicated relay | `sco-relay` hosts the transport session as peer 1. It authenticates the shared passphrase, tracks a roster, forwards messages (chat lines too, which the program never reads or logs), and can optionally route nearby poses. | It has no game process, world, or simulation authority. Its `multiplayer.relay` roster lists players only. The relay is not a player and does not create ghosts. A relay process has at most 15 players because the transport session has 16 seats including peer 1. |
| Authoritative dedicated simulation | Not implemented. | This would need a game-side simulation provider and explicit authority/state contracts. `sco-relay` cannot become that backend by forwarding packets. No MMO or official-service integration is implied. |

The plugin status command and panel report the current topology from the existing private-session
state, local peer id, and authenticated host protocol role. A player-hosted session is described as
co-presence with local game state; a relay-announced host is described as message forwarding. These
labels report the protocol contract, not in-game verification.

## Existing service boundaries

| Surface | Owns | Does not provide |
| --- | --- | --- |
| `sc_private_session_v1` (`sc_private_session.h`) | Explicit Host/Join requests, Leave, and a local session state summary. Request consent is fresh and host-controlled; the product applies offline/EAC admission and owns bind/peer policy. | World identity, session discovery, host migration, or game-state authority. Plugins do not open sockets through this service. |
| `sco_net_v1` (`sco_net.h`) | Authenticated session membership, opaque peer ids (host is 1), bounded channels, host-only and host-directed delivery, and leave/state events. The transport authenticates messages but does not encrypt them. | A durable player identity, global session id, world id, application-level handshake refusal, shared simulation, or host migration. The current protocol announcement filters incompatible peers locally; it cannot reject them during transport admission. |
| `sco_storage_v1` (`sco_storage.h`) | Transactional per-plugin local persistence. sc-mp can store user settings and explicitly saved server profiles. Remembered passphrases are plaintext in that local database and are opt-in. | Cross-machine state replication, authoritative world saves, or a session directory. A local profile is a connection hint, not proof of a live session. |
| `sco-relay` (`server/`) | A LAN/VPN-scoped headless transport endpoint built with `ScopedTransport`, peer roster and forwarding lifecycle. | Gameplay entities, a world, or authoritative commands. It never connects to Star Citizen services. |

## Identity and lifecycle

- `sco.net` peer ids are opaque and unique only inside one live session. The host is peer 1; a new
  session may reuse the same numeric ids. They are not account ids and must not be saved as player
  identities.
- The current ghost message id `(peer_id << 32) | kind` is also session-scoped. The actor/entity
  id returned by a game provider belongs to that one local process. Never treat either id as a
  globally shared entity id.
- The current pose uses observed zone names and positions, not a stable world or instance id.
  Zone names help each game resolve a local frame; they are not enough to prove two clients are in
  the same authoritative world.
- `multiplayer.protocol` carries the application generation and player/relay role over the
  authenticated channel. The host and all clients must upgrade together. Incompatible endpoints
  can still complete the transport handshake; the current plugin withholds their ghosts and shows
  a pending/incompatible status. A future incompatible wire change needs a new generation and a
  synchronized relay/plugin upgrade; this project does not maintain legacy interoperability.
- Hosting, joining, and leaving are user-initiated. A plugin load, settings read, timeout, or host
  loss never triggers an automatic join. The host ends the private session on admission loss or
  owner unload. A relay stop clears its roster and disconnects players. Rejoining is a fresh,
  explicit request. There is no host migration, session restoration, or world snapshot today.

## Text chat

Chat is a co-presence feature like the ghosts, not a game feature: `multiplayer.chat` carries short plain-text lines
between the players of a session, in listen-host mode and through a relay, with nearby-only on or off. The peer that hosts
the session relays it through the network library, so the relay program is unchanged and protocol generation 2 stays; a
plugin build without chat never registers the channel and receives nothing. Like all `sco.net` traffic it is
authenticated but **not encrypted** (use a VPN). It is read only from verified players, bounded in length and rate,
never logged or run as a command, and lasts for one session. The layout and rules are in the
[plugin README](../plugins/multiplayer/README.md#text-chat). Not run in game; how the panel looks in game is left to testers.

## Discovery, persistence, and recovery

Players currently enter a numeric IPv4 address and port, or select one of up to eight locally saved
profiles. sc-mp has no LAN browser, public directory, account service, or automatic retry. The relay
does not advertise to an internet service. Local persistence covers configuration only; it does not
preserve world state, peer identity, or entity ids.

A minimal later discovery path should be LAN-only, opt-in, and non-authoritative: advertise a
protocol generation, a mode label, a bounded player count, and a reachable endpoint; never advertise
the passphrase or a participant list. The user must still choose a result and explicitly Join. A
restart creates a new session epoch, so old peers and ghost ids expire; restoring world state is a
separate host-owned save/load feature. Automatic reconnect and host migration remain out of scope
until consent, authority transfer, and data ownership are designed together.

## Small backend shape

1. **Local-only:** let the game process own simulation. Keep the network service idle and use
   `sco.storage` only for the local plugin preferences that the user chooses to save.
2. **Listen host:** let the product's private-session provider enforce consent, offline/EAC state,
   and exact transport policy. Keep peer messaging on `sco.net`. Until an authoritative simulation
   contract exists, synchronize co-presence only and label all remote actors as local ghosts.
3. **Dedicated relay:** run `sco-relay` as a separate, LAN/VPN-scoped transport host. Keep its role
   limited to authentication, roster/lifecycle, forwarding, and optional nearby filtering. Store
   its operator configuration locally; do not imply it owns a game world.
4. **Dedicated authoritative server:** treat this as a later, separate product. It needs an explicit
   game-side simulation provider, world identity, validated host-owned state updates, and a recovery
   policy. Reusing the relay's host peer id does not grant simulation authority.

## Shared SDK work needed for authoritative modes

The current contracts are sufficient for explicit private sessions, co-presence, local profiles,
and a relay-only host. Broader world synchronization needs new, versioned service contracts in
sco-core before sc-mp or the other products implement it. The following is a requirements sketch for
the owners to review, not a committed API decision:

1. **Session/world context.** Add a new size-prefixed, read-only C service table (for example, a
   versioned `get_current(context_v1 *out)` method) that returns an opaque 128-bit session id, a
   monotonically changing session epoch, an opaque provider-supplied world/instance id, the current
   mode/role, the authority peer id, and capability flags. Return `SCO_UNAVAILABLE` when the product
   cannot establish a field; do not derive identity from zone names, addresses, or peer ids. The
   values are copied, stable for that session epoch, and invalidated on leave, admission loss,
   provider stop, or host replacement.
2. **Authoritative state.** Add a separate versioned game service before remote gameplay mutations:
   the authority receives bounded, typed player intents, validates them on the owning game thread,
   and publishes accepted state with opaque session-scoped entity ids and ownership. No plugin may
   write arbitrary remote entity transforms as authoritative state. Provider absence must fail
   closed; unload and host loss must release owned work and invalidate outstanding ids.
3. **Discovery/recovery.** Extend the product's private-session surface only after a concrete local
   discovery protocol is agreed. Results must be copied, size-bounded, LAN-scoped, and never contain
   credentials. Joining remains an explicit UI request. Host migration or saved-world recovery must
   define a new session epoch and authority handoff before old state can be accepted.

Each extension needs C11/C++ ABI layout and size-tail tests, fail-closed provider fixtures, synthetic
multi-peer loopback coverage for join/leave/host-loss/restart, and explicit assertions that stale
session/world/entity ids are rejected. No transport handshake, state migration, or gameplay claim
is considered verified in game by those synthetic tests.
