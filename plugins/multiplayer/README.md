# External multiplayer (G13)

`multiplayer` is an ordinary enabled native plugin, never a built-in. Install its
independent component into `data/plugins/multiplayer`; `plugins = on` and a newer
G13 host/SDK are required. Removing/disabling the folder never activates a host
fallback. Host source retains the inactive old implementation for later cleanup.

## Preserved surface and secure defaults

- `multiplayer.status`, `multiplayer.leave`, `multiplayer.goto <player>`.
  Goto always reports **G2 mutation not approved**, even if another table offers
  movement. There is no private relocation fallback.
- Retained **Multiplayer** panel: Host, Join, Leave, player name, numeric IPv4
  address, host/join ports, local avatar archetype, show ships, remembering, text chat and
  authenticated peer list. Host-side `sco.widgets::ImGuiRenderer` draws it on the
  existing game-thread frame handoff; no ImGui context/types enter the module.
- Host/Join are deliberately UI-only operations. Loading, changing settings,
  ticks, peer packets and automatic retries never set consent or open a socket.
  Requests borrow copied host-bound credentials and validate complete callbacks.
  Acceptance is not proof that a peer has authenticated/connected.
- The host-owned G13 broker is ON by default, but **loopback-only with no startup
  socket**. Admission requires positively certified offline session, no EAC,
  blocked official network and the right process. Unknown/online/EAC refuses;
  admission loss and owner unload close the session. Broker authenticates joins,
  bounds peers/messages/rates and audit-logs its lifecycle without credentials.
- Passphrase minimum is 12 bytes. The panel masks input with the additive
  `SCO_WIDGET_PASSWORD` flag; remembering defaults false. Leave clears the
  plugin's current credential. Masking is display protection, not encrypted
  memory or storage. The widget service copies text and native plugins have
  host rights. Explicitly saving a profile with remembering enabled writes its
  passphrase in plaintext to the optional local DB. To remove it, select the
  profile, turn remembering off, and save the same profile again. Host-side
  transient copies and database backups are outside the plugin's control.

## Settings compatibility

Owner remains **`multiplayer`**, database **`data/storage/multiplayer.db`**. Global
settings retain `name`, `join_address`, `avatar_class`, `host_port`, `join_port`,
`show_ships` and `remember_passphrase`. The legacy global `passphrase` is not
loaded and is deleted on the next successful settings save. Up to eight saved
server profiles are keyed by address text and join port. Save is explicit and
transactional; an existing address/port replaces that profile. Select a profile
in the panel to restore its address, port and optional passphrase; selecting it
never connects. Profiles are not automatically removed on Leave. Storage 1.1
is optional; without it, saves fail and session settings remain in memory.
Widgets 1.2 is optional; without it the panel is absent and the plugin still
loads. The host must actually render retained panels for these controls to be
reachable. No host files or secrets are imported.

## Explicit LAN host policy

The module cannot choose a bind address, widen scope, run a socket, select a
transport or bypass admission. A host-source product sets
`Platform.privateSession.bindAddress` and `.allowedAddresses` explicitly.
The sc-offline shell also accepts host-selected environment policy:

```
SC_OFFLINE_MULTIPLAYER_BIND=192.168.50.10
SC_OFFLINE_MULTIPLAYER_PEERS=192.168.50.11,192.168.50.12
```

These are synthetic examples, not observed user addresses. Both values are
required for LAN; the broker accepts only a numeric private local interface and
1–16 exact private peer addresses. DNS, public/wildcard/CIDR/IPv6, duplicates,
missing/truncated inputs and implicit LAN expansion refuse. Use the appropriate
local interface and reciprocal peer policy on each instance. Default/unset is
127.0.0.1 only. `SC_OFFLINE_MULTIPLAYER=off` disables the host broker while the
plugin can still expose status. Legacy `multiplayer_allow` CIDR ranges do **not**
authorize G13 and are never imported into broker policy. Existing launcher
firewall allowances are not broker admission or a guarantee of reachability.
Actual interface/firewall behavior still requires owner Windows acceptance.

