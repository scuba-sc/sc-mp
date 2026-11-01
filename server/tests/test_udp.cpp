// SPDX-License-Identifier: GPL-3.0-only
// One test over real UDP sockets on the loopback address, in one process: a player joins the server
// through sco-core's own socket class, and the server listens on the UdpSocket of server/net_boundary.h,
// the one the program uses. The program itself is tested by test_program.sh. The chat test sends the plugin's
// multiplayer.chat messages between players through that same, unmodified relay.
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
#include <cstring>
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
    int seat = -1;                // nearby-only: the seat the latest roster gave this player
    uint8_t seatGeneration = 0;

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

    // multiplayer.chat as the plugin registers it. maxLen is the largest message this player takes or sends.
    bool RegisterChat(uint32_t maxLen) {
        return core->RegisterChannel(playerWire::kChatChannel, sco::net::kReliable, maxLen);
    }

    // Nearby-only: a player says where it is on multiplayer.near_up ...
    bool RegisterNearUp() {
        return core->RegisterChannel(wire::kNearUpChannel, sco::net::kToHost,
                                     static_cast<uint32_t>(wire::kNearUpMaxBytes));
    }

    bool SendNearUp(uint64_t area, uint8_t tag) {
        const std::vector<uint8_t> pose{ tag, 1, 2 };
        const std::vector<uint8_t> up = wire::EncodeNearUp(area, 0.0, 0.0, 0.0, pose.data(), pose.size());
        return core->Send(wire::kNearUpChannel, up.data(), static_cast<uint32_t>(up.size())) ==
               sco::net::SendResult::Ok;
    }

    // ... and hears the others on the channel of its seat, read from the newest roster that lists it.
    bool ListenOnSeat() {
        for (size_t i = messages.size(); i-- > 0;) {
            wire::RelayRoster roster;
            if (messages[i].channel != wire::kRelayChannel ||
                !wire::DecodeRelay(messages[i].data.data(), messages[i].data.size(), &roster))
                continue;
            for (const wire::RelayEntry& entry : roster.players) {
                if (entry.peer != core->Self()) continue;
                seat = entry.slot;
                seatGeneration = entry.generation;
                return core->RegisterChannel(wire::NearDownChannel(entry.slot), sco::net::kFromHost,
                                             static_cast<uint32_t>(wire::kNearDownMaxBytes));
            }
        }
        return false;
    }

    // A pose from `sender` that arrived on this player's seat channel, made for its generation; its first byte is `tag`.
    bool HasNearPoseFrom(sco::net::PeerId sender, uint8_t tag) const {
        if (seat < 0) return false;
        const std::string channel = wire::NearDownChannel(static_cast<uint32_t>(seat));
        for (const auto& message : messages) {
            wire::NearDown down;
            if (message.channel == channel &&
                wire::DecodeNearDown(message.data.data(), message.data.size(), seatGeneration, &down) ==
                    wire::NearDownVerdict::Accepted &&
                down.sender == sender && down.poseLen > 0 && down.pose[0] == tag)
                return true;
        }
        return false;
    }

    size_t Count(const char* channel) const {
        size_t count = 0;
        for (const auto& message : messages)
            if (message.channel == channel) ++count;
        return count;
    }

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

// ---- text chat through the relay ---------------------------------------------------------------------

constexpr uint64_t kArea = 0x1122334455667788ull;   // the one area the nearby-only players share

// A multiplayer.chat message as the plugin writes it.
std::vector<uint8_t> ChatMessage(const char* text) {
    uint8_t buffer[playerWire::kMaxChat];
    const size_t size = playerWire::EncodeChat(text, std::strlen(text), buffer, sizeof(buffer));
    return std::vector<uint8_t>(buffer, buffer + size);
}

