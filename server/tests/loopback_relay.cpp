// SPDX-License-Identifier: GPL-3.0-only
// Test-only composition of the production relay library. Never installed or used by operators.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../config.h"
#include "../log.h"
#include "../net_boundary.h"
#include "../run_loop.h"
#include "../session.h"
#include <chrono>
#include <cstdio>
#include <string>

namespace {
uint64_t Now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Event {
    HANDLE value;
    ~Event() { if (value) CloseHandle(value); }
};
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    sco::relay::Settings settings;
    std::string why;
    if (!sco::relay::LoadConfig(argv[1], &settings, &why)) return 1;
    Event stop{OpenEventA(SYNCHRONIZE, FALSE, argv[2])};
    if (!stop.value) return 1;
    // This executable has no all-interface mode. Keep production main.cpp unchanged.
    auto socket = sco::relay::UdpSocket::Open(settings.port, true, &why);
    if (!socket) { std::fputs("loopback bind refused\n", stderr); return 1; }
    sco::relay::Logger log(settings.logLevel, sco::relay::StderrSink());
    sco::relay::RelaySession session(*socket, settings, log);
    if (!session.Start(Now(), &why)) return 1;
    sco::relay::RunLoop loop(session, settings, log);
    std::fputs("LOOPBACK_READY\n", stdout);
    std::fflush(stdout);
    const auto deadline = Now() + 45000;
    for (;;) {
        const auto state = WaitForSingleObject(stop.value, 0);
        if (state == WAIT_OBJECT_0) break;
        if (state != WAIT_TIMEOUT) return 1;
        if (Now() >= deadline) return 1;  // Independent backstop if the parent disappears.
        loop.Pass(Now());
        socket->Wait(loop.WaitMs());
    }
    session.Stop("loopback test stopped");
    loop.ReportStats(Now());
    std::fputs("LOOPBACK_STOPPED\n", stdout);
    return 0;
}
