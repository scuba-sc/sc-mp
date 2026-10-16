# Contributing to sc-mp

sc-mp is the optional co-presence `multiplayer` plugin for [sc-offline](https://github.com/scubamount/sc-offline),
and the place where a player-run relay server for it would live. Read [README.md](README.md) first.
sc-mp consumes the published sco-core SDK only, and sco-core's and sc-offline's contribution rules apply
here too: [sco-core CONTRIBUTING](https://github.com/scubamount/sco-core/blob/main/CONTRIBUTING.md),
[sc-offline CONTRIBUTING](https://github.com/scubamount/sc-offline/blob/main/CONTRIBUTING.md).

## Terms of contribution

This repository has no licence-by-contribution text of its own yet. The terms below are quoted from the
sibling repositories, not restated. The maintainer decides how they apply here.

From sco-core's CONTRIBUTING, section "License":

> By opening a pull request you agree your contribution is licensed under GPL-3.0-only, and you confirm you
> wrote it or have the right to contribute it on those terms; answer the questions in the pull request template
> ([IP policy](https://github.com/scubamount/sco-core/blob/main/IP_POLICY.md)).

From sc-offline's CONTRIBUTING, section "License":

> By opening a pull request you agree your contribution is licensed under GPL-3.0-only.

sc-mp's own licence is GPL-3.0-only ([LICENSE](LICENSE)). The three questions that sco-core's IP policy asks of
every pull request (source, rights, game material) are in
[sco-core IP_POLICY.md, "Contributions"](https://github.com/scubamount/sco-core/blob/main/IP_POLICY.md).
Put the answers in the pull request description.

## Outside contributors are recorded

Every contribution from someone other than the maintainer is recorded in
[PROVENANCE.md](PROVENANCE.md), "Outside contributions", in the same change that adds it: who (GitHub
handle), what (paths), where it came from (the pull request), and the terms it came in under. Commits keep the
contributor as author or `Co-authored-by`. A change that adds outside code without that row is not merged.

## Where a relay lives

The plugin is `plugins/multiplayer/`. A relay (a headless program that carries a session and is never a
player) belongs in `server/` next to it, in its own CMake target, with its own README. The relay and the plugin
share one wire header, `plugins/multiplayer/relay_wire.h` (on the `relay-import` branch today): a new layout gets
a new wire version, and an old version is never reinterpreted.

A relay is built from the published SDK's `core-source` profile (`sco::net::Core`, `udp::Socket`,
`ScopedTransport`), pinned the same way as the plugin by [cmake/ScoSdk.cmake](cmake/ScoSdk.cmake). It never reads
a sibling sco-core checkout and never copies SDK sources into this repository.

### Guardrails any relay must meet

1. **Offline only (program rule 11).** No connection to Star Citizen's servers or online services, no
   anti-cheat bypass, no account or entitlement tampering, no telemetry, no accounts of the project's own. The
   relay is run by players for players on a LAN or VPN. The project does not run relays or keep a public server
   list.
2. **The LAN rule, through `ScopedTransport`.** The relay's socket must sit behind a `ScopedTransport`
   (`sco/net/scope.h`) passed to `Core`, with the default `Scope` (the LAN rule) unless the operator has named
   wider ranges. Per sco-core's [net-wire.md, "Headless hosts built from `core-source`"](https://github.com/scubamount/sco-core/blob/main/docs/net-wire.md):
   `Core` takes any `Transport` and applies no address rule of its own, and `udp::Socket::Open(port, false, ...)`
   binds every IPv4 interface, so a host composed without `ScopedTransport` accepts datagrams from, and sends
   to, any address, including public ones. Nothing in the kernel enforces this: it is a rule for the program that
   composes `Core`.
3. **No unauthenticated remote commands between peers.** Traffic is authenticated but plaintext
   ([net-wire.md, "Known limits"](https://github.com/scubamount/sco-core/blob/main/docs/net-wire.md)); players
   use a VPN for confidentiality.
4. **Public SDK surface only.** Nothing from outside the published SDK archive and its documented headers.
5. **A relay is never a player.** Its roster message (`multiplayer.relay`) says so; the plugin does not list it
   as a player.
6. **Evidence.** A green CI run is a build and a synthetic test. Write "not run in game" unless a recorded
   in-game run says otherwise.

## Build and checks

See [README.md, "Build"](README.md#build): `cmake -S . -B build`, build, then `ctest`. CI
([.github/workflows/build.yml](.github/workflows/build.yml)) must pass. Keep pull requests small, with one
concern each.
