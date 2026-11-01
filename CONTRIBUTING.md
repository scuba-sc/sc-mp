# Contributing to sc-mp

sc-mp is the optional co-presence `multiplayer` plugin for [sc-offline](https://github.com/scuba-sc/sc-offline),
and the home of its player-run relay server. Read [README.md](README.md) first.
Released sc-mp builds consume the published sco-core SDK. For coordinated
development and review, a build may instead use a current, reviewed, unreleased
sco-core candidate selected in coordination with the core owner, with the exact
source revision and artifact hashes recorded. A held candidate remains held; a
development check does not approve its merge or release. Transition to a published stable SDK
is a separate deliberate step.

sc-mp's own code is GPL-3.0-only; the contribution checks below follow the project's
[sco-core contribution guidance](https://github.com/scuba-sc/sco-core/blob/main/CONTRIBUTING.md)
and [IP policy](https://github.com/scuba-sc/sco-core/blob/main/IP_POLICY.md).

## Terms of contribution

By opening a pull request you agree your contribution is licensed under
GPL-3.0-only, and confirm you wrote it or have the right to contribute it on
those terms. Answer the source, rights and game-material questions in the
[sco-core IP policy](https://github.com/scuba-sc/sco-core/blob/main/IP_POLICY.md#contributions)
in the pull request description. Identify outside sources, their licences and
any employer or third-party ownership claims. Material with unknown or disputed
origin or rights is not distributed until resolved, rewritten or removed.

Preserve outside contributors' credit and applicable source licence notices;
these checks do not retroactively establish rights or change the terms of earlier
submissions. Existing unresolved contribution records remain in
[PROVENANCE.md](PROVENANCE.md) for maintainer review.

## Outside contributors are recorded

Every contribution from someone other than the maintainer is recorded in
[PROVENANCE.md](PROVENANCE.md), "Outside contributions", in the same change that adds it: who (GitHub
handle), what (paths), where it came from (the pull request), and the terms it came in under. Commits keep the
contributor as author or `Co-authored-by`. A change that adds outside code without that row is not merged.

## Where a relay lives

The plugin is `plugins/multiplayer/`. A relay (a headless program that carries a session and is never a
player) belongs in `server/` next to it, in its own CMake target, with its own README. The relay and the plugin
share one wire header, `plugins/multiplayer/relay_wire.h`: a new layout gets
a new wire version, and an old version is never reinterpreted.

A released relay is built from the published SDK's `core-source` profile
(`sco::net::Core`, `udp::Socket`, `ScopedTransport`), pinned the same way as the
plugin by [cmake/ScoSdk.cmake](cmake/ScoSdk.cmake). Coordinated development
builds may use a reviewed candidate kit under the recorded-input rule above. They
do not read an arbitrary sibling checkout or copy SDK sources into this repository.

### Guardrails any relay must meet

1. **Offline only (program rule 11).** No connection to Star Citizen's servers or online services, no
   anti-cheat bypass, no account or entitlement tampering, no telemetry, no accounts of the project's own. The
   relay is run by players for players on a LAN or VPN. The project does not run relays or keep a public server
   list.
2. **The LAN rule, through `ScopedTransport`.** The relay's socket must sit behind a `ScopedTransport`
   (`sco/net/scope.h`) passed to `Core`, with the default `Scope` (the LAN rule) unless the operator has named
   wider ranges. Per sco-core's [net-wire.md, "Headless hosts built from `core-source`"](https://github.com/scuba-sc/sco-core/blob/main/docs/net-wire.md):
   `Core` takes any `Transport` and applies no address rule of its own, and `udp::Socket::Open(port, false, ...)`
   binds every IPv4 interface, so a host composed without `ScopedTransport` accepts datagrams from, and sends
   to, any address, including public ones. Nothing in the kernel enforces this: it is a rule for the program that
   composes `Core`.
3. **No unauthenticated remote commands between peers.** Traffic is authenticated but plaintext
   ([net-wire.md, "Known limits"](https://github.com/scuba-sc/sco-core/blob/main/docs/net-wire.md)); players
   use a VPN for confidentiality.
4. **Public SDK surface only.** Use documented SDK headers and interfaces from the published archive, or from an
   exactly pinned reviewed candidate kit for coordinated development as described above.
5. **A relay is never a player.** Its roster message (`multiplayer.relay`) says so; the plugin does not list it
   as a player.
6. **Evidence.** A green CI run is a build and a synthetic test. Write "not run in game" unless a recorded
   in-game run says otherwise.

## Build and checks

See [README.md, "Build"](README.md#build): `cmake -S . -B build`, build, then `ctest`. CI
([.github/workflows/build.yml](.github/workflows/build.yml)) must pass. Keep pull requests small, with one
concern each.