// Three players on one relay: Alice and Bob have the chat channel, Carol is a player on a build without it. The
// relay is the program's own session, untouched: it does not know the channel, and the network library passes on what
// joiners register. With nearby-only on, poses go the other way (near_up and the seat channels) and chat must not care.
void ChatThroughRelay(bool nearbyOnly) {
    std::string why;
    std::unique_ptr<sco::relay::UdpSocket> serverSocket = sco::relay::UdpSocket::Open(0, true, &why);
    CHECK(serverSocket != nullptr);
    if (!serverSocket) return;

    LoopbackPlayer alice(2, &why), bob(3, &why), carol(4, &why);
    CHECK(alice.Ready() && bob.Ready() && carol.Ready());
    if (!alice.Ready() || !bob.Ready() || !carol.Ready()) return;
    // Alice may send up to 1024 bytes (more than the plugin ever does), so that a long line reaches the relay; Bob takes
    // what the plugin takes.
    CHECK(alice.RegisterChat(1024));
    CHECK(bob.RegisterChat(static_cast<uint32_t>(playerWire::kMaxChat)));
    if (nearbyOnly) {
        CHECK(alice.RegisterNearUp());
        CHECK(bob.RegisterNearUp());
        CHECK(carol.RegisterNearUp());
    }

    std::vector<std::string> relayLines;
    sco::relay::Logger relayLogger(sco::relay::LogLevel::Debug,
                                   [&relayLines](sco::relay::LogLevel, const std::string& line) { relayLines.push_back(line); });
    sco::relay::Settings settings = relaytest::TestSettings(3);
    settings.nearbyOnly = nearbyOnly;
    sco::relay::RelaySession session(*serverSocket, settings, relayLogger, relaytest::kTestIters);
    const bool started = session.Start(NowMs(), &why);
    CHECK(started);
    if (!started) return;

    sco::net::Endpoint host;
    CHECK(sco::net::ParseAddress("127.0.0.1", serverSocket->Port(), &host));
    const std::vector<LoopbackPlayer*> players{ &alice, &bob, &carol };
    const char* const names[] = { "Alice", "Bob", "Carol" };
    for (size_t i = 0; i < players.size(); ++i) {
        LoopbackPlayer* joining = players[i];
        CHECK(joining->Join(host, names[i], NowMs()));
        CHECK(PumpLoopback(session, *serverSocket, players, 5000, [joining] { return joining->active; }));
    }
    CHECK(alice.core->Self() == 2 && bob.core->Self() == 3 && carol.core->Self() == 4);
    CHECK(PumpLoopback(session, *serverSocket, players, 5000, [&] {
        return alice.HasRoster({ 2, 3, 4 }) && bob.HasRoster({ 2, 3, 4 }) && carol.HasRoster({ 2, 3, 4 });
    }));
    if (nearbyOnly) CHECK(alice.ListenOnSeat() && bob.ListenOnSeat() && carol.ListenOnSeat());

    // A line goes to the other player who has the channel, with its writer as the origin, not the relay.
    const char* const chat = playerWire::kChatChannel;
    const std::vector<uint8_t> toBob = ChatMessage("chat-marker-1 from Alice");
    const std::vector<uint8_t> toAlice = ChatMessage("chat-marker-2 from Bob");
    CHECK(!toBob.empty() && !toAlice.empty());
    CHECK(alice.core->Send(chat, toBob.data(), static_cast<uint32_t>(toBob.size())) == sco::net::SendResult::Ok);
    CHECK(bob.core->Send(chat, toAlice.data(), static_cast<uint32_t>(toAlice.size())) == sco::net::SendResult::Ok);
    CHECK(PumpLoopback(session, *serverSocket, players, 3000, [&] {
        return bob.HasMessage(chat, 2, toBob) && alice.HasMessage(chat, 3, toAlice);
    }));
    CHECK(alice.Count(chat) == 1 && bob.Count(chat) == 1);   // nobody gets their own line back

    // A line over what the receiver takes is dropped by the receiver, before it is read; the stream goes on.
    std::vector<uint8_t> tooLong{ playerWire::kChatVersion, 0, 200, 0 };   // a header that fits version 1, then 200 bytes
    tooLong.insert(tooLong.end(), 200, uint8_t('o'));
    const std::vector<uint8_t> after = ChatMessage("chat-marker-3 after the long one");
    const uint64_t refusedBefore = bob.core->GetStats().refused;
    CHECK(alice.core->Send(chat, tooLong.data(), static_cast<uint32_t>(tooLong.size())) == sco::net::SendResult::Ok);
    CHECK(alice.core->Send(chat, after.data(), static_cast<uint32_t>(after.size())) == sco::net::SendResult::Ok);
    CHECK(PumpLoopback(session, *serverSocket, players, 3000, [&] { return bob.HasMessage(chat, 2, after); }));
    CHECK(bob.core->GetStats().refused > refusedBefore);
    CHECK(bob.Count(chat) == 2);   // the first line and the one after the long one
    for (const CapturedMessage& message : bob.messages)
        if (message.channel == chat) CHECK(message.data.size() <= playerWire::kMaxChat);

    // Carol has no chat channel: she gets none of it, and the session carries her poses all the same.
    CHECK(carol.Count(chat) == 0);
    if (nearbyOnly) {
        // Everyone says where they are, then Alice's pose reaches the others on their seat channels.
        CHECK(alice.SendNearUp(kArea, 0xA1));
        CHECK(bob.SendNearUp(kArea, 0xB1));
        CHECK(carol.SendNearUp(kArea, 0xC1));
        PumpLoopback(session, *serverSocket, players, 300, [] { return false; });
        CHECK(alice.SendNearUp(kArea, 0xA2));
        CHECK(PumpLoopback(session, *serverSocket, players, 3000, [&] {
            return carol.HasNearPoseFrom(2, 0xA2) && bob.HasNearPoseFrom(2, 0xA2);
        }));
    } else {
        const std::vector<uint8_t> pose{ protocol::kGeneration, 0xA5, 0x5A };
        CHECK(alice.core->Send("multiplayer.pose", pose.data(), static_cast<uint32_t>(pose.size())) == sco::net::SendResult::Ok);
        CHECK(PumpLoopback(session, *serverSocket, players, 3000, [&] {
            return carol.HasMessage("multiplayer.pose", 2, pose) && bob.HasMessage("multiplayer.pose", 2, pose);
        }));
    }
    CHECK(carol.Count(chat) == 0);

    session.Stop("test over");
    CHECK(PumpLoopback(session, *serverSocket, players, 3000, [&] {
        return alice.downEvents >= 1 && bob.downEvents >= 1 && carol.downEvents >= 1;
    }));

    // The relay's log never holds what the players said.
    CHECK(std::any_of(relayLines.begin(), relayLines.end(),
                      [](const std::string& line) { return line.find("session is up") != std::string::npos; }));
    CHECK(std::none_of(relayLines.begin(), relayLines.end(), [](const std::string& line) {
        return line.find("chat-marker") != std::string::npos;
    }));
}

void TestRealUdpChatThroughRelay() {
    ChatThroughRelay(false);   // the default: every pose goes to every player
    ChatThroughRelay(true);    // nearby-only: poses go through the server's forwarding
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

void RunUdpTests() {
    Run("TestRealUdpSessionLifecycle", TestRealUdpSessionLifecycle);
    Run("TestRealUdpChatThroughRelay", TestRealUdpChatThroughRelay);
}

}  // namespace relaytest
