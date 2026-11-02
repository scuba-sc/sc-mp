#include "bounded_mem.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace boundedtest;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
    const auto a = MemEndpoint(1), b = MemEndpoint(2);
    const uint8_t byte = 1;
    MemNet::Datagram d;
    {
        MemNet n(1); n.minDelayMs = n.maxDelayMs = 0;
        for (int i = 0; i < 256; ++i) CHECK(n.Send(i % 2 ? b : a, i % 2 ? a : b, &byte, 1));
        CHECK(!n.Send(a, b, &byte, 1)); CHECK(!n.Inject(b, a, {1}));
        CHECK(n.InFlight() == 256 && n.InFlightBytes() == 256 && n.rejected == 2);
        CHECK(n.highWaterDatagrams == 256 && n.highWaterBytes == 256);
        CHECK(n.Take(a, &d)); CHECK(n.Send(b, a, &byte, 1)); CHECK(n.Take(b, &d));
        CHECK(n.Inject(a, b, {1})); CHECK(n.InFlight() == 256);
    }
    {
        MemNet n(2); n.minDelayMs = n.maxDelayMs = 0;
        std::vector<uint8_t> payload(2048, 9);
        for (int i = 0; i < 128; ++i) CHECK(n.Inject(i % 2 ? b : a, i % 2 ? a : b, payload));
        CHECK(n.InFlightBytes() == 262144); CHECK(!n.Send(a, b, &byte, 1));
        CHECK(!n.Inject(b, a, {1})); CHECK(n.rejected == 2);
        CHECK(n.Take(a, &d)); CHECK(n.Send(b, a, payload.data(), payload.size()));
        CHECK(n.Take(b, &d)); CHECK(n.Inject(a, b, payload));
        CHECK(n.InFlightBytes() == 262144 && n.highWaterBytes == 262144);
        payload.push_back(1); CHECK(!n.Inject(a, b, payload));
        CHECK(!n.Send(b, a, payload.data(), payload.size())); CHECK(n.rejected == 4);
    }
    {
        MemNet n(3, 3, 5); n.minDelayMs = n.maxDelayMs = 0; n.dup = 1;
        uint8_t pair[2] = {1, 2};
        CHECK(n.Send(a, b, pair, 2)); CHECK(n.InFlight() == 2 && n.InFlightBytes() == 4);
        CHECK(!n.Send(b, a, pair, 2)); CHECK(n.rejected == 2);
        CHECK(n.Take(b, &d)); CHECK(!n.Send(b, a, pair, 2));
        CHECK(n.InFlight() == 2 && n.InFlightBytes() == 4 && n.rejected == 3);
        CHECK(n.Take(a, &d)); CHECK(n.Take(b, &d)); CHECK(n.InFlightBytes() == 0);
    }
    {
        MemNet n(4, 2, 4); n.minDelayMs = n.maxDelayMs = 0;
        int taps = 0;
        n.tap = [&](MemNet::Datagram& packet) {
            ++taps; CHECK(n.Inject(b, a, {8, 9})); packet.bytes = {3, 4}; return true;
        };
        CHECK(n.Send(a, b, &byte, 1)); CHECK(taps == 1 && n.InFlight() == 2);
        CHECK(!n.Inject(b, a, {1})); CHECK(n.Take(a, &d) && d.bytes[0] == 8);
        CHECK(n.Take(b, &d) && d.bytes[0] == 3);
        n.tap = [&](MemNet::Datagram& packet) { packet.bytes.resize(2049); return true; };
        CHECK(!n.Send(a, b, &byte, 1)); CHECK(n.InFlight() == 0);
        n.tap = [](MemNet::Datagram&) { return false; };
        CHECK(n.Send(a, b, &byte, 1) && n.InFlight() == 0);
    }
    {
        MemNet n(5, 1, 4); n.minDelayMs = n.maxDelayMs = 0;
        MemTransport ta(n, 1), tb(n, 2);
        CHECK(ta.Send(b, &byte, 1)); CHECK(!tb.Send(a, &byte, 1));
        Endpoint from; uint8_t out = 0; size_t len = 0;
        CHECK(tb.Receive(&from, &out, 1, &len)); CHECK(from == a && out == 1 && len == 1);
        CHECK(tb.Send(a, &byte, 1)); CHECK(ta.Receive(&from, &out, 1, &len)); CHECK(from == b);
        CHECK(n.Inject(a,b,{1,2})); CHECK(!tb.Receive(&from,&out,1,&len)); CHECK(len==0);
    }
    std::puts("PASS: bounded medium tests");
}
