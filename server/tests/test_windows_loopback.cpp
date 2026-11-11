// SPDX-License-Identifier: GPL-3.0-only
// Synthetic process test only. Every socket we create is bound to 127.0.0.1.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include "sco/net/core.h"
#include "sco/net/transport.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
uint64_t Now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Winsock {
    Winsock() { WSADATA d{}; Check(WSAStartup(MAKEWORD(2,2), &d) == 0, "WSAStartup"); }
    ~Winsock() { WSACleanup(); }
};
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        path = std::filesystem::temp_directory_path() /
            ("sc-mp-loopback-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Now()));
        Check(std::filesystem::create_directory(path), "create temporary directory");
    }
    ~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

class Socket final : public sco::net::Transport {
public:
    SOCKET fd = INVALID_SOCKET;
    WSAEVENT event = WSA_INVALID_EVENT;
    Socket() {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Check(fd != INVALID_SOCKET, "socket");
        try {
            const BOOL exclusive = TRUE;
            Check(setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive), sizeof exclusive) == 0, "exclusive reservation");
            sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            Check(bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0, "loopback bind");
            event = WSACreateEvent(); Check(event != WSA_INVALID_EVENT, "socket event");
            Check(WSAEventSelect(fd, event, FD_READ) == 0, "select socket event");
        } catch (...) { Close(); throw; }
    }
    ~Socket() override { Close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    void Close() {
        if (fd != INVALID_SOCKET) { closesocket(fd); fd = INVALID_SOCKET; }
        if (event != WSA_INVALID_EVENT) { WSACloseEvent(event); event = WSA_INVALID_EVENT; }
    }
    uint16_t Port() const {
        sockaddr_in a{}; int n = sizeof a;
        Check(getsockname(fd, reinterpret_cast<sockaddr*>(&a), &n) == 0, "socket name");
        Check(a.sin_addr.s_addr == htonl(INADDR_LOOPBACK), "fixture socket escaped loopback");
        return ntohs(a.sin_port);
    }
    bool Send(const sco::net::Endpoint& to, const uint8_t* bytes, size_t count) override {
        Check(to.family == 4 && to.addr[0] == 127 && to.addr[1] == 0 &&
            to.addr[2] == 0 && to.addr[3] == 1, "send escaped loopback");
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(to.port);
        std::memcpy(&a.sin_addr, to.addr, 4);
        return sendto(fd, reinterpret_cast<const char*>(bytes), static_cast<int>(count), 0,
            reinterpret_cast<sockaddr*>(&a), sizeof a) == static_cast<int>(count);
    }
    bool Receive(sco::net::Endpoint* from, uint8_t* bytes, size_t cap, size_t* count) override {
        sockaddr_in a{}; int n = sizeof a;
        const int result = recvfrom(fd, reinterpret_cast<char*>(bytes), static_cast<int>(cap), 0,
            reinterpret_cast<sockaddr*>(&a), &n);
        if (result < 0) { Check(WSAGetLastError() == WSAEWOULDBLOCK, "recvfrom"); return false; }
        Check(a.sin_addr.s_addr == htonl(INADDR_LOOPBACK), "received non-loopback datagram");
        *from = {}; from->family = 4; from->port = ntohs(a.sin_port);
        std::memcpy(from->addr, &a.sin_addr, 4); *count = static_cast<size_t>(result); return true;
    }
    void Reset() { WSANETWORKEVENTS e{}; Check(WSAEnumNetworkEvents(fd, event, &e) == 0, "reset socket event"); }
};