**Admission in the production shell** (from reading the source at the commits linked here; not run in game). The
shell does not report a fixed Unknown. It answers the host's admission check from live reads of the Windows Firewall
rules and the TCP table: sc-offline builds the check in
[`src/dllmain.cpp` lines 250 to 259](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L250-L259)
and passes it to the host kit at
[line 430](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L430),
and sco-core gives G13 that same check
([`src/game/host.cpp` line 154](https://github.com/scuba-sc/sco-core/blob/844e136ccb280ad8cb43f73bc43c9447e8f6799d/src/game/host.cpp#L154)).
The check passes only when the session reads offline and anti-cheat, the target process and the official network all
read Clear ([`host.cpp` lines 46 to 52](https://github.com/scuba-sc/sco-core/blob/844e136ccb280ad8cb43f73bc43c9447e8f6799d/src/game/host.cpp#L46-L52));
otherwise the host refuses ([line 121](https://github.com/scuba-sc/sco-core/blob/844e136ccb280ad8cb43f73bc43c9447e8f6799d/src/game/host.cpp#L121))
and the shell skips its offline start-up
([`dllmain.cpp` line 273](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L273)).
sc-offline's own status notes record `officialNetwork=Clear` in two runs of v0.7.8.1
([`docs/status.md` lines 13 to 15](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/docs/status.md#L13-L15)) and say
the shell has reported network evidence from live firewall and connection reads since then
([line 40](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/docs/status.md#L40)).
So an earlier version of this README, which said G13 always refuses in that shell, no longer matches the source.
What a given PC's reads find is not known from here.

One setting makes the official network read Unknown by construction. It reads Clear only when the firewall's blocked set
covers every IPv4 address except 10/8, 127/8, 169.254/16, 172.16/12 and 192.168/16
([`src/admission_evidence.h` lines 329 to 334](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/admission_evidence.h#L329-L334)
and [414 to 416](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/admission_evidence.h#L414-L416)),
and the launcher leaves the `multiplayer_allow` ranges open in the `StarCitizen.exe` block rule
([`launcher/launcher.cpp` lines 2017 to 2030](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/launcher/launcher.cpp#L2017-L2030)).
A range outside those five that is allowed there, such as a VPN's `100.64.0.0/10`, is not covered, so the check reads
Unknown at start-up. The host then refuses (`host.cpp` line 121, which leaves it idle), the shell tries three times and
gives up
([`dllmain.cpp` lines 436 to 450](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L436-L450)),
and it returns from its offline start-up before applying any patch (line 273 above). The host kit is started only from
the prepared state
([`dllmain.cpp` lines 534 to 535](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L534-L535)),
and that start is what hands G13 its admission check (`host.cpp` line 154 above), so as far as this reading shows no
Host or Join is offered at all in that case. No bypass or invented evidence was added to this plugin.

## Co-presence, not shared simulation

Keep the `multiplayer.pose`, `.spawn`, `.despawn` channels (and add `multiplayer.relay`, below). Poses are bounded at
10 Hz, name-based frames only (never remote zone IDs), strict finite/quaternion
validation and wrap-aware sequences. Match copied, observed, unambiguous local
zone names; unresolved/distant (over 10 km)/stale (10 seconds) ghosts are removed,
not placed in guessed frames. A buffer of samples with a short, capped
extrapolation moves the ghosts (see Ghost motion). No shared physics, damage,
missions or inventory is promised.

Pose senders and receivers require pose wire version 2. All players and the relay must use the
same **multiplayer protocol generation**, currently 2. Upgrade the relay and every player together
before starting a session; do not mix an older plugin or relay with this generation. The protocol
generation is a compatibility contract, not the plugin's binary or release version: builds that
implement the same generation can interoperate. There is no automatic downgrade.

`multiplayer.protocol` is a reliable two-byte announcement: generation, then role (1 player,
2 relay). Each player repeats its announcement and the relay sends its own with each roster. The
module accepts evidence only from authenticated session members; only host peer 1 can claim the
relay role. It does not display a player or consume its spawn or pose until that player and the
session host have announced the matching generation. It accepts the relay roster and nearby-only
poses only after the host announces the relay role. The panel and `multiplayer.status` show peers
waiting for evidence or incompatible; after 10 seconds they say verification timed out or is
incompatible. A later valid announcement can complete admission. Leaving clears this evidence.
A verified player that sends a structurally valid v1 pose contradicts its announcement and is
excluded until it leaves and rejoins or the session ends. Unknown or malformed pose layouts are
dropped without interpreting their contents. When sco.net provides channel registration methods
but refuses one of its channels (the chat channel included), the plugin rolls back loading. If registration, sending, or membership methods are unavailable, the plugin reports the protocol
unavailable and refuses Host and Join. When registration methods are absent it still loads without
registered network channels and must be reloaded with the channel service available.

This is **local application-level gating**, not a sco.net handshake refusal. The authenticated
transport may still connect an older endpoint, and an unmodified older plugin can still show its
own connection state even though it cannot interpret v2 poses. The relay can neither inspect the
protocol during the existing handshake nor kick an individual peer. Full transport-level refusal
would require the network API to carry application compatibility in admission and support rejection
or eviction; every endpoint would need the corresponding upgrade. Check each player's panel for
compatibility rather than treating a transport connection or roster count as proof of readiness.

Avatar ghosts use the locally selected NPC archetype; peer avatar classes are
informational rather than arbitrary remote actor-spawn authority. Ship ghosts
are bounded, class-syntax-checked and validated by the owning entity provider.
Only authenticated, protocol-compatible session members allocate peer records; at most 63 peers and
32 active/pending-removal ghosts. Actors use `game.actors.spawn_npc/despawn`,
ships use `game.entities.spawn/despawn`, and transforms are owned public calls.
The member list is read with room for 16 entries; when `sco.net` answers `SCO_TOO_MANY`
with a larger count, the call is repeated with room for that many (at most 3 calls,
at most 64 members). Any other answer, or a count over 64, means no list and no record.
With 16 members or fewer nothing changes. Not run in game.
Streaming/retry/removal tickets are bounded. An ambiguous `alive=0` never erases
ownership; provider unload cleanup retains any refused removal. There are no
private NPC-remover, spawner, vehicle-registration or zone-pointer fallbacks.

The legacy v1 despawn helper wrote 16 bytes but declared/validated 12, making all
messages fail. The external v1 consumer declares and checks the actual 16-byte
frame; no active built-in peer format is silently claimed compatible.

## Joining a relay server (sco-relay)

A session can also be hosted by a dedicated relay server, **sco-relay**, instead of by a player. The
server is peer 1 of the session, relays the players' messages to each other and spawns nothing. It
is **not a player**: it gets no ghost, is not in the "Other players" list and is not counted in
"N other players". The player still presses **Join** (address, port and passphrase of the server);
nothing connects by itself, and consent, the 12-byte passphrase minimum, the host-side admission
check and the refusal when admission is Unknown or EAC is present are exactly as for any join.

- `multiplayer.relay` is a fourth channel: reliable, from the session host only, at most 1024 bytes.
  The server sends it right after a player joins and again every few seconds (its `announce_interval_ms`,
  5 s by default): who is in the session, in which slot, and the server's player limit. The module
  reads it only from peer 1, only in version 1 and only when the length and every field are right; a
  message that is not is dropped, and `mod.log` says so once per session.
- The host announces whether it is a player or relay on `multiplayer.protocol`. A relay is never
  added as a player; a player host must announce the current generation before it can be listed.
  The roster is accepted only after a matching relay announcement, and repeats on the server interval.
- **Nearby-only.** The roster says whether the server filters poses by distance (its `nearby_only` setting,
  off by default). When it is on, the module sends each pose to the server on `multiplayer.near_up`
  (unreliable, to the host only) instead of `multiplayer.pose`, and listens for the poses of nearby players
  on `multiplayer.near.<slot>` (unreliable, from the host only), where `<slot>` is its own seat in the latest
  roster, registered when the roster arrives and dropped when the roster says nearby-only is off, the seat
  changes or the session ends. A `near_up` message is a 40-byte header, then the pose bytes exactly as
  before: the area (FNV-1a-64 of the outermost zone's name) and the player's position in that zone, which is
  all the server reads. The server forwards the pose to the players in the same area within its
  `nearby_distance_m` (12 km by default, above the 10 km at which this module shows a player). A
  `near.<slot>` message names the sender's peer id and the generation of the slot (a counter that goes up
  each time someone new takes it); the module drops one whose generation is not the one its latest roster
  gave, one that did not come from the host, one whose sender is this player, the server or not in the
  session, and one that is not the contract (one `mod.log` line per session). A pose that arrives on
  `multiplayer.pose` is accepted only from a verified player using pose version 2. Spawns and
  despawns are not filtered by nearby-only: they still reach everyone through the network library.
- The message layouts are in `relay_wire.h`, the one wire header this module shares with the relay server in
  `server/` (standard headers only, with golden bytes in `server/tests/test_relay_wire.cpp`; the workflow test
  repeats the roster's). There is no second copy. A layout change gets a new version number, and the server and
  this module change together, in a change of its own.
- Poses, spawns and despawns use the same channels and 10 Hz pose rate, still relayed between
  players by the network library unless nearby-only is on (below). The module works the same on a session
  hosted by a player.

`mod.log` lines of a relay session (each starts with `[multiplayer]`; there is no passphrase, address or
player name in any of them):

| Line | When |
| --- | --- |
| `join request accepted: waiting for the session (0)` / `join request refused (N)` | after **Join**, N being the result code; accepted is not a connection |
| `relay server found: peer 1 hosts the session and relays players; it is not shown or counted as a player` | the first valid roster of a session |
| `joined relay server as peer P in slot S (generation G): N of M transport members, nearby-only off` | the same roster, when it lists this player: **this is the line that shows a join to a relay server** |
| `relay roster: N of M transport members, slot S (generation G), nearby-only off` | a later roster whose players, slot or mode differ (an unchanged roster is silent) |
| `relay roster: N of M transport members, this player is not listed yet` | a roster that does not list this player |
| `nearby-only: poses go to the relay server on multiplayer.near_up and come back on multiplayer.near.S` | the roster says nearby-only is on and lists this player: the module now listens on its slot |
| `nearby-only is off: poses go to the other players on multiplayer.pose` | a later roster says nearby-only is off |
| `cannot listen on multiplayer.near.S (code): this player sends its poses on multiplayer.pose and will not see the players the server forwards on that channel` | sco.net refused the channel, once per session; the module tries again at every roster. Only this player's own pose falls back: the poses the server forwards to this seat have no listener |
| `ignored a malformed multiplayer.relay message from the session host; ...` (or `multiplayer.near`) | once per session, however many follow |
| `left the relay session` | the session ends or this player leaves |

`multiplayer.status` and the panel add `via relay server (not a player), slot S, nearby-only off` once a
roster has been read.

**Not run in game.** The workflow test, the relay-join test (synthetic game providers on a real
`sco::net::Core` joining a real sco-relay over loopback UDP) and the Windows build are the only evidence;
no real game, EAC, admission collector or LAN was involved. What the production shell's admission check reads on a
real PC is not known from here; "Admission in the production shell" above is from reading the source only.

## Text chat

Players in a session can send each other short lines of text from the panel: the **Chat** list, the message box and
**Send** (panel nodes 20 to 23; nodes 1 to 19 keep their meaning). It works the same in a session a player hosts and in
one a relay server hosts, with nearby-only on or off, because it rides one more channel, `multiplayer.chat`, which the
network library relays between players the way it relays spawns: the relay server needs no change. It changes no
existing layout, so the protocol generation stays **2**. **Not run in game.** The workflow test and the loopback relay
test (below) are the only evidence; how the panel looks and feels in game is left to testers.

- **Wire.** `multiplayer.chat` is reliable, sent to every other player, at most 164 bytes. Version 1 is `u8` version (1),
  `u8` 0, `u16` little-endian `n` (1 to 160), then `n` bytes of text: exactly 4 + `n` bytes. The text is well-formed
  UTF-8 with no control character (C0, DEL, C1) and no bidirectional embedding, override or isolate control. The line
  "hi" is `01 00 02 00 68 69`. The layout is in `multiplayer_wire.h`, with golden bytes in the workflow test; a later
  layout gets a new version number and version 1 is never reinterpreted.
- **Who is read.** Only a player this module has verified: a member of the session now whose `multiplayer.protocol`
  announcement of the current generation was seen (so not a peer that left, nor one that came back without announcing
  again), not this player, and never a relay server (peer 1 with the relay role). A line from anyone else, and a line that
  is not the layout (wrong length, a trailing byte, bad UTF-8), is dropped and counted.
- **Limits.** 160 bytes a line. A player sends at most 3 lines in 5 seconds and reads at most 6 from one sender in
  5 seconds; the extras are dropped and counted, and `multiplayer.status` adds "chat: N dropped over the rate limit, M not
  accepted" once either count is not zero. The history keeps the last 32 lines. The history and the draft in the
  message box last for one session: **Leave**, the end of the session and unload clear them. A sent line is shown only
  after sco.net took it; when it refuses, nothing is shown and the draft stays.
- **Plain text only.** A line is copied into the history and shown. It is never written to `mod.log` or anywhere else,
  never used as a format string, and never run as a command or handed to one (CONTRIBUTING, guardrail 3: no remote
  commands between peers). A line is labelled with the speaker's name and peer id ("Alice #3"), so it cannot pass for
  this player's own `You: ` lines, and a name that is not clean text or holds a colon (the separator, which could start
  a second speaker) is not used at all ("peer 3").
- **Not encrypted.** Like every sco.net message, chat is authenticated but readable by anyone on the network path. Use a
  VPN, and send nothing private. The panel says so beside the message box.
- **Older plugins.** A build without chat never registers the channel and so receives nothing; nothing else changes for it,
  and no synchronized upgrade is needed for chat. sco.net drops a line without a word when no other player has the
  channel, so **Send** cannot tell who actually sees it.
- **Early lines.** A line sent in the first seconds after a join can be dropped silently by a receiver that has not yet
  verified the sender, because chat and the `multiplayer.protocol` announcement travel on separate reliable channels and
  nothing orders one against the other. Each player repeats its announcement every few seconds, so later lines are read.

Tests: the workflow test (`tools/multiplayer_workflow_test.cpp`) checks the layout (golden bytes, length edges, malformed
UTF-8, trailing bytes), the rates with a clock it steps by hand, and the panel in a listen-host session and a relay session:
unannounced, relay, departed and over-rate senders, a refused send, text that looks like a command, a format string or
markup, speaker names that try to pass for this player ("You") or to start a second speaker, a joiner reading a listen
host, and that no chat text reaches `mod.log` or the status line. `TestRealUdpChatThroughRelay` in
`server/tests/test_udp.cpp` runs the relay's own session over loopback UDP, in its default mode and nearby-only: lines
arrive with their writers as the origin, a receiver drops one over its limit, a player without the channel receives none
and still gets poses, and the relay's log holds no chat text.

## Ghost motion

Poses arrive about ten times a second, each later than it was sent by a different amount, and some never
arrive. The module used to draw a ghost 0.1 s behind between its last two poses; one late or lost pose
froze it at the newest, and the next one made it jump. `motion.h` (header only, no host calls) now keeps
the last 12 poses of each player:

- **Version 2 timing.** Sender monotonic timestamps are mapped to the first
  arrival's clock; no shared epoch is needed. The innermost player frame uses
  the transmitted velocity for extrapolation, capped at 3 km/s and 150 ms.
  Other frames derive velocity from positions and sender times. A non-increasing
  sender time or backward arrival is dropped. The track also retains arrival-based rules
  used in the historical v1 measurements below; the current plugin accepts only v2 poses.
- **Dejitter.** A pose has a time on the sender's timeline, not its arrival time: the point, at the pose's
  sequence number, of a line fitted through the arrival times of the last 16 poses. A lost pose leaves a
  gap in the sequence, which the line keeps. The line is held within 150 ms of the arrival times.
- **Playout delay.** The ghost is drawn 200 ms behind the present, so the pose after the drawn time has
  usually arrived, even after one lost pose, and the ghost moves in a straight line between the two.
- **Capped extrapolation.** If the drawn time is past the newest pose (several lost in a row) the ghost keeps its
  last velocity for at most 150 ms, then holds. A rotation is never extrapolated. The velocity is the distance
  between the last two poses over the time the **sender** took for them. The wire carries no sender time, so the
  module judges that time from the arrival times it has seen, and takes the longest of: 100 ms per sequence step
  (the sender sends one pose per tick, a tick at least 100 ms after the last; steps are counted without a cap, since
  a longer gap only lowers the speed); the timeline's own gap between the two poses; the slope of the timeline fit,
  once the fit holds 10 poses; and the newest arrival gaps when they are clearly slower than a 10 Hz sender can look
  under 35 ms of jitter, that is more than 75 ms longer than 100 ms a step (the newest pair, or the newest two or three
  intervals together; the longer spans are left out when the newest pair arrived together with its predecessor and a
  cadence is known, which is a stall let go, not a slow sender). The last is how a sender that drops under 10 Hz (a game
  under 10 frames a second) is noticed within a pose or two, before the fit has followed. The difference of two arrival
  times is never the speed by itself: poses delivered in one frame (a burst after a stall, the first two after a reset)
  arrive 1 ms apart, and a speed worked out from that would be 100 times too high. When the newest poses arrived together
  and the track is too young to know the cadence, nothing says whether the sender is a 10 Hz or a 5 Hz one, and the
  ghost holds at the newest pose instead of guessing. A second guard caps the distance: the ghost never goes on past
  the newest pose by more than 1.5 steps of movement.
- **Resets.** The buffer starts over and the ghost goes straight to the new pose, without gliding, when no
  pose arrived for 600 ms, when the pose's frames differ from the last one's (another chain of zones, a
  ship boarded or left), or when two poses are further apart than 3 km/s allows in the time the sender took (a teleport; that time is at least 100 ms per sequence step, or the track's cadence when it knows one, counted up to 50 steps: poses delivered together are not a teleport, and neither is a fast player across 10 steps at 2.5 km/s, or at 500 m a step of a 200 ms sender). A pose that is not newer than the last (wrap-aware) is dropped, as before.
- The ghost's delay is the module's own business; the relay contract carries no rendering parameter.

**How far the lead can be wrong.** The lead is a judgment, not a measurement, and the bounds below are what the motion
test measured on its fixed inputs, not promises. The test pins the worst lead of each case, not how many runs go over the
cap; for a constant slow sender and for the drop to 150 ms the pins equal what the version before already measured, so
they stop it getting worse, they do not prove the improvement.
"The cap" is 150 ms at the real speed (30 m at 200 m/s). "The network" is the test's own: 60 ms, +-35 ms, 12 in 100 poses
lost, 1000 runs of each case. "Before" is the `motion.h` before the slow-sender work (commit f2ba0a0).

- With exact arrival times (no jitter, no loss) the lead stays within 3% of the cap for a sender at 200 or 250 ms a pose
  (two poses delivered together, a burst after a stall, a new track), for a sender that drops from 10 Hz to 150, 200 or 250 ms
  a pose in the middle of a track (also with one pose lost), and for the four arrival patterns a review built against the
  version before (poses bunched at the start, a jitter pattern, a lost pose, a rate change). Before, these led by up to 2.5 times.
- On the network, a constant 200 ms sender is led over the cap in 243 runs of 1000, by up to 1.54 times (before: 883 runs,
  up to 1.54 times); a constant 250 ms sender in 279 runs, up to 1.39 times (before: 833, up to 1.39). The worst case did not
  improve: it sits in the first poses of a track, where two arrival times are all there is. Later in a track a
  constant slow sender goes over the cap by 1.11 times at most. The worst band is a sender at 160 to 190 ms a pose, led by
  up to 1.70 times (at 170 ms), the same as before; a game at 12 frames a second reaches 1.65 times.
- On the network, a sender that drops from 10 Hz to 150, 200 or 250 ms a pose is led over the cap in 211, 427 and 590 runs,
  by up to 1.47, 1.72 and 1.35 times (before: 660, 994 and 986 runs, up to 1.50, 2.00 and 2.50 times). A drop is noticed
  from the first slow pose when its arrival gap is clearly long (over 175 ms), and otherwise within a few poses, as the longer
  spans and the fit catch up. The large overshoot falls within 3 poses of the drop; after that the lead can stay a little
  over the cap for several more poses (at 200 ms: up to 1.13 times 4 to 10 poses after the drop, 1.09 times later).
- New 10 Hz tracks (every join, silence, change of frames and teleport starts one) stutter exactly as much as before:
  over 2000 new tracks on the network, 41 freezes and 61 jumps in the first 1.5 s, the same as before. A version that held the
  ghost at the newest pose until it knew the cadence led slow senders much closer to the cap (a constant 200 ms sender over
  it in 18 runs of 1000, by 1.05 times at most) but stuttered 191 freezes and 234 jumps on the same tracks, so it was not kept.
- Not covered: a track whose first poses arrive bunched together can mislead the cadence until it has 10 poses; a rate change
  that arrives together with jitter can still be led too far for several poses (the figures above are that case); poses of a
  sender slower than 2.5 Hz (over 400 ms a pose) are outside the range the fit reads. Two costs of the slow-sender rule,
  found by review and not fixed: a normal 10 Hz sender whose poses arrive with more jitter than the test's +-35 ms can be read
  as slow, so the ghost runs behind and catches up in jumps (one review measured 61 jumps against 39 before at +-50 ms, and
  42 against 19 at +-70 ms, over the same mature tracks; none at +-35 ms); and right after a stall the fit can read a 10 Hz
  sender as a little slower (114 ms), which raises the teleport limit, so a real jump of up to about 340 m in one step is taken
  as movement and the ghost slides and leads instead of starting over. None of this ran in game.

`tools/multiplayer_motion_test.cpp` feeds the same jittered, lossy 10 Hz path (60 ms +-35 ms, 12 in 100
lost, a burst of four lost, one pose reordered; 20 s) to the unmodified sc-offline #126 module and to this one, in real time at 60
frames a second, and counts per ghost the freezes (4 frames or more in a row moving less than a quarter of
the player) and the jumps (a frame moving more than three times the player). It prints both modules' counts, and fails unless
this module has at most half the freezes, no more jumps and a smaller largest step. It also checks the extrapolation
on its own, at walking, ship and fast-ship speeds: for two poses delivered in one frame (in steady flight, after a reset,
after a change of frames) and bursts after stalls of 150 to 250 ms, with the flow cut or resumed, no frame may lead the newest
pose by more than 150 ms at the real speed, and a burst may not look like a teleport. The same is checked for a sender
slower than 10 Hz (a pose every 200 or 250 ms, a burst of four on a steady track), for a sender that drops under 10 Hz mid-track,
for the four review patterns, for a gap of 16 sequence steps (into a new track, and on a track that knows its cadence, where the
steps must all count), and for the teleport limit at a known cadence; the network cases and the 2000 new 10 Hz tracks are run
as described above. `multiplayer-motion-test --track-tests` runs only these buffer tests, without a module.

## Tester cases (all **not run in game**)

Every case that needs a connection needs a shell whose admission check reads Clear at start-up (see "Admission in the
production shell" above: a check that reads Unknown then ends the offline start-up, and no Host or Join is offered).
Report what `mod.log` says.

1. Start the game with the module installed and do nothing: no `[multiplayer]` line, no socket, **Join**
   has not been pressed.
2. Admission evidence that stops reading Clear after start-up. EAC starts: the shell reads a running EAC process
   ([`dllmain.cpp` lines 134 to 136](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L134-L136))
   or a service that is not stopped
   ([line 153](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L153))
   as anti-cheat Present
   ([line 188](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L188)),
   not Unknown. The firewall service stops answering: the official network reads Unknown
   ([`admission_evidence.h` lines 393 to 399](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/admission_evidence.h#L393-L399)).
   The host's check passes only when anti-cheat and the official network both read Clear
   ([`host.cpp` lines 49 to 50](https://github.com/scuba-sc/sco-core/blob/844e136ccb280ad8cb43f73bc43c9447e8f6799d/src/game/host.cpp#L49-L50)),
   so either way Host and Join are expected to be refused. G13 is handed that same check
   ([`host.cpp` line 154](https://github.com/scuba-sc/sco-core/blob/844e136ccb280ad8cb43f73bc43c9447e8f6799d/src/game/host.cpp#L154))
   and the shell re-inspects it on every tick
   ([`dllmain.cpp` lines 538 to 540](https://github.com/scuba-sc/sc-offline/blob/96686b95adc2e07e4cf3d0db15c4ec3a79cb1a8e/src/dllmain.cpp#L538-L540)).
   Press **Join** with a sco-relay server's address and passphrase and report what the panel and `mod.log` say: a refused
   request (`Join request refused (N)` in the panel, `join request refused (N)` in `mod.log`) or a session closed by the loss
   of admission is what the source suggests. Nothing is sent either way.
3. In a shell that grants admission: **Join**. Expect `join request accepted: waiting for the session (0)`,
   then within the server's announce interval `relay server found: ...` and `joined relay server as peer P in slot S (generation G): ...`.
4. Two players on the server: each sees one ghost, the panel lists one other player, and the server is not in the list.
5. Press **Leave**, or stop the server: `left the relay session`, the ghosts go away.
6. A session a player hosts: no relay line in `mod.log`; everything as before.
7. A third player in another place with nearby-only on: no ghost for them, `nearby-only: poses go to the relay server ...` in `mod.log`.
8. The other player walks, stops and turns: the ghost follows about a quarter of a second behind with no freeze
   at a dropped message and no jump; a quantum jump or a respawn moves the ghost at once.
9. Chat, two players: type a line in the message box and press **Send**. The other player's **Chat** list shows it with
   your name and peer id (`Name #3: ...`); yours shows it as `You: ...`. Report how the list, the box and the button look and feel in game. Nothing
   of what was said is in `mod.log`.
10. Chat, a third player on a plugin build without chat in the same session: they see no line and nothing else changes.

## Build/install and evidence boundary

The pinned SDK is **2.4.0 release candidate 2** (release
[`sdk-v2.4.0-rc.2`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2); the final
`sdk-v2.4.0` will follow). It **contains G13, G3 and retained widgets**
(`sc_private_session.h`, `sc_game_spatial.h`, `sco_widgets.h`), as every SDK
since 2.2.0 has; sdk-v2.0.0 did not. The CI release job does not yet stage
multiplayer. The external target refuses a missing header and never adds a host
include root. An isolated local compile uses the checksum-verified published
`sco-sdk-2.4.0.zip` from that release (SHA-256 in `cmake/ScoSdk.cmake`):

```
cmake -S plugins/multiplayer -B build-multiplayer -G Ninja \
  -DSCO_SDK_INCLUDE_DIR=/path/to/extracted/sco-sdk-2.4.0/include \
  -DSCO_WORKFLOW_TESTS=ON
cmake --build build-multiplayer --target multiplayer-module-test
ctest --test-dir build-multiplayer -R '^multiplayer-module-workflow$' -V
cmake --install build-multiplayer --component multiplayer --prefix stage
```

`-DSCO_WORKFLOW_TESTS=ON` also builds `multiplayer-motion-test` (ctest name `multiplayer-motion`, about 25 s in
real time). Add `-DSCO_MOTION_BASELINE_DIR=<folder with the unmodified multiplayer.cpp and multiplayer_wire.h of
sc-offline #126, commit 481b99e>` and it runs that module beside this one on the same path and compares them.

The enabled `plugin.ini` and module install into `stage/data/plugins/multiplayer`
without a `disabled` marker. Windows emits `multiplayer.dll`; macOS produces a
native development `.so` for the targeted dynamic-loading fixture, not a player
Windows package. Shared plugin builds include this target by default when the
extracted SDK contains `sc_private_session.h`, and explicitly report unavailable
otherwise; no published CI job pretends to ship it without the contract.

The fixture loads the actual independent module with synthetic public providers,
checks no auto-host/join, Host/Join consent and credential bounds/masking/defaults,
exact storage keys, refusal, owned ghost messages/transform/despawn, leave and
reload. Core separately exercises the real authenticated backend/broker through
a synthetic transport. Neither proves real Windows/GPU/render/input, admission
collection, LAN reachability, native unload/fault cleanup or gameplay. Those are
**NOT VERIFIED**; no full suite or actual game/network session was run here.
