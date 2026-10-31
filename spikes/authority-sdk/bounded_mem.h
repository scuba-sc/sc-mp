// SPDX-License-Identifier: GPL-3.0-only
// Adapted from sco-core tests/net_mem.h, sdk-v2.4.0-rc.2
// source 844e136ccb280ad8cb43f73bc43c9447e8f6799d (GPL-3.0-only).
// Changes: bounded shared medium, explicit rejection accounting and fail-closed receive.
#pragma once
#include "sco/net/transport.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <utility>
#include <vector>

namespace boundedtest {
using sco::net::Endpoint;
inline Endpoint MemEndpoint(uint16_t id) {
    Endpoint e; e.family = Endpoint::kMemory; e.port = id; return e;
}
class MemNet {
public:
    struct Datagram {
        uint64_t at = 0, order = 0;
        Endpoint from, to;
        std::vector<uint8_t> bytes;
    };
    explicit MemNet(uint64_t seed, size_t maxDatagrams = 256, size_t maxBytes = 262144)
        : maxDatagrams_(maxDatagrams), maxBytes_(maxBytes), rng_(seed) {}
    double loss = 0.0, dup = 0.0;
    uint32_t minDelayMs = 1, maxDelayMs = 1;
    uint64_t now = 0;
    std::function<bool(Datagram&)> tap;
    // Rejections count individual copies that could not be queued. High-water
    // marks measure queued datagrams and payload bytes, not transient tap data.
    size_t rejected = 0, highWaterDatagrams = 0, highWaterBytes = 0;
    static constexpr size_t maxDatagramBytes = 2048;

    // False means at least one requested copy was rejected by a bound. A tap
    // drop or simulated loss is a successful send, as with a real datagram wire.
    bool Send(const Endpoint& from, const Endpoint& to, const uint8_t* data, size_t len) {
        if (len > maxDatagramBytes || (len && !data)) { ++rejected; return false; }
        Datagram d; d.from = from; d.to = to;
        if (len) d.bytes.assign(data, data + len);
        if (tap && !tap(d)) return true;
        if (d.bytes.size() > maxDatagramBytes) { ++rejected; return false; }
        if (Chance(loss)) return true;
        const int copies = Chance(dup) ? 2 : 1;
        bool ok = true;
        for (int i = 0; i < copies; ++i) {
            Datagram c = d; c.at = now + Delay();
            if (!Enqueue(std::move(c))) ok = false;
        }
        return ok;
    }
    bool Inject(const Endpoint& from, const Endpoint& to, std::vector<uint8_t> bytes) {
        Datagram d; d.from = from; d.to = to; d.bytes = std::move(bytes); d.at = now;
        return Enqueue(std::move(d));
    }
    bool Take(const Endpoint& to, Datagram* out) {
        if (!out) return false;
        size_t best = q_.size();
        for (size_t i = 0; i < q_.size(); ++i) {
            const auto& d = q_[i];
            if (!(d.to == to) || d.at > now) continue;
            if (best == q_.size() || d.at < q_[best].at ||
                (d.at == q_[best].at && d.order < q_[best].order)) best = i;
        }
        if (best == q_.size()) return false;
        bytes_ -= q_[best].bytes.size();
        *out = std::move(q_[best]);
        q_.erase(q_.begin() + static_cast<std::ptrdiff_t>(best));
        return true;
    }
    size_t InFlight() const { return q_.size(); }
    size_t InFlightBytes() const { return bytes_; }
private:
    bool Enqueue(Datagram&& d) {
        const size_t n = d.bytes.size();
        if (n > maxDatagramBytes || q_.size() >= maxDatagrams_ || n > maxBytes_ - bytes_) {
            ++rejected; return false;
        }
        d.order = order_++;
        q_.push_back(std::move(d)); bytes_ += n;
        if (q_.size() > highWaterDatagrams) highWaterDatagrams = q_.size();
        if (bytes_ > highWaterBytes) highWaterBytes = bytes_;
        return true;
    }
    bool Chance(double p) { return p > 0.0 && std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < p; }
    uint32_t Delay() {
        if (maxDelayMs <= minDelayMs) return minDelayMs;
        return std::uniform_int_distribution<uint32_t>(minDelayMs, maxDelayMs)(rng_);
    }
    const size_t maxDatagrams_, maxBytes_;
    size_t bytes_ = 0;
    std::mt19937_64 rng_;
    uint64_t order_ = 0;
    std::vector<Datagram> q_;
};
class MemTransport : public sco::net::Transport {
public:
    MemTransport(MemNet& net, uint16_t id) : net_(net), self_(MemEndpoint(id)) {}
    const Endpoint& Self() const { return self_; }
    bool Send(const Endpoint& to, const uint8_t* data, size_t len) override { return net_.Send(self_, to, data, len); }
    bool Receive(Endpoint* from, uint8_t* buf, size_t cap, size_t* len) override {
        MemNet::Datagram d;
        if (!net_.Take(self_, &d)) return false;
        if (d.bytes.size()>cap) { *len=0; ++net_.rejected; return false; }
        *from = d.from; *len = d.bytes.size();
        const size_t n = d.bytes.size() < cap ? d.bytes.size() : cap;
        for (size_t i = 0; i < n; ++i) buf[i] = d.bytes[i];
        return true;
    }
private:
    MemNet& net_;
    Endpoint self_;
};
}