// Inspect both families: exactly one IPv4 UDP endpoint, no hidden IPv6 endpoint.
void CheckChildSockets(DWORD pid, uint16_t port, bool running) {
    unsigned owned = 0;
    for (const ULONG family : {static_cast<ULONG>(AF_INET), static_cast<ULONG>(AF_INET6)}) {
        ULONG size = 0;
        DWORD result = GetExtendedUdpTable(nullptr, &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
        Check(result == ERROR_INSUFFICIENT_BUFFER, "UDP owner table size");
        std::vector<unsigned char> bytes(size);
        for (unsigned retry = 0; ; ++retry) {
            result = GetExtendedUdpTable(bytes.data(), &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
            if (result != ERROR_INSUFFICIENT_BUFFER || retry == 2) break;
            bytes.resize(size);
        }
        Check(result == NO_ERROR, "UDP owner table read");
        if (family == AF_INET) {
            const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(bytes.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& row = table->table[i];
                if (row.dwOwningPid != pid) continue;
                ++owned;
                Check(row.dwLocalAddr == htonl(INADDR_LOOPBACK) &&
                    ntohs(static_cast<u_short>(row.dwLocalPort)) == port, "child endpoint escaped loopback");
            }
        } else {
            const auto* table = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(bytes.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i)
                if (table->table[i].dwOwningPid == pid) ++owned;
        }
    }
    Check(owned == (running ? 1u : 0u), "unexpected child UDP socket count");
}

// No reader thread that could hang cleanup: poll a bounded pipe and the process handle.
class Child {
    Handle job_, process_, pipe_;
    DWORD pid_ = 0;
    std::string output_;
public:
    Child(const std::wstring& executable, const std::wstring& args, const std::filesystem::path& cwd) {
        // Closing this handle, including on parent termination by CTest, kills the child.
        job_.value = CreateJobObjectW(nullptr, nullptr);
        Check(job_.value != nullptr, "create child job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Check(SetInformationJobObject(job_.value, JobObjectExtendedLimitInformation,
            &limits, sizeof limits) != 0, "set child job limits");
        SECURITY_ATTRIBUTES security{sizeof security, nullptr, TRUE};
        Handle write, input;
        Check(CreatePipe(&pipe_.value, &write.value, &security, 0) != 0, "child pipe");
        Check(SetHandleInformation(pipe_.value, HANDLE_FLAG_INHERIT, 0) != 0, "pipe inheritance");
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, 0, nullptr);
        Check(input.value != INVALID_HANDLE_VALUE, "child stdin");
        STARTUPINFOW startup{}; startup.cb = sizeof startup; startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write.value; startup.hStdError = write.value; startup.hStdInput = input.value;
        std::wstring command = L"\"" + executable + L"\" " + args;
        PROCESS_INFORMATION pi{};
        Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr, cwd.c_str(), &startup, &pi) != 0, "create child");
        process_.value = pi.hProcess; pid_ = pi.dwProcessId;
        Handle thread; thread.value = pi.hThread;
        if (!AssignProcessToJobObject(job_.value, process_.value)) {
            TerminateProcess(process_.value, 99);
            WaitForSingleObject(process_.value, 3000);
            Check(false, "assign child job");
        }
        Check(ResumeThread(thread.value) != static_cast<DWORD>(-1), "resume contained child");
    }
    ~Child() {
        if (process_.value && WaitForSingleObject(process_.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process_.value, 99);
            WaitForSingleObject(process_.value, 3000);
        }
    }
    DWORD Pid() const { return pid_; }
    void Drain() {
        // A noisy failed child cannot monopolize the test deadline.
        for (unsigned pass = 0; pass < 32; ++pass) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe_.value, nullptr, 0, nullptr, &available, nullptr)) {
                Check(GetLastError() == ERROR_BROKEN_PIPE, "peek child output"); return;
            }
            if (!available) return;
            char bytes[1024]; DWORD count = 0;
            Check(ReadFile(pipe_.value, bytes, std::min<DWORD>(available, sizeof bytes), &count, nullptr) != 0,
                "read child output");
            output_.append(bytes, count); Check(output_.size() <= 65536, "child output limit");
        }
    }
    void Ready() {
        const auto deadline = Now() + 10000;
        for (;;) {
            Drain();
            if (output_.find("LOOPBACK_READY") != std::string::npos) return;
            Check(Now() < deadline, "child ready timeout");
            Check(WaitForSingleObject(process_.value, 10) == WAIT_TIMEOUT, "child exited before ready");
        }
    }
    DWORD Exit(DWORD timeout) {
        const auto deadline = Now() + timeout;
        for (;;) {
            Drain();
            const auto result = WaitForSingleObject(process_.value, 10);
            if (result == WAIT_OBJECT_0) break;
            Check(result == WAIT_TIMEOUT && Now() < deadline, "child exit timeout");
        }
        Drain(); DWORD code = 99;
        Check(GetExitCodeProcess(process_.value, &code) != 0, "child exit status"); return code;
    }
    const std::string& Output() const { return output_; }
};

