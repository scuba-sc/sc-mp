// SPDX-License-Identifier: GPL-3.0-only
#include "context.h"
#include <limits>
#include <mutex>
#include <new>
#include <thread>

namespace host_context {
namespace r = sco::reserved;
class Adapter {
public:
    r::CoreBinding binding;
    r::Session session;
    std::mutex mutex;
    Context value;
#ifdef HOST_CONTEXT_TESTING
    std::atomic<CopyBarrier*> barrier{nullptr};
#endif
};
namespace {
bool Nonzero(Id id) { return id.high || id.low; }
bool Valid(const Input& in) {
    if (in.size < sizeof(Input)) return false;
    switch (in.mode) { case Mode::None: case Mode::Local: case Mode::Listen:
    case Mode::Dedicated: case Mode::Client: break; default: return false; }
    switch (in.transport) { case Transport::None: case Transport::Host:
    case Transport::Joiner: case Transport::Relay: break; default: return false; }
    switch (in.provider) { case Provider::Unavailable: case Provider::Synthetic: break;
    default: return false; } // Native is explicitly unsupported.
    return in.authorityPeerKnown ? in.authorityPeer != 0 : in.authorityPeer == 0;
}
void BeforeCopy(Adapter& a) {
#ifdef HOST_CONTEXT_TESTING
    if (auto* b = a.barrier.load(std::memory_order_acquire)) {
        b->reached.store(true, std::memory_order_release);
        while (!b->release.load(std::memory_order_acquire)) std::this_thread::yield();
    }
#else
    (void)a;
#endif
}
Result ReadImpl(Adapter& a, bool check, uint64_t expected, Context* out) noexcept {
    r::CallPin pin;
    if (r::TryEnter(a.session, pin) != sco::Result::Ok) return Result::Unavailable;
    if (!out || out->size < sizeof(Context)) return Result::BadArg;
    try {
        Context copy;
        {
            std::lock_guard lock(a.mutex);
            if (check && a.value.generation != expected) return Result::Stale;
            if (!Nonzero(a.value.world) || a.value.provider != Provider::Synthetic) return Result::Unavailable;
            copy = a.value;
        }
        BeforeCopy(a);
        const auto size = out->size;
        *out = copy;
        out->size = size;
        return Result::Ok;
    } catch (...) { r::Fault(a.session); return Result::Failed; }
}
}
Adapter* Start(Id sessionNamespace) noexcept {
    if (!Nonzero(sessionNamespace)) return nullptr;
    // Control is trusted and initialization is single-threaded. The backing is
    // intentionally leaked even when startup fails or retirement times out.
    static std::atomic<bool> attempted{false};
    static std::atomic<Adapter*> root{nullptr};
    if (attempted.exchange(true, std::memory_order_acq_rel)) return nullptr;
    auto* a = new (std::nothrow) Adapter;
    if (!a) return nullptr;
    root.store(a, std::memory_order_release);
    a->value.sessionNamespace = sessionNamespace;
    const auto* bootstrap = r::CreateExecutableAuthority();
    if (!bootstrap || r::BindCoreInstance(bootstrap, a->binding) != sco::Result::Ok) return nullptr;
    r::MockGraph graph{};
    if (r::ReserveSession(a->binding, graph, a->session) != sco::Result::Ok) return nullptr;
    r::StartupControl control;
    if (r::BeginStartup(a->session, control) != sco::Result::Ok ||
        r::PublishTypedProvider(control) != sco::Result::Ok ||
        r::FinishStartup(control) != sco::Result::Ok) return nullptr;
    return a;
}
Result Read(Adapter& a, Context* out) noexcept { return ReadImpl(a, false, 0, out); }
Result ReadExpected(Adapter& a, uint64_t expected, Context* out) noexcept { return ReadImpl(a, true, expected, out); }
Result Replace(Adapter& a, const Input& in, Generation* out) noexcept {
    r::CallPin pin;
    if (r::TryEnter(a.session, pin) != sco::Result::Ok) return Result::Unavailable;
    if (!out || out->size < sizeof(Generation) || !Valid(in)) return Result::BadArg;
    try {
        std::lock_guard lock(a.mutex);
        if (a.value.generation == (std::numeric_limits<uint64_t>::max)()) return Result::Overflow;
        Context next = a.value;
        next.world = in.world; next.mode = in.mode; next.transport = in.transport;
        next.provider = in.provider; next.authorityPeerKnown = in.authorityPeerKnown;
        next.authorityPeer = in.authorityPeer; ++next.generation;
        a.value = next;
        out->value = next.generation;
        return Result::Ok;
    } catch (...) { r::Fault(a.session); return Result::Failed; }
}
Result Leave(Adapter& a, Generation* out) noexcept {
    r::CallPin pin;
    if (r::TryEnter(a.session, pin) != sco::Result::Ok) return Result::Unavailable;
    if (!out || out->size < sizeof(Generation)) return Result::BadArg;
    try {
        std::lock_guard lock(a.mutex);
        if (a.value.generation == (std::numeric_limits<uint64_t>::max)()) {
            // Leave must invalidate even when the generation cannot advance.
            // Latch the real lifecycle closed while the state lock is held.
            r::Fault(a.session);
            return Result::Overflow;
        }
        Context empty{}; empty.sessionNamespace = a.value.sessionNamespace;
        empty.generation = a.value.generation + 1;
        a.value = empty; out->value = empty.generation;
        return Result::Ok;
    } catch (...) { r::Fault(a.session); return Result::Failed; }
}
r::RetireResult Stop(Adapter& a, std::chrono::steady_clock::time_point deadline) noexcept {
    // No own call pin while waiting for the census to drain.
    if (r::Close(a.session) != sco::Result::Ok && r::Status(a.session) != r::Phase::FailedRetained)
        return r::RetireResult::Refused;
    return r::RetireUntil(a.session, deadline);
}
#ifdef HOST_CONTEXT_TESTING
void SetCopyBarrier(Adapter& a, CopyBarrier* b) noexcept { a.barrier.store(b, std::memory_order_release); }
bool SeedGeneration(Adapter& a, uint64_t generation) noexcept {
    r::CallPin pin;
    if (r::TryEnter(a.session, pin) != sco::Result::Ok) return false;
    std::lock_guard lock(a.mutex); a.value.generation = generation; return true;
}
#endif
} // namespace host_context
