// SPDX-License-Identifier: GPL-3.0-only
// One test over real UDP sockets on the loopback address, in one process: a player joins the server
// through sco-core's own socket class, and the server listens on the UdpSocket of server/net_boundary.h,
// the one the program uses. The program itself is tested by test_program.sh.
#include "sco/net/core.h"
#include "sco/net/scope.h"
#include "sco/net/udp.h"
#include "server/config.h"
#include "server/log.h"
#include "server/relay_info.h"
#include "server/session.h"
#include "server/tests/check.h"
#include "server/tests/session_kit.h"
#include "plugins/multiplayer/multiplayer_wire.h"
#include "plugins/multiplayer/relay_wire.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

namespace wire = sco::relay::wire;
namespace protocol = sco::relay::wire::protocol;
namespace playerWire = mpwire;

struct CapturedMessage {
    std::string channel;
    sco::net::PeerId from = 0;
    std::vector<uint8_t> data;
};

struct CapturedPeerEvent {
    sco::net::PeerEvent what;
    sco::net::PeerId peer;
    std::string text;
};

struct LoopbackPlayer {
    std::unique_ptr<sco::net::udp::Socket> socket;
    std::unique_ptr<sco::net::Core> core;
    std::vector<CapturedMessage> messages;
    std::vector<CapturedPeerEvent> peerEvents;
    bool active = false;
    unsigned downEvents = 0;
    std::string downReason;
    uint64_t id;
    unsigned attempt = 0;

    explicit LoopbackPlayer(uint64_t playerId, std::string* why) : id(playerId) {
        socket = sco::net::udp::Socket::Open(0, true, why);
        if (!socket) return;
        sco::net::Callbacks callbacks;
        callbacks.message = [this](const sco::net::Message& message) {
            CapturedMessage received;
            received.channel = std::string(message.channel);
            received.from = message.from;
            if (message.len) received.data.assign(message.data, message.data + message.len);
            messages.push_back(std::move(received));
        };
        callbacks.peer = [this](sco::net::PeerEvent what, sco::net::PeerId peer, std::string_view text) {
            peerEvents.push_back({ what, peer, std::string(text) });
        };
        callbacks.state = [this](bool up, std::string_view reason) {
            active = up;
            if (!up) {
                ++downEvents;
                downReason.assign(reason);
            }
        };
        core = std::make_unique<sco::net::Core>(*socket, std::move(callbacks));
        const bool channels =
            core->RegisterChannel("relay.info", sco::net::kReliable | sco::net::kFromHost, sco::relay::kRelayInfoBytes) &&
            core->RegisterChannel(protocol::kChannel, sco::net::kReliable, protocol::kBytes) &&
            core->RegisterChannel(wire::kRelayChannel, sco::net::kReliable | sco::net::kFromHost,
                                  static_cast<uint32_t>(wire::kRelayMaxBytes)) &&
            core->RegisterChannel("multiplayer.pose", 0, static_cast<uint32_t>(playerWire::kMaxPose)) &&
            core->RegisterChannel("multiplayer.spawn", sco::net::kReliable, static_cast<uint32_t>(playerWire::kMaxSpawn)) &&
            core->RegisterChannel("multiplayer.despawn", sco::net::kReliable,
                                  static_cast<uint32_t>(playerWire::kDespawn));
        if (!channels) core.reset();
    }

    bool Ready() const { return socket != nullptr && core != nullptr; }

    bool Join(const sco::net::Endpoint& host, const char* name, uint64_t nowMs) {
        auto options = relaytest::JoinOptions(name, relaytest::kPassphrase,
                                              relaytest::kTestIters,
                                              id * 7919 + ++attempt * 104729);
        return core->Join(options, host, nowMs);
    }

    bool HasRole(sco::net::PeerId sender, uint8_t expectedRole) const {
        for (const auto& message : messages) {
            uint8_t role = 0;
            if (message.channel == protocol::kChannel && message.from == sender &&
                protocol::Decode(message.data.data(), message.data.size(), &role) && role == expectedRole)
                return true;
        }
        return false;
    }

    bool HasRoster(const std::vector<sco::net::PeerId>& expectedPlayers) const {
        for (const auto& message : messages) {
            if (message.channel != wire::kRelayChannel || message.from != sco::net::kHostPeer) continue;
            wire::RelayRoster roster;
            if (wire::DecodeRelay(message.data.data(), message.data.size(), &roster) &&
                roster.players.size() == expectedPlayers.size() &&
                std::none_of(roster.players.begin(), roster.players.end(), [](const wire::RelayEntry& player) {
                    return player.peer == sco::net::kHostPeer;
                })) {
                bool containsAll = true;
                for (sco::net::PeerId peer : expectedPlayers)
                    containsAll = containsAll && std::any_of(roster.players.begin(), roster.players.end(),
                                                              [peer](const wire::RelayEntry& player) {
                                                                  return player.peer == peer;
                                                              });
                if (containsAll) return true;
            }
        }
        return false;
    }

    bool HasMessage(const char* channel, sco::net::PeerId sender, const std::vector<uint8_t>& data) const {
        for (const auto& message : messages)
            if (message.channel == channel && message.from == sender && message.data == data) return true;
        return false;
    }