void Until(const std::function<void()>& pump, const std::function<bool()>& done,
           Socket& a, Socket& b, Socket& c, uint64_t duration) {
    const auto deadline = Now() + duration;
    WSAEVENT events[]{a.event, b.event, c.event};
    for (;;) {
        pump(); if (done()) return;
        Check(Now() < deadline, "client phase timeout");
        // Short timer wakeups also service retransmits if a datagram is lost.
        const auto result = WSAWaitForMultipleEvents(3, events, FALSE, 20, FALSE);
        Check(result == WSA_WAIT_TIMEOUT || (result >= WSA_WAIT_EVENT_0 && result < WSA_WAIT_EVENT_0 + 3),
            "wait client sockets");
        if (result != WSA_WAIT_TIMEOUT) {
            const auto index = result - WSA_WAIT_EVENT_0;
            if (index == 0) a.Reset(); else if (index == 1) b.Reset(); else c.Reset();
        }
    }
}

void Run(const std::wstring& relayExecutable, const std::wstring& productionExecutable) {
    Winsock winsock;
    TempDirectory temp;
    Socket reservation;
    const auto port = reservation.Port();
    const auto config = temp.path / "relay.conf";
    { std::ofstream out(config); out << "passphrase = loopback integration passphrase\nmax_players = 2\nport = "
        << port << "\n"; Check(out.good(), "write config"); }
    const auto quoteConfig = L"\"" + config.wstring() + L"\"";
    // Execute the real production preflight, which must work with the port occupied and reveal no secret.
    Child preflight(productionExecutable, L"--check-config " + quoteConfig, temp.path);
    Check(preflight.Exit(5000) == 0, "occupied-port production preflight");
    Check(preflight.Output().find("Configuration is valid.") != std::string::npos &&
        preflight.Output().find("UDP port: " + std::to_string(port)) != std::string::npos &&
        preflight.Output().find("Preflight only: no socket was opened") != std::string::npos &&
        preflight.Output().find("loopback integration passphrase") == std::string::npos, "preflight output");
    // Flag-shaped files must not turn an invalid preflight invocation into a launch.
    { std::ofstream out(temp.path / "--check-config"); out << "passphrase = flag shaped config\n"; }
    Child missingPath(productionExecutable, L"--check-config", temp.path);
    Check(missingPath.Exit(5000) == 2 && missingPath.Output().find("usage: sco-relay") != std::string::npos,
        "missing preflight path");
    Child badShape(productionExecutable, L"--check-config --stop-event Local\\sc-mp-invalid", temp.path);
    Check(badShape.Exit(5000) == 2 && badShape.Output().find("usage: sco-relay") != std::string::npos,
        "preflight stop-event argument shape");
    const auto invalid = temp.path / "invalid.conf";
    { std::ofstream out(invalid); out << "passphrase = PREFLIGHT-SECRET-SENTINEL\xC2\x9B\n"
        << "port = PREFLIGHT-SECRET-SENTINEL\xC2\x9B\n"; }
    Child invalidConfig(productionExecutable, L"--check-config \"" + invalid.wstring() + L"\"", temp.path);
    Check(invalidConfig.Exit(5000) == 1 && invalidConfig.Output().find("config check failed") != std::string::npos &&
        invalidConfig.Output().find("PREFLIGHT-SECRET-SENTINEL") == std::string::npos &&
        invalidConfig.Output().find("\xC2\x9B") == std::string::npos, "invalid preflight redaction");
    Check(std::distance(std::filesystem::directory_iterator(temp.path), std::filesystem::directory_iterator{}) == 3,
        "preflight created unexpected files");
    const std::wstring eventName = L"Local\\sc-mp-loopback-" + std::to_wstring(GetCurrentProcessId());
    Handle stop;
    stop.value = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    const DWORD eventError = GetLastError();
    Check(stop.value && eventError != ERROR_ALREADY_EXISTS, "unique stop event");
    const auto childArgs = quoteConfig + L" \"" + eventName + L"\"";
    {
        Child occupied(relayExecutable, childArgs, temp.path);
        Check(occupied.Exit(5000) == 1 && occupied.Output().find("loopback bind refused") != std::string::npos,
            "occupied-port loopback startup must fail");
    }
    reservation.Close();
    Child relay(relayExecutable, childArgs, temp.path); relay.Ready();
    CheckChildSockets(relay.Pid(), port, true);
    Socket a, b, bad;
    (void)a.Port(); (void)b.Port(); (void)bad.Port();
    bool downA = false, downB = false, wrong = false, badUp = false, gotA = false, gotB = false;
    sco::net::PeerId idA = 0, idB = 0;
    sco::net::Callbacks ca, cb, cc;
    ca.state = [&](bool active, std::string_view){ if (!active) downA = true; };
    cb.state = [&](bool active, std::string_view){ if (!active) downB = true; };
    cc.state = [&](bool active, std::string_view reason){
        if (active) badUp = true; else wrong = reason == "refused: wrong passphrase";
    };
    ca.message = [&](const sco::net::Message& message){
        if (message.channel != "loopback.probe") return;
        Check(message.from == idB && message.len == 3 && std::memcmp(message.data, "two", 3) == 0,
            "second-to-first routed payload"); gotA = true;
    };
    cb.message = [&](const sco::net::Message& message){
        if (message.channel != "loopback.probe") return;
        Check(message.from == idA && message.len == 3 && std::memcmp(message.data, "one", 3) == 0,
            "first-to-second routed payload"); gotB = true;
    };
    sco::net::Core first(a, ca), second(b, cb), refused(bad, cc);
    Check(first.RegisterChannel("loopback.probe", sco::net::kReliable, 32) &&
        second.RegisterChannel("loopback.probe", sco::net::kReliable, 32), "register channels");
    sco::net::Endpoint target{}; target.family = 4; target.addr[0] = 127; target.addr[3] = 1; target.port = port;
    sco::net::Options options; options.playerName = "refusal_probe"; options.passphrase = "wrong passphrase";
    Check(refused.Join(options, target, Now()), "begin wrong-password join");
    auto pump = [&]{ first.Pump(Now()); second.Pump(Now()); refused.Pump(Now()); };
    // Try the wrong password before filling the two seats, so a full-session refusal cannot pass.
    Until(pump, [&]{ return wrong; }, a, b, bad, 10000);
    Check(!badUp && !refused.Active() && refused.Peers().empty(), "wrong-password client admitted");
    options.passphrase = "loopback integration passphrase"; options.playerName = "loopback_one";
    Check(first.Join(options, target, Now()), "begin first join");
    options.playerName = "loopback_two"; Check(second.Join(options, target, Now()), "begin second join");
    Until(pump, [&]{ return first.Active() && second.Active() && first.Peers().size() == 3 &&
        second.Peers().size() == 3; }, a, b, bad, 10000);
    idA = first.Self(); idB = second.Self();
    Check(idA != idB && idA != sco::net::kHostPeer && idB != sco::net::kHostPeer, "distinct player identities");
    Check(first.Send("loopback.probe", "one", 3) == sco::net::SendResult::Ok &&
        second.Send("loopback.probe", "two", 3) == sco::net::SendResult::Ok, "send probes");
    Until(pump, [&]{ return gotA && gotB; }, a, b, bad, 5000);
    CheckChildSockets(relay.Pid(), port, true);
    Check(SetEvent(stop.value) != 0, "signal stop event");
    Check(relay.Exit(5000) == 0 && relay.Output().find("LOOPBACK_STOPPED") != std::string::npos,
        "clean event-driven child stop");
    Until(pump, [&]{ return downA && downB; }, a, b, bad, 5000);
    Check(!first.Active() && !second.Active(), "clients received clean session end");
    CheckChildSockets(relay.Pid(), port, false);
    // The process released its exclusive endpoint, not just its process handle.
    sockaddr_in released{}; released.sin_family = AF_INET;
    released.sin_addr.s_addr = htonl(INADDR_LOOPBACK); released.sin_port = htons(port);
    Check(bind(reservation.fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP),
        reinterpret_cast<sockaddr*>(&released), sizeof released) == 0, "rebind released relay port");
    std::puts("LOOPBACK_PROCESS_PASS preflight, exclusive port refusal, one 127.0.0.1 UDP endpoint, "
        "password refusal, two authenticated clients, bidirectional routing and event shutdown; not run in game");
}
}

int wmain(int argc, wchar_t** argv) {
    try { Check(argc == 3, "expected test relay and production preflight executable"); Run(argv[1], argv[2]); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "LOOPBACK_PROCESS_FAIL: %s\n", error.what()); return 1; }
}
