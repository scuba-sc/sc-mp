// SPDX-License-Identifier: GPL-3.0-only
#include "context.h"
#include "sco/owned_shutdown.h"
#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
using namespace host_context;
namespace {
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
Input Sample() {
    Input in; in.world = {0x33, 0x44}; in.mode = Mode::Listen;
    in.transport = Transport::Relay; in.provider = Provider::Synthetic;
    return in;
}
void Print(const char* label, const Context& c) {
    std::cout << label << " namespace=" << c.sessionNamespace.high << ':' << c.sessionNamespace.low
              << " world=" << c.world.high << ':' << c.world.low << " generation=" << c.generation
              << " mode=" << static_cast<unsigned>(c.mode) << " transport=" << static_cast<unsigned>(c.transport)
              << " provider=" << static_cast<unsigned>(c.provider)
              << " authority_known=" << c.authorityPeerKnown << " authority=" << c.authorityPeer << '\n';
}
void Basic(Adapter& a) {
    Context out; out.generation = 777;
    Check(Read(a, &out) == Result::Unavailable && out.generation == 777, "initial read unavailable without output");
    Generation g;
    auto in = Sample();
    Check(Replace(a, in, &g) == Result::Ok && g.value == 1 && g.size == sizeof(g), "first replacement");
    Check(Read(a, &out) == Result::Ok && out.size == sizeof(out) && out.generation == 1, "read and size prefix");
    Check(out.mode == Mode::Listen && out.transport == Transport::Relay && !out.authorityPeerKnown && !out.authorityPeer, "relay does not imply authority");
    Print("initial", out);
    Context independent = out;
    in.world = {0x55, 0x66}; in.mode = Mode::Client; in.transport = Transport::Joiner;
    in.authorityPeerKnown = true; in.authorityPeer = 9;
    Check(Replace(a, in, &g) == Result::Ok && g.value == 2, "replace");
    Check(independent.world.high == 0x33 && independent.generation == 1, "independent copy");
    out.generation = 777;
    Check(ReadExpected(a, 1, &out) == Result::Stale && out.generation == 777, "stale read no copy");
    Check(ReadExpected(a, 2, &out) == Result::Ok && out.authorityPeer == 9, "expected read");
    Print("replaced", out);
    Input invalid = in; invalid.provider = Provider::Native;
    g.value = 777;
    Check(Replace(a, invalid, &g) == Result::BadArg && g.value == 777, "native rejected");
    invalid = in; invalid.authorityPeerKnown = false;
    Check(Replace(a, invalid, &g) == Result::BadArg, "invalid whole input rejected");
    Check(ReadExpected(a, 2, &out) == Result::Ok, "invalid input did not commit");
    Context shortOut; shortOut.size = sizeof(Context) - 1; shortOut.generation = 777;
    Check(Read(a, &shortOut) == Result::BadArg && shortOut.generation == 777, "short output");
    in.world = {}; in.provider = Provider::Synthetic;
    Check(Replace(a, in, &g) == Result::Ok && g.value == 3, "unknown world commit");
    out.generation = 777;
    Check(Read(a, &out) == Result::Unavailable && out.generation == 777, "unknown world unavailable");
    in.world = {1, 2}; in.provider = Provider::Unavailable;
    Check(Replace(a, in, &g) == Result::Ok && g.value == 4, "unavailable provider commit");
    Check(Read(a, &out) == Result::Unavailable && out.generation == 777, "provider unavailable");
    Check(Leave(a, &g) == Result::Ok && g.value == 5, "leave increments generation");
    std::cout << "leave generation=" << g.value << " read=unavailable\n";
    Check(Read(a, &out) == Result::Unavailable, "leave invalidates");
    out.generation = 777;
    Check(ReadExpected(a, 2, &out) == Result::Stale && out.generation == 777, "leave invalidates old generation");
    for (auto mode : {Mode::None, Mode::Local, Mode::Listen, Mode::Dedicated, Mode::Client}) {
        for (auto transport : {Transport::None, Transport::Host, Transport::Joiner, Transport::Relay}) {
            auto metadata = Sample(); metadata.mode = mode; metadata.transport = transport;
            Check(Replace(a, metadata, &g) == Result::Ok, "mode and transport replacement");
            Check(ReadExpected(a, g.value, &out) == Result::Ok && out.mode == mode &&
                  out.transport == transport && !out.authorityPeerKnown && out.authorityPeer == 0,
                  "mode and transport copied independently");
        }
    }
    Check(Stop(a, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == sco::reserved::RetireResult::QuiescedRetained, "stop");
    out.generation = 777; g.value = 777;
    Check(Read(a, &out) == Result::Unavailable && out.generation == 777, "read after stop");
    Check(Replace(a, in, &g) == Result::Unavailable && g.value == 777, "replace after stop");
    Check(Leave(a, &g) == Result::Unavailable && g.value == 777, "leave after stop");
    std::cout << "stop quiesced-retained; subsequent calls unavailable\n";
}
void Overflow(Adapter& a) {
    auto in = Sample(); Generation g;
    Check(Replace(a, in, &g) == Result::Ok, "overflow setup");
    Check(SeedGeneration(a, (std::numeric_limits<uint64_t>::max)()), "seed generation");
    in.world = {9, 9}; g.value = 777;
    Check(Replace(a, in, &g) == Result::Overflow && g.value == 777, "replace overflow");
    Context out;
    Check(ReadExpected(a, (std::numeric_limits<uint64_t>::max)(), &out) == Result::Ok &&
          out.world.high == 0x33 && out.world.low == 0x44, "replacement overflow retains prior state");
    Check(Leave(a, &g) == Result::Overflow && g.value == 777, "leave overflow");
    out.generation = 777;
    Check(Read(a, &out) == Result::Unavailable && out.generation == 777, "leave overflow invalidates reads");
    Check(Replace(a, in, &g) == Result::Unavailable && g.value == 777, "leave overflow rejects replacement");
    Check(Stop(a, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == sco::reserved::RetireResult::FailedRetained, "overflow stop retained failure");
    std::cout << "replacement overflow refused; leave overflow latched failed-retained\n";
}
// Real extended objects: the last 16 extension bytes and trailing canary lie
// beyond the advertised size, but remain physically allocated.
template<class T> struct Extended {
    uint64_t before = 0x123456789abcdef0ULL;
    T value{};
    std::array<unsigned char, 32> extension{};
    uint64_t after = 0xfedcba9876543210ULL;
    Extended() {
        value.size = sizeof(T) + 16;
        for (size_t i = 0; i < extension.size(); ++i) extension[i] = static_cast<unsigned char>(i + 1);
    }
};
template<class T> auto Bytes(const T& value) {
    std::array<unsigned char, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    return bytes;
}
template<class T> void Unchanged(const T& value, const std::array<unsigned char, sizeof(T)>& before, const char* message) {
    Check(std::memcmp(&value, before.data(), sizeof(T)) == 0, message);
}
template<class T> void SuccessEnvelope(const Extended<T>& value) {
    Check(value.before == 0x123456789abcdef0ULL && value.after == 0xfedcba9876543210ULL &&
          value.value.size == sizeof(T) + 16, "success preserves size and canaries");
    for (size_t i = 0; i < value.extension.size(); ++i)
        Check(value.extension[i] == i + 1, "success preserves all extension bytes");
}
void OutputContract(Adapter& a) {
    Extended<Context> c; Extended<Generation> g;
    auto cb = Bytes(c);
    Check(Read(a, &c.value) == Result::Unavailable, "initial unavailable"); Unchanged(c, cb, "initial output unchanged");
    auto in = Sample();
    Check(Replace(a, in, &g.value) == Result::Ok && g.value.value == 1, "extended replacement"); SuccessEnvelope(g);
    Check(Read(a, &c.value) == Result::Ok && c.value.generation == 1 && c.value.world.high == 0x33 &&
          c.value.world.low == 0x44 && c.value.mode == Mode::Listen && c.value.transport == Transport::Relay &&
          c.value.provider == Provider::Synthetic && !c.value.authorityPeerKnown && c.value.authorityPeer == 0 &&
          c.value.sessionNamespace.high == 0x11 && c.value.sessionNamespace.low == 0x22, "extended read fields");
    SuccessEnvelope(c);
    cb = Bytes(c); Check(ReadExpected(a, 99, &c.value) == Result::Stale, "stale output"); Unchanged(c, cb, "stale unchanged");
    c.value.size = sizeof(Context) - 1; cb = Bytes(c);
    Check(Read(a, &c.value) == Result::BadArg, "short read"); Unchanged(c, cb, "short read unchanged");
    Check(ReadExpected(a, 1, &c.value) == Result::BadArg, "short expected read"); Unchanged(c, cb, "short expected unchanged");
    c.value.size = sizeof(Context) + 16;
    g.value.size = sizeof(Generation) - 1; auto gb = Bytes(g);
    Check(Replace(a, in, &g.value) == Result::BadArg, "short replace output"); Unchanged(g, gb, "short replace unchanged");
    Check(Leave(a, &g.value) == Result::BadArg, "short leave output"); Unchanged(g, gb, "short leave unchanged");
    g.value.size = sizeof(Generation) + 16;
    auto bad = in; bad.size = sizeof(Input) - 1; gb = Bytes(g);
    Check(Replace(a, bad, &g.value) == Result::BadArg, "short input"); Unchanged(g, gb, "short input unchanged");
    bad = in; bad.mode = static_cast<Mode>(999);
    Check(Replace(a, bad, &g.value) == Result::BadArg, "invalid mode"); Unchanged(g, gb, "invalid mode unchanged");
    bad = in; bad.transport = static_cast<Transport>(999);
    Check(Replace(a, bad, &g.value) == Result::BadArg, "invalid transport"); Unchanged(g, gb, "invalid transport unchanged");
    bad = in; bad.provider = static_cast<Provider>(999);
    Check(Replace(a, bad, &g.value) == Result::BadArg, "invalid provider"); Unchanged(g, gb, "invalid provider unchanged");
    bad = in; bad.provider = Provider::Native;
    Check(Replace(a, bad, &g.value) == Result::BadArg, "native provider"); Unchanged(g, gb, "native unchanged");
    bad = in; bad.authorityPeerKnown = true; bad.authorityPeer = 0;
    Check(Replace(a, bad, &g.value) == Result::BadArg, "known zero peer"); Unchanged(g, gb, "known zero unchanged");
    bad = in; bad.authorityPeerKnown = false; bad.authorityPeer = 42;
    Check(Replace(a, bad, &g.value) == Result::BadArg, "unknown nonzero peer"); Unchanged(g, gb, "unknown peer unchanged");
    cb = Bytes(c); gb = Bytes(g);
    Check(Read(a, nullptr) == Result::BadArg && ReadExpected(a, 1, nullptr) == Result::BadArg &&
          Replace(a, in, nullptr) == Result::BadArg && Leave(a, nullptr) == Result::BadArg, "null outputs");
    Unchanged(c, cb, "null reads leave unrelated context unchanged"); Unchanged(g, gb, "null writes leave unrelated generation unchanged");
    Check(ReadExpected(a, 1, &c.value) == Result::Ok, "invalid and null operations did not commit");
    in.world = {}; Check(Replace(a, in, &g.value) == Result::Ok, "unknown world setup"); cb = Bytes(c);
    Check(Read(a, &c.value) == Result::Unavailable, "unknown world"); Unchanged(c, cb, "unknown world unchanged");
    in.world = {1, 2}; in.provider = Provider::Unavailable;
    Check(Replace(a, in, &g.value) == Result::Ok, "unavailable provider setup"); cb = Bytes(c);
    Check(Read(a, &c.value) == Result::Unavailable, "unavailable provider"); Unchanged(c, cb, "provider unavailable unchanged");
    Check(Leave(a, &g.value) == Result::Ok && g.value.value == 4, "extended leave"); SuccessEnvelope(g);
    Check(Stop(a, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == sco::reserved::RetireResult::QuiescedRetained, "output contract stop");
    cb = Bytes(c); gb = Bytes(g);
    Check(Read(a, &c.value) == Result::Unavailable, "post-stop read"); Unchanged(c, cb, "post-stop read unchanged");
    Check(ReadExpected(a, 1, &c.value) == Result::Unavailable, "post-stop expected read"); Unchanged(c, cb, "post-stop expected unchanged");
    Check(Replace(a, in, &g.value) == Result::Unavailable, "post-stop replace"); Unchanged(g, gb, "post-stop replace unchanged");
    Check(Leave(a, &g.value) == Result::Unavailable, "post-stop leave"); Unchanged(g, gb, "post-stop leave unchanged");
    std::cout << "output contract: success envelopes and complete failure buffers preserved\n";
}
void OutputOverflow(Adapter& a) {
    Extended<Context> c; Extended<Generation> g;
    auto in = Sample(); Check(Replace(a, in, &g.value) == Result::Ok, "overflow setup");
    Check(SeedGeneration(a, (std::numeric_limits<uint64_t>::max)()), "seed output overflow"); auto gb = Bytes(g);
    Check(Replace(a, in, &g.value) == Result::Overflow, "replace overflow output"); Unchanged(g, gb, "replace overflow unchanged");
    Check(Leave(a, &g.value) == Result::Overflow, "leave overflow output"); Unchanged(g, gb, "leave overflow unchanged");
    auto cb = Bytes(c);
    Check(Read(a, &c.value) == Result::Unavailable, "failed admission read"); Unchanged(c, cb, "failed admission read unchanged");
    Check(ReadExpected(a, 1, &c.value) == Result::Unavailable, "failed admission expected read"); Unchanged(c, cb, "failed admission expected unchanged");
    Check(Replace(a, in, &g.value) == Result::Unavailable, "failed admission replace"); Unchanged(g, gb, "failed admission replace unchanged");
    Check(Leave(a, &g.value) == Result::Unavailable, "failed admission leave"); Unchanged(g, gb, "failed admission leave unchanged");
    Check(Stop(a, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == sco::reserved::RetireResult::FailedRetained, "overflow stop");
    Check(Read(a, &c.value) == Result::Unavailable, "failed stop read"); Unchanged(c, cb, "failed stop read unchanged");
    Check(Replace(a, in, &g.value) == Result::Unavailable, "failed stop replace"); Unchanged(g, gb, "failed stop replace unchanged");
    Check(Leave(a, &g.value) == Result::Unavailable, "failed stop leave"); Unchanged(g, gb, "failed stop leave unchanged");
    std::cout << "output overflow: complete failure buffers preserved through failed-retained stop\n";
}

Input Correlated(uint64_t generation) {
    Input in; in.world = {generation, generation ^ 0x55};
    in.mode = generation % 2 ? Mode::Listen : Mode::Client;
    in.transport = generation % 2 ? Transport::Relay : Transport::Joiner;
    in.provider = Provider::Synthetic; in.authorityPeerKnown = true; in.authorityPeer = generation + 1000;
    return in;
}
bool Coherent(const Context& c) {
    const auto in = Correlated(c.generation);
    return c.generation >= 1 && c.generation <= 129 && c.sessionNamespace.high == 0x11 &&
           c.sessionNamespace.low == 0x22 && c.world.high == in.world.high && c.world.low == in.world.low &&
           c.mode == in.mode && c.transport == in.transport && c.provider == in.provider &&
           c.authorityPeerKnown && c.authorityPeer == in.authorityPeer;
}
void ReplaceDuringCopy(Adapter& a) {
    Generation g;
    Check(Replace(a, Correlated(1), &g) == Result::Ok && g.value == 1, "copy race setup");
    CopyBarrier barrier; SetCopyBarrier(a, &barrier);
    Extended<Context> old; const auto before = Bytes(old);
    Result result = Result::Failed;
    std::thread caller([&] { result = ReadExpected(a, 1, &old.value); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!barrier.reached.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    if (!barrier.reached.load(std::memory_order_acquire)) {
        barrier.release.store(true, std::memory_order_release); caller.join();
        Check(false, "copy race reached barrier");
    }
    Unchanged(old, before, "held read output remains untouched");
    Check(Replace(a, Correlated(2), &g) == Result::Ok && g.value == 2, "replacement during held copy");
    SetCopyBarrier(a, nullptr); // The held caller already owns its local barrier pointer.
    Context current;
    Check(Read(a, &current) == Result::Ok && current.generation == 2 && Coherent(current), "current read sees replacement");
    barrier.release.store(true, std::memory_order_release); caller.join();
    Check(result == Result::Ok && old.value.generation == 1 && Coherent(old.value), "captured old read completes");
    SuccessEnvelope(old);
    const auto captured = Bytes(old);
    Check(ReadExpected(a, 1, &old.value) == Result::Stale, "old generation now stale");
    Unchanged(old, captured, "stale read preserves complete old copy");
    Check(Stop(a, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == sco::reserved::RetireResult::QuiescedRetained, "copy race stop");
    std::cout << "replacement during held copy: current generation 2, captured generation 1, then stale\n";
}
void SuccessDrain(Adapter& a) {
    Generation g;
    Check(Replace(a, Correlated(1), &g) == Result::Ok && g.value == 1, "success drain setup");
    CopyBarrier barrier; SetCopyBarrier(a, &barrier);
    Extended<Context> old; const auto oldBefore = Bytes(old);
    Result readResult = Result::Failed;
    std::thread caller([&] { readResult = ReadExpected(a, 1, &old.value); });
    const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!barrier.reached.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < waitDeadline)
        std::this_thread::yield();
    if (!barrier.reached.load(std::memory_order_acquire)) {
        barrier.release.store(true, std::memory_order_release); caller.join();
        Check(false, "success drain copy barrier");
    }
    std::atomic<bool> retirementDone{false};
    sco::reserved::RetireResult retired = sco::reserved::RetireResult::Refused;
    const auto retireDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::thread stopper([&] {
        retired = Stop(a, retireDeadline);
        retirementDone.store(true, std::memory_order_release);
    });
    while (!sco::reserved::testing::ReaderDrainObserved() &&
           !retirementDone.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < retireDeadline)
        std::this_thread::yield();
    const bool observed = sco::reserved::testing::ReaderDrainObserved();
    const bool stillWaiting = !retirementDone.load(std::memory_order_acquire);
    const bool oldUntouched = std::memcmp(&old, oldBefore.data(), sizeof(old)) == 0;
    Extended<Context> rejected; const auto rejectedBefore = Bytes(rejected);
    const auto rejectedResult = Read(a, &rejected.value);
    const bool rejectedUntouched = std::memcmp(&rejected, rejectedBefore.data(), sizeof(rejected)) == 0;
    const bool releasedBeforeDeadline = std::chrono::steady_clock::now() < retireDeadline;
    barrier.release.store(true, std::memory_order_release);
    caller.join(); stopper.join(); SetCopyBarrier(a, nullptr);
    Check(observed && stillWaiting && releasedBeforeDeadline, "real retirement observed waiting for admitted reader before deadline");
    Check(oldUntouched, "success drain held output untouched");
    Check(rejectedResult == Result::Unavailable && rejectedUntouched, "closing rejects new read without output");
    Check(readResult == Result::Ok && old.value.generation == 1 && Coherent(old.value), "held read completed old copy");
    SuccessEnvelope(old);
    Check(retired == sco::reserved::RetireResult::QuiescedRetained, "retirement quiesced after copy");
    Check(Read(a, &rejected.value) == Result::Unavailable, "quiesced read unavailable");
    Unchanged(rejected, rejectedBefore, "quiesced read output untouched");
    std::cout << "success drain: core observed reader, copy completed, retirement quiesced-retained\n";
}
void LeaveDuringCopy(Adapter& a) {
    Generation g;
    Check(Replace(a, Correlated(1), &g) == Result::Ok && g.value == 1, "leave copy setup");
    CopyBarrier barrier; SetCopyBarrier(a, &barrier);
    Extended<Context> old; const auto oldBefore = Bytes(old);
    Result readResult = Result::Failed;
    std::thread caller([&] { readResult = ReadExpected(a, 1, &old.value); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!barrier.reached.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    if (!barrier.reached.load(std::memory_order_acquire)) {
        barrier.release.store(true, std::memory_order_release); caller.join();
        Check(false, "leave copy barrier");
    }
    const bool oldUntouched = std::memcmp(&old, oldBefore.data(), sizeof(old)) == 0;
    const auto leaveResult = Leave(a, &g);
    SetCopyBarrier(a, nullptr);
    Extended<Context> fresh; const auto freshBefore = Bytes(fresh);
    const auto unavailable = Read(a, &fresh.value);
    const bool unavailableUntouched = std::memcmp(&fresh, freshBefore.data(), sizeof(fresh)) == 0;
    const auto stale = ReadExpected(a, 1, &fresh.value);
    const bool staleUntouched = std::memcmp(&fresh, freshBefore.data(), sizeof(fresh)) == 0;
    barrier.release.store(true, std::memory_order_release); caller.join();
    Check(oldUntouched && leaveResult == Result::Ok && g.value == 2, "leave increments while copy held");
    Check(unavailable == Result::Unavailable && unavailableUntouched, "leave makes fresh context unavailable");
    Check(stale == Result::Stale && staleUntouched, "leave makes old generation stale");
    Check(readResult == Result::Ok && old.value.generation == 1 && Coherent(old.value), "captured pre-leave copy completes");
    SuccessEnvelope(old);
    Check(Stop(a, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == sco::reserved::RetireResult::QuiescedRetained, "leave copy stop");
    std::cout << "leave during copy: new reads unavailable, captured generation 1 completed\n";
}
bool WaitFlag(const std::atomic<bool>& flag, std::chrono::steady_clock::time_point deadline) {
    while (!flag.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    return flag.load(std::memory_order_acquire);
}
void OwnedMaintenance(Adapter& a) {
    // Separate real host-source owner: it is not the adapter's reserved Session.
    using Owner = sco::OwnedShutdown;
    using Reap = Owner::ReapResult;
    using namespace std::chrono_literals;
    Generation generation;
    Check(Replace(a, Correlated(1), &generation) == Result::Ok, "companion adapter setup");
    static auto* pending = new Owner;
    Check(pending->reap_retained_until(std::chrono::steady_clock::now() + 2s) == Reap::Refused,
          "fresh maintenance refused");
    { auto read = pending->try_read(); Check(static_cast<bool>(read), "fresh owner remains open"); }
    std::atomic<bool> bodyEntered{false}, bodyRelease{false};
    std::atomic<bool> tlsEntered{false}, tlsRelease{false}, tlsDestroyed{false};
    struct Tail {
        std::atomic<bool>* entered;
        std::atomic<bool>* release;
        std::atomic<bool>* destroyed;
        ~Tail() {
            entered->store(true, std::memory_order_release);
            while (!release->load(std::memory_order_acquire)) std::this_thread::yield();
            destroyed->store(true, std::memory_order_release);
        }
    };
    const bool started = pending->start_worker([&](const auto&) {
        thread_local Tail tail{&tlsEntered, &tlsRelease, &tlsDestroyed};
        (void)tail;
        bodyEntered.store(true, std::memory_order_release);
        while (!bodyRelease.load(std::memory_order_acquire)) std::this_thread::yield();
    });
    Check(started, "companion fixture worker started");
    if (!WaitFlag(bodyEntered, std::chrono::steady_clock::now() + 2s)) {
        bodyRelease.store(true, std::memory_order_release);
        tlsRelease.store(true, std::memory_order_release);
        (void)pending->drain_until(std::chrono::steady_clock::now() + 2s);
        Check(false, "companion worker entered");
    }
    const bool failedDrain = !pending->drain_until(std::chrono::steady_clock::now() + 50ms);
    const auto bodyPending = pending->reap_retained_until(std::chrono::steady_clock::now() + 50ms);
    const bool closedAfterFailure = pending->closed() && !pending->ready() && !pending->try_read();
    Context separate;
    const auto separateRead = ReadExpected(a, 1, &separate);
    bodyRelease.store(true, std::memory_order_release);
    if (!WaitFlag(tlsEntered, std::chrono::steady_clock::now() + 2s)) {
        tlsRelease.store(true, std::memory_order_release);
        (void)pending->reap_retained_until(std::chrono::steady_clock::now() + 2s);
        Check(false, "companion worker TLS entered");
    }
    const auto tlsPending = pending->reap_retained_until(std::chrono::steady_clock::now() + 50ms);
    const bool tlsStillAlive = !tlsDestroyed.load(std::memory_order_acquire);
    tlsRelease.store(true, std::memory_order_release);
    const auto reaped = pending->reap_retained_until(std::chrono::steady_clock::now() + 2s);
    const bool stayedClosed = pending->closed() && !pending->ready() && !pending->try_read();
    const bool noRestart = !pending->start_worker([](const auto&) {});
    const bool noSecondDrain = !pending->drain_until(std::chrono::steady_clock::now() + 2s);
    const auto repeatReap = pending->reap_retained_until(std::chrono::steady_clock::now() + 2s);
    Check(failedDrain && bodyPending == Reap::PendingRetained && closedAfterFailure,
          "failed drain and held worker remain retained");
    Check(separateRead == Result::Ok && separate.generation == 1 && Coherent(separate),
          "companion failure does not alter separate adapter session");
    Check(tlsPending == Reap::PendingRetained && tlsStillAlive, "TLS must exit before real join");
    Check(reaped == Reap::ReapedRetained && tlsDestroyed.load(std::memory_order_acquire),
          "maintenance joins worker and TLS");
    Check(stayedClosed && noRestart && noSecondDrain && repeatReap == Reap::ReapedRetained,
          "maintenance never reopens or retires failed owner");

    static auto* self = new Owner;
    std::atomic<bool> selfDone{false}, selfRefused{false};
    Check(self->start_worker([&](const auto&) {
        const bool drainRefused = !self->drain_until(std::chrono::steady_clock::now() + 2s);
        const auto maintenance = self->reap_retained_until(std::chrono::steady_clock::now() + 2s);
        selfRefused.store(drainRefused && maintenance == Reap::Refused, std::memory_order_release);
        selfDone.store(true, std::memory_order_release);
    }), "self-reentry fixture worker started");
    Check(WaitFlag(selfDone, std::chrono::steady_clock::now() + 2s), "self-reentry fixture completed");
    const auto selfReaped = self->reap_retained_until(std::chrono::steady_clock::now() + 2s);
    Check(selfRefused.load(std::memory_order_acquire) && selfReaped == Reap::ReapedRetained &&
          self->closed() && !self->ready() && !self->try_read(), "worker self-drain and self-maintenance refused");

    static auto* completed = new Owner;
    Check(completed->drain_until(std::chrono::steady_clock::now() + 2s) && completed->ready(),
          "companion successful drain");
    Check(completed->reap_retained_until(std::chrono::steady_clock::now() + 2s) == Reap::Refused &&
          completed->ready(), "maintenance refused after successful drain");
    Context final;
    Check(ReadExpected(a, 1, &final) == Result::Ok && Coherent(final), "separate adapter remains running");
    Check(Stop(a, std::chrono::steady_clock::now() + 2s) == sco::reserved::RetireResult::QuiescedRetained,
          "adapter stops separately");
    std::cout << "companion OwnedShutdown: retained maintenance reaps worker/TLS without reopening; adapter separate\n";
}
void Concurrent(Adapter& a) {
    Generation g; Check(Replace(a, Correlated(1), &g) == Result::Ok && g.value == 1, "concurrent setup");
    std::atomic<bool> done{false}; std::atomic<uint64_t> reads{0};
    std::array<Context, 128> retained{}; size_t retainedCount = 0;
    std::thread caller([&] {
        while (!done.load(std::memory_order_acquire)) {
            Context copy;
            Check(Read(a, &copy) == Result::Ok && Coherent(copy), "concurrent read coherent");
            if (retainedCount < retained.size()) retained[retainedCount++] = copy;
            Context expected; const auto before = Bytes(expected);
            const auto status = ReadExpected(a, copy.generation, &expected);
            if (status == Result::Ok) Check(expected.generation == copy.generation && Coherent(expected), "expected read coherent");
            else { Check(status == Result::Stale, "expected read stale or captured generation"); Unchanged(expected, before, "stale concurrent output"); }
            reads.fetch_add(1, std::memory_order_release);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (uint64_t generation = 2; generation <= 129; ++generation) {
        const auto before = reads.load(std::memory_order_acquire);
        Check(Replace(a, Correlated(generation), &g) == Result::Ok && g.value == generation, "concurrent replace");
        if (generation % 8 == 0) {
            while (reads.load(std::memory_order_acquire) == before && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            Check(reads.load(std::memory_order_acquire) != before, "reader makes bounded progress during writes");
        }
    }
    done.store(true, std::memory_order_release); caller.join();
    Check(retainedCount > 0 && reads.load() >= 16, "concurrent reads observed");
    for (size_t i = 0; i < retainedCount; ++i) Check(Coherent(retained[i]), "returned copies remain independent");
    Check(Stop(a, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == sco::reserved::RetireResult::QuiescedRetained, "concurrent stop");
    std::cout << "coherence: 128 replacements, " << reads.load() << " reads, " << retainedCount << " retained copies\n";
}
void Drain(Adapter& a, bool immediate) {
    auto in = Sample(); Generation g;
    Check(Replace(a, in, &g) == Result::Ok, "drain setup");
    CopyBarrier barrier; SetCopyBarrier(a, &barrier);
    Context out; out.generation = 777; Result read = Result::Failed;
    std::thread caller([&] { read = Read(a, &out); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!barrier.reached.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    if (!barrier.reached.load(std::memory_order_acquire)) { barrier.release.store(true); caller.join(); Check(false, "caller reached copy barrier"); }
    Check(out.generation == 777, "output remains untouched at copy barrier");
    const auto retireDeadline = std::chrono::steady_clock::now() +
        (immediate ? std::chrono::milliseconds(0) : std::chrono::milliseconds(200));
    const auto retired = Stop(a, retireDeadline);
    Context after; after.generation = 777;
    const auto rejected = Read(a, &after);
    barrier.release.store(true, std::memory_order_release); caller.join();
    SetCopyBarrier(a, nullptr);
    Check(retired == sco::reserved::RetireResult::FailedRetained, "held pin prevents quiescence");
    Check(read == Result::Ok && out.generation == 1 && out.world.high == 0x33, "admitted copy completes");
    Check(rejected == Result::Unavailable && after.generation == 777, "failed retained rejects new reads");
    std::cout << "held copy (" << (immediate ? "immediate" : "finite")
              << " deadline): failed-retained, then admitted output completed\n";
}
}
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: host-context basic|overflow|drain|drain-now|output-contract|output-overflow|coherence|replace-during-copy|success-drain|leave-during-copy|owned-maintenance\n"; return 2; }
    auto* a = Start({0x11, 0x22}); Check(a != nullptr, "real executable authority startup");
    const std::string_view mode(argv[1]);
    if (mode == "basic") Basic(*a); else if (mode == "overflow") Overflow(*a);
    else if (mode == "output-contract") OutputContract(*a); else if (mode == "output-overflow") OutputOverflow(*a);
    else if (mode == "coherence") Concurrent(*a); else if (mode == "replace-during-copy") ReplaceDuringCopy(*a);
    else if (mode == "success-drain") SuccessDrain(*a); else if (mode == "leave-during-copy") LeaveDuringCopy(*a);
    else if (mode == "owned-maintenance") OwnedMaintenance(*a);
    else if (mode == "drain") Drain(*a, false); else if (mode == "drain-now") Drain(*a, true); else return 2;
}