    bool SawPeerLeave(sco::net::PeerId peer, const char* reason) const {
        for (const auto& event : peerEvents)
            if (event.what == sco::net::PeerEvent::Left && event.peer == peer &&
                event.text.find(reason) != std::string::npos)
                return true;
        return false;
    }
};

template <class Done>
bool PumpLoopback(sco::relay::RelaySession& session, sco::relay::UdpSocket& serverSocket,
                  const std::vector<LoopbackPlayer*>& players, uint64_t timeoutMs, Done done) {
    const uint64_t deadline = NowMs() + timeoutMs;
    while (NowMs() < deadline) {
        const uint64_t now = NowMs();
        session.Pump(now);
        for (LoopbackPlayer* player : players)
            if (player->core) player->core->Pump(now);
        if (done()) return true;
        serverSocket.Wait(1);
    }
    return done();
}

void TestRealUdpSessionLifecycle() {
    std::string why;
    std::unique_ptr<sco::relay::UdpSocket> serverSocket = sco::relay::UdpSocket::Open(0, true, &why);
    CHECK(serverSocket != nullptr);
    if (!serverSocket) return;

    LoopbackPlayer alice(2, &why), bob(3, &why);
    CHECK(alice.Ready() && bob.Ready());
    if (!alice.Ready() || !bob.Ready()) return;

    sco::relay::Logger quiet(sco::relay::LogLevel::Error, [](sco::relay::LogLevel, const std::string&) {});
    sco::relay::RelaySession first(*serverSocket, relaytest::TestSettings(2), quiet, relaytest::kTestIters);
    const bool firstStarted = first.Start(NowMs(), &why);
    CHECK(firstStarted);
    if (!firstStarted) return;

    sco::net::Endpoint host;
    CHECK(sco::net::ParseAddress("127.0.0.1", serverSocket->Port(), &host));
    const std::vector<LoopbackPlayer*> players{ &alice, &bob };
    CHECK(alice.Join(host, "Alice", NowMs()));
    CHECK(PumpLoopback(first, *serverSocket, players, 5000, [&] {
        return alice.active && alice.HasRole(sco::net::kHostPeer, protocol::kRelay) && alice.HasRoster({2});
    }));
    CHECK(alice.core->Self() == 2 && first.PlayerCount() == 1);

    CHECK(bob.Join(host, "Bob", NowMs()));
    CHECK(PumpLoopback(first, *serverSocket, players, 5000, [&] {
        return bob.active && alice.HasRoster({2, 3}) && bob.HasRoster({2, 3});
    }));
    CHECK(bob.core->Self() == 3 && first.PlayerCount() == 2);
    CHECK(alice.HasRole(sco::net::kHostPeer, protocol::kRelay));

    const std::vector<uint8_t> playerRole{ protocol::kGeneration, protocol::kPlayer };
    CHECK(bob.core->Send(protocol::kChannel, playerRole.data(), static_cast<uint32_t>(playerRole.size())) ==
          sco::net::SendResult::Ok);
    const std::vector<uint8_t> pose{ protocol::kGeneration, 0xA5, 0x5A };
    CHECK(alice.core->Send("multiplayer.pose", pose.data(), static_cast<uint32_t>(pose.size())) ==
          sco::net::SendResult::Ok);
    CHECK(PumpLoopback(first, *serverSocket, players, 3000, [&] {
        return alice.HasMessage(protocol::kChannel, 3, playerRole) &&
               bob.HasMessage("multiplayer.pose", 2, pose);
    }));
    CHECK(alice.HasRole(3, protocol::kPlayer));
    CHECK(first.PlayerCount() == 2); // the forwarding host is not a player in the relay roster

    bob.core->Leave("user left");
    CHECK(PumpLoopback(first, *serverSocket, players, 3000, [&] {
        return first.PlayerCount() == 1 && alice.SawPeerLeave(3, "user left");
    }));
    CHECK(alice.HasRoster({2}));

    first.Stop("restart");
    CHECK(!first.Running() && first.PlayerCount() == 0);
    CHECK(PumpLoopback(first, *serverSocket, players, 3000, [&] { return alice.downEvents == 1; }));
    CHECK(alice.downReason == "the host ended the session: restart");

    // Recovery is an explicit fresh Join into a new session instance, not a restored world or auto-retry.
    sco::relay::RelaySession second(*serverSocket, relaytest::TestSettings(2), quiet, relaytest::kTestIters);
    const bool secondStarted = second.Start(NowMs(), &why);
    CHECK(secondStarted);
    if (!secondStarted) return;
    alice.messages.clear();
    CHECK(alice.Join(host, "Alice", NowMs()));
    CHECK(PumpLoopback(second, *serverSocket, players, 5000, [&] {
        return alice.active && alice.HasRole(sco::net::kHostPeer, protocol::kRelay) && alice.HasRoster({2});
    }));
    CHECK(second.PlayerCount() == 1);
    second.Stop("test over");
    CHECK(PumpLoopback(second, *serverSocket, players, 3000, [&] { return alice.downEvents == 2; }));
}

}  // namespace

namespace relaytest {

void RunUdpTests() { Run("TestRealUdpSessionLifecycle", TestRealUdpSessionLifecycle); }

}  // namespace relaytest
