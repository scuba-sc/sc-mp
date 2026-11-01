// SPDX-License-Identifier: GPL-3.0-only
// Experimental trusted-host source interface; not a plugin ABI.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include "sco/reserved_session.h"

namespace host_context {
struct Id { uint64_t high = 0, low = 0; };
enum class Mode : uint32_t { None, Local, Listen, Dedicated, Client };
enum class Transport : uint32_t { None, Host, Joiner, Relay };
enum class Provider : uint32_t { Unavailable, Synthetic, Native };
struct Input {
    uint32_t size = sizeof(Input);
    Id world{}; // zero means unknown
    Mode mode = Mode::None;
    Transport transport = Transport::None;
    Provider provider = Provider::Unavailable;
    bool authorityPeerKnown = false;
    uint64_t authorityPeer = 0;
};
struct Context {
    uint32_t size = sizeof(Context);
    Id sessionNamespace{}; // supplied by trusted host; process-scoped, not durable
    Id world{};
    uint64_t generation = 0;
    Mode mode = Mode::None;
    Transport transport = Transport::None;
    Provider provider = Provider::Unavailable;
    bool authorityPeerKnown = false;
    uint64_t authorityPeer = 0;
};
struct Generation { uint32_t size = sizeof(Generation); uint64_t value = 0; };
enum class Result { Ok, BadArg, Unavailable, Stale, Overflow, Failed };
class Adapter;
// Trusted local host only. One initialization per process; root is never freed.
Adapter* Start(Id sessionNamespace) noexcept;
// Failures leave the entire caller buffer untouched. On success, size and bytes
// beyond the known structure remain untouched; known-prefix padding is unspecified.
Result Read(Adapter&, Context*) noexcept;
// Expected generation is checked when state is captured under the mutex; a
// later replacement does not invalidate an already captured successful copy.
Result ReadExpected(Adapter&, uint64_t expected, Context*) noexcept;
Result Replace(Adapter&, const Input&, Generation*) noexcept;
Result Leave(Adapter&, Generation*) noexcept;
sco::reserved::RetireResult Stop(Adapter&, std::chrono::steady_clock::time_point) noexcept;
#ifdef HOST_CONTEXT_TESTING
// Typed, test-only rendezvous; no arbitrary callback or foreign context.
struct CopyBarrier { std::atomic<bool> reached{false}; std::atomic<bool> release{false}; };
void SetCopyBarrier(Adapter&, CopyBarrier*) noexcept;
bool SeedGeneration(Adapter&, uint64_t) noexcept;
#endif
} // namespace host_context
