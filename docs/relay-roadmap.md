# Relay roadmap

Imported from sc-offline PR #133 (commit 97b0b7a). The PR description, as written there:

## What this changes

Three changes to the external multiplayer plugin of #126, so it works with sco-relay, a small standalone relay server that players join (context: #134):

- **The server is never a player.** The plugin reads the server's reliable, host-only `multiplayer.relay` roster and keeps the server out of the peer list, the ghosts and the count. `relay_wire.h` is a byte-for-byte copy of the server's message contract.
- **Nearby-only forwarding.** When the roster says the server filters by distance, the plugin sends on `multiplayer.near_up` and listens on `multiplayer.near.<slot>`. Otherwise it behaves as before.
- **Smoother ghosts.** Ghosts move from a buffer of each player's last poses on a dejittered timeline, drawn 200 ms behind, with a short capped extrapolation. The plugin README ("How far the lead can be wrong") gives the measured bounds and what is not covered: senders slower than 10 Hz can still be led past the cap.

A session hosted by a player works as before. Consent, the 10 Hz cadence, the admission refusal and no-auto-join are unchanged.

It builds on #126 and starts from `main` at `4db58d2` (0.7.8). I can split it into two PRs (relay support, smoothing) if you prefer one change per PR.

## How it was tested

- [x] `tools/check.sh`: no new diagnostic against `main` at `4db58d2` (both report the same single diagnostic against the baseline, which is already on `main`)
- [x] Built the plugin Release x64 with MSVC 2022 (`/W4 /WX`, no warnings) and on Linux with `-Wall -Wextra -Werror`; the rest relies on CI's `build`
- [ ] Played in game on Windows
- [ ] `tools/update-test/run.sh` (not touched)

Tests added and run on Linux and Windows: the workflow test with the golden bytes of the new messages (266 checks); a motion test that runs the unmodified #126 module and this one on the same jittered, lossy 10 Hz path, plus buffer cases (548 checks; freezes 18 -> 1, largest step 2.71 m -> 1.02 m in one run); and, outside this PR, a join test against a real sco-relay over loopback UDP with synthetic game providers (239 checks).

The local builds used an SDK packaged from sco-core `82462b0`; `main` now pins SDK 2.2.0, which CI builds against.

Not tested: nothing was run in game. sc-offline 0.7.8.1 and later pass the admission gate on their own evidence; this module has not shipped with sc-offline since 0.7.8; nothing here changes admission.

## Checklist

- [x] In scope: offline, no CIG servers or online play, connections only through LAN/VPN co-presence; no game files, secrets or personal data
- [x] PC changes the launcher makes are still listed and undone (the launcher is not touched)
- [x] Docs and `CHANGELOG.md` updated where players would notice

## Server peer and sessions of more than 16 players 

Stacked on `relay-import`. It is the plugin half of two sco-core changes, "Raise the sco.net session limit and the
private-session cap from 16 to 51" and "Let a sco.net host be a server, not a player (sco.net 1.1)". Details are in
[the plugin README](../plugins/multiplayer/README.md#servers-and-sessions-of-more-than-16-players-sconet-11).

Done here:

- The module asks sco.net for version 1.1 when its SDK defines it, and keeps a host that sco.net marks as a server out of
  its peer records, ghosts, list and count. The relay roster still does the same for a server that is not marked.
- `get_peers` is read past 16 entries (retry on `SCO_TOO_MANY`), the module's own peer limit follows the session size, and
  in a build against an SDK that has `SCO_NET_SESSION_MAX_PEERS` the 32-ghost budget is counted in ghosts held instead of
  two per listed player (a build against SDK 2.2.0 keeps the old count).
- A `static_assert` ties `relay_wire.h`'s `kPoseMaxBytes` to the plugin's own largest pose (yerts' optional note on the
  review of #133).
- Built and tested against the published SDK 2.2.0 (there the flag code is not compiled and the ghosts are counted as before;
  only the member list is read past 16 entries) and against an SDK packaged from the unmerged
  sco-core branch, on Linux and Windows. **Not run in game.**

Open, not done here:

- sc-mp has no standalone build or CI yet, so nothing proves this on every push; the tests are run by hand through a local
  harness.
- The relay-join test (a real sco-relay, a real `sco::net::Core`) hands the module a table without `get_peer_flags`, so
  the flag is exercised only by the workflow test's synthetic table, not through a real network library.
- Drawing more than 32 players needs a decision on the ghost budget (it is a plugin-side cap, not a sco.net limit).
- sc-offline 0.7.8.1 and later pass the admission gate on their own evidence; this module has not shipped with sc-offline since 0.7.8; nothing here changes admission.
