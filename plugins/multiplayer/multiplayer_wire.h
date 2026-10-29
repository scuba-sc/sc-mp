// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The external multiplayer plugin's messages on sco.net (sco-core's docs/net.md). Plain bytes,
// little-endian (every sc-offline peer is x64 Windows), written and read field by field: nothing
// here is a game object, a pointer or a zone id. A zone travels by its NAME ("OOC_Stanton_2b_Daymar")
// with a position local to it, as teleport's saved spots keep it, because zone ids are volatile
// streaming handles that mean something only inside one game process.
//
//   multiplayer.pose      unreliable, ~10 per second. Where the sender's player is:
//     u8  version (2; decode also accepts 1)   u8 flags (bit 0: aboard a ship)
//     u8  levels (1..kMaxLevels)   u8 shipFirst (0 without a ship, else 1..levels-1)
//     u32 seq (counts up per pose; older ones are dropped)
//     v2 only: u64 sender monotonic milliseconds, f32 velocity[3] in the innermost frame
//     levels x { u8 nameLen (1..kMaxZoneName), name, f64 pos[3], f64 rot[4] (x y z w) }
//       the player's position and rotation in each zone of their chain, innermost first
//     ship: levels - shipFirst x { f64 pos[3], f64 rot[4] }
//       the ship's pose in the zones from shipFirst outwards (zone shipFirst - 1 is the ship's own)
//   multiplayer.spawn     reliable. What one of the sender's things is:
//     u8 version (1)   u8 kind (1 avatar, 2 ship)   u8 classLen (1..kMaxClass)   u8 0
//     u64 sessionId ((sender peer id << 32) | kind)   class bytes ([A-Za-z0-9_-])
//   multiplayer.despawn   reliable. That thing is gone:
//     u8 version (1)   u8 kind   u16 0   u32 0   u64 sessionId
//   multiplayer.chat      reliable, to every other player (whichever peer hosts the session relays it). One line of text:
//     u8 version (1)   u8 0   u16 n (1..kMaxChatText)   n bytes of text; exactly 4 + n bytes in all, at most kMaxChat
//     The text is well-formed UTF-8 (shortest forms, no surrogates, nothing above U+10FFFF) with no control character:
//     none of C0 (U+0000..U+001F), DEL (U+007F) or C1 (U+0080..U+009F), and no bidirectional embedding, override or
//     isolate (U+202A..U+202E, U+2066..U+2069). "hi" is 01 00 02 00 68 69. Plain text, not encrypted, like every
//     sco.net message.
//
// Decode* check everything (sizes, counts, characters, finite numbers, unit rotations) and return
// false for anything else: the sender is another player's game, so nothing is trusted.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mpwire {

constexpr uint8_t  kVersion = 1;  // spawn/despawn
constexpr uint8_t  kPoseVersion = 2;
constexpr int      kMaxLevels = 6;
constexpr size_t   kMaxZoneName = 63;
constexpr size_t   kMaxClass = 63;
constexpr uint8_t  kAvatar = 1;
constexpr uint8_t  kShip = 2;
constexpr double   kMaxCoord = 1.0e13;   // metres; a system-level zone holds planets ~1e11-1e12 m out

constexpr size_t kPoseHeader = 8;
constexpr size_t kPoseV2Extra = sizeof(uint64_t) + 3 * sizeof(float);
constexpr size_t kPoseRecord = 7 * sizeof(double);
constexpr size_t kMaxPose = kPoseHeader + kMaxLevels * (1 + kMaxZoneName + kPoseRecord) + (kMaxLevels - 1) * kPoseRecord;
constexpr size_t kMaxPoseV2 = kMaxPose; // relay limit; oversized v2 combinations are rejected
constexpr size_t kSpawnHeader = 12;
constexpr size_t kMaxSpawn = kSpawnHeader + kMaxClass;
constexpr size_t kDespawn = 16;
constexpr const char* kChatChannel = "multiplayer.chat";
constexpr uint8_t kChatVersion = 1;
constexpr size_t  kChatHeader = 4;
constexpr size_t  kMaxChatText = 160;                      // bytes of text in one line
constexpr size_t  kMaxChat = kChatHeader + kMaxChatText;   // the whole message: the channel's max_len
// Chat limits. A window is 5 seconds; a player sends at most kChatSendsPerWindow lines in one, and reads at most
// kChatAcceptPerWindow from one sender (the rest are dropped and counted). The history shows the last kChatHistoryRows.
constexpr uint64_t kChatWindowMs = 5000;
constexpr unsigned kChatSendsPerWindow = 3;
constexpr unsigned kChatAcceptPerWindow = 6;
constexpr size_t   kChatHistoryRows = 32;
static_assert(kMaxPose <= 1200, "a pose must fit one unreliable sco.net message");

struct Level {
    char   name[kMaxZoneName + 1];
    double pos[3];
    double rot[4];
};

struct Pose {
    uint32_t seq;
    uint8_t  version;
    uint64_t senderMs;
    float velocity[3];
    uint8_t  levels;
    bool     ship;
    uint8_t  shipFirst;
    Level    lv[kMaxLevels];
    double   shipPos[kMaxLevels][3];   // valid for shipFirst <= i < levels
    double   shipRot[kMaxLevels][4];
};

struct Spawn {
    uint8_t  kind;
    uint64_t sessionId;
    char     cls[kMaxClass + 1];
};

struct ChatLine {
    size_t length;                  // bytes of text
    char   text[kMaxChatText + 1];  // the text, NUL-terminated
};

inline uint64_t SessionId(uint64_t peer, uint8_t kind) { return (peer << 32) | kind; }

// ---- checks -------------------------------------------------------------------------------------

inline bool ZoneNameOk(const char* s, size_t n) {
    if (n < 1 || n > kMaxZoneName) return false;
    for (size_t i = 0; i < n; ++i)
        if (s[i] < 0x21 || s[i] > 0x7E) return false;
    return true;
}

inline bool ClassOk(const char* s, size_t n) {
    if (n < 1 || n > kMaxClass) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

// Text another player may put in front of this one: well-formed UTF-8 (only the shortest form of each character,
// no surrogate, nothing above U+10FFFF) with no control character. Refused: C0 (U+0000..U+001F, so no tab or line
// end), DEL, C1 (U+0080..U+009F) and the bidirectional embedding, override and isolate controls (U+202A..U+202E,
// U+2066..U+2069), which can make text read in another order than it was written. n may be 0 here; a chat line may not.
inline bool ChatTextOk(const char* s, size_t n) {
    const auto* p = reinterpret_cast<const uint8_t*>(s);
    for (size_t i = 0; i < n;) {
        const uint8_t lead = p[i];
        uint32_t cp = 0;
        size_t more = 0;   // continuation bytes that follow the lead byte
        if (lead < 0x80) { cp = lead; more = 0; }
        else if (lead >= 0xC2 && lead <= 0xDF) { cp = lead & 0x1Fu; more = 1; }
        else if (lead >= 0xE0 && lead <= 0xEF) { cp = lead & 0x0Fu; more = 2; }
        else if (lead >= 0xF0 && lead <= 0xF4) { cp = lead & 0x07u; more = 3; }
        else return false;   // a continuation byte alone, C0 and C1 (overlong forms), F5 and above
        if (more > n - i - 1) return false;   // the character is cut off
        for (size_t k = 1; k <= more; ++k) {
            const uint8_t next = p[i + k];
            if ((next & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (next & 0x3Fu);
        }
        if (more == 2 && cp < 0x800) return false;                   // overlong
        if (more == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return false;   // overlong, or beyond Unicode
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;             // a surrogate
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return false;  // C0, DEL, C1
        if ((cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069)) return false;
        i += more + 1;
    }
    return true;
}

// A position in range and a rotation near unit length (normalized in place).
inline bool PoseOk(double pos[3], double rot[4]) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(pos[i]) || std::fabs(pos[i]) > kMaxCoord) return false;
    double n = 0;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(rot[i])) return false;
        n += rot[i] * rot[i];
    }
    n = std::sqrt(n);
    if (!(n > 0.5 && n < 1.5)) return false;
    for (int i = 0; i < 4; ++i) rot[i] /= n;
    return true;
}

// ---- writing ------------------------------------------------------------------------------------

struct Writer {
    uint8_t* p;
    size_t   cap, n = 0;
    bool     ok = true;
    Writer(uint8_t* buf, size_t size) : p(buf), cap(size) {}
    void Bytes(const void* d, size_t len) {
        if (!ok || len > cap - n) { ok = false; return; }
        memcpy(p + n, d, len);
        n += len;
    }
    void U8(uint8_t v) { Bytes(&v, 1); }
    void U16(uint16_t v) { Bytes(&v, 2); }
    void U32(uint32_t v) { Bytes(&v, 4); }
    void U64(uint64_t v) { Bytes(&v, 8); }
    void F64s(const double* v, int count) { Bytes(v, count * sizeof(double)); }
};

// The pose into buf (kMaxPose bytes); its length, or 0 when it doesn't encode.
inline size_t EncodePose(const Pose& p, uint8_t* buf, size_t cap) {
    if (p.levels < 1 || p.levels > kMaxLevels) return 0;
    if (p.ship && (p.shipFirst < 1 || p.shipFirst >= p.levels)) return 0;
    for (float v : p.velocity) if (!std::isfinite(v)) return 0;
    Writer w(buf, cap < kMaxPoseV2 ? cap : kMaxPoseV2);
    w.U8(kPoseVersion);
    w.U8(p.ship ? 1 : 0);
    w.U8(p.levels);
    w.U8(p.ship ? p.shipFirst : 0);
    w.U32(p.seq);
    w.U64(p.senderMs);
    w.Bytes(p.velocity, sizeof(p.velocity));
    for (int i = 0; i < p.levels; ++i) {
        const size_t len = strnlen(p.lv[i].name, kMaxZoneName + 1);
        if (!ZoneNameOk(p.lv[i].name, len)) return 0;
        w.U8(static_cast<uint8_t>(len));
        w.Bytes(p.lv[i].name, len);
        w.F64s(p.lv[i].pos, 3);
        w.F64s(p.lv[i].rot, 4);
    }
    if (p.ship)
        for (int i = p.shipFirst; i < p.levels; ++i) {
            w.F64s(p.shipPos[i], 3);
            w.F64s(p.shipRot[i], 4);
        }
    return w.ok ? w.n : 0;
}

inline size_t EncodeSpawn(const Spawn& s, uint8_t* buf, size_t cap) {
    const size_t len = strnlen(s.cls, kMaxClass + 1);
    if ((s.kind != kAvatar && s.kind != kShip) || !ClassOk(s.cls, len)) return 0;
    Writer w(buf, cap);
    w.U8(kVersion);
    w.U8(s.kind);
    w.U8(static_cast<uint8_t>(len));
    w.U8(0);
    w.U64(s.sessionId);
    w.Bytes(s.cls, len);
    return w.ok ? w.n : 0;
}

inline size_t EncodeDespawn(uint8_t kind, uint64_t sessionId, uint8_t* buf, size_t cap) {
    Writer w(buf, cap);
    w.U8(kVersion);
    w.U8(kind);
    w.U16(0);
    w.U32(0);
    w.U64(sessionId);
    return w.ok ? w.n : 0;
}

// The multiplayer.chat message for `text` (n bytes, no terminator) into buf; its length, or 0 when the text is empty,
// longer than kMaxChatText bytes or not allowed (ChatTextOk), or buf is too small.
inline size_t EncodeChat(const char* text, size_t n, uint8_t* buf, size_t cap) {
    if (!text || n < 1 || n > kMaxChatText || !ChatTextOk(text, n)) return 0;
    Writer w(buf, cap);
    w.U8(kChatVersion);
    w.U8(0);
    w.U16(static_cast<uint16_t>(n));
    w.Bytes(text, n);
    return w.ok ? w.n : 0;
}

// ---- reading ------------------------------------------------------------------------------------

struct Reader {
    const uint8_t* p;
    size_t         len, n = 0;
    bool           ok = true;
    Reader(const void* buf, size_t size) : p(static_cast<const uint8_t*>(buf)), len(size) {}
    void Bytes(void* d, size_t count) {
        if (!ok || count > len - n) { ok = false; return; }
        memcpy(d, p + n, count);
        n += count;
    }
    uint8_t U8() { uint8_t v = 0; Bytes(&v, 1); return v; }
    uint16_t U16() { uint16_t v = 0; Bytes(&v, 2); return v; }
    uint32_t U32() { uint32_t v = 0; Bytes(&v, 4); return v; }
    uint64_t U64() { uint64_t v = 0; Bytes(&v, 8); return v; }
    void F64s(double* v, int count) { Bytes(v, count * sizeof(double)); }
};

inline bool DecodePose(const void* buf, size_t len, Pose& out) {
    if (!buf || len < kPoseHeader || len > kMaxPose) return false;
    Reader r(buf, len);
    out = Pose{};
    out.version = r.U8();
    if (out.version != kVersion && out.version != kPoseVersion) return false;
    const uint8_t flags = r.U8();
    if (flags & ~1u) return false;
    out.ship = (flags & 1) != 0;
    out.levels = r.U8();
    out.shipFirst = r.U8();
    out.seq = r.U32();
    if (out.version == kPoseVersion) {
        out.senderMs = r.U64();
        r.Bytes(out.velocity, sizeof(out.velocity));
        if (!r.ok) return false;
        for (float v : out.velocity) if (!std::isfinite(v)) return false;
    }
    if (out.levels < 1 || out.levels > kMaxLevels) return false;
    if (out.ship ? (out.shipFirst < 1 || out.shipFirst >= out.levels) : out.shipFirst != 0) return false;
    for (int i = 0; i < out.levels; ++i) {
        Level& l = out.lv[i];
        const uint8_t n = r.U8();
        if (!r.ok || n < 1 || n > kMaxZoneName) return false;
        r.Bytes(l.name, n);
        l.name[n] = 0;
        r.F64s(l.pos, 3);
        r.F64s(l.rot, 4);
        if (!r.ok || !ZoneNameOk(l.name, n) || !PoseOk(l.pos, l.rot)) return false;
    }
    if (out.ship)
        for (int i = out.shipFirst; i < out.levels; ++i) {
            r.F64s(out.shipPos[i], 3);
            r.F64s(out.shipRot[i], 4);
            if (!r.ok || !PoseOk(out.shipPos[i], out.shipRot[i])) return false;
        }
    return r.ok && r.n == len;
}

// sender is the peer the message came from: a session id must be its own.
inline bool DecodeSpawn(const void* buf, size_t len, uint64_t sender, Spawn& out) {
    if (!buf || len < kSpawnHeader + 1 || len > kMaxSpawn) return false;
    Reader r(buf, len);
    out = Spawn{};
    if (r.U8() != kVersion) return false;
    out.kind = r.U8();
    const uint8_t n = r.U8();
    if (r.U8() != 0) return false;
    out.sessionId = r.U64();
    if ((out.kind != kAvatar && out.kind != kShip) || out.sessionId != SessionId(sender, out.kind)) return false;
    if (n < 1 || n > kMaxClass || len != kSpawnHeader + n) return false;
    r.Bytes(out.cls, n);
    out.cls[n] = 0;
    return r.ok && ClassOk(out.cls, n);
}

inline bool DecodeDespawn(const void* buf, size_t len, uint64_t sender, uint8_t& kind) {
    if (!buf || len != kDespawn) return false;
    Reader r(buf, len);
    if (r.U8() != kVersion) return false;
    kind = r.U8();
    if (r.U16() != 0 || r.U32() != 0) return false;
    const uint64_t id = r.U64();
    return r.ok && (kind == kAvatar || kind == kShip) && id == SessionId(sender, kind);
}

// The line of text in a multiplayer.chat message. The layout, the length against the bytes that follow (a trailing
// byte is refused) and the text itself (ChatTextOk) are all checked; a message that fails any of it is not read at all.
inline bool DecodeChat(const void* buf, size_t len, ChatLine& out) {
    if (!buf || len < kChatHeader + 1 || len > kMaxChat) return false;
    Reader r(buf, len);
    out = ChatLine{};
    if (r.U8() != kChatVersion) return false;
    if (r.U8() != 0) return false;
    const size_t n = r.U16();
    if (n < 1 || n > kMaxChatText || len != kChatHeader + n) return false;
    r.Bytes(out.text, n);
    out.text[n] = 0;
    out.length = n;
    if (!r.ok || !ChatTextOk(out.text, n)) { out = ChatLine{}; return false; }
    return true;
}

// ---- rates --------------------------------------------------------------------------------------

// At most N events in any kChatWindowMs. The caller supplies the clock (milliseconds, never read here), so a test can
// step time by hand. An event is forgotten once a whole window has passed since it.
template <unsigned N>
class RateWindow {
    static_assert(N > 0, "a window holds at least one event");
public:
    // Is there room for one more event at nowMs?
    bool Room(uint64_t nowMs) {
        Expire(nowMs);
        return count_ < N;
    }
    // Records an event at nowMs; nothing happens when the window is full.
    void Add(uint64_t nowMs) {
        Expire(nowMs);
        if (count_ >= N) return;
        stamp_[(first_ + count_) % N] = nowMs;
        ++count_;
    }
    // Room, then Add: false (and nothing recorded) when the window is full.
    bool Take(uint64_t nowMs) {
        if (!Room(nowMs)) return false;
        Add(nowMs);
        return true;
    }
private:
    void Expire(uint64_t nowMs) {
        while (count_ && nowMs >= stamp_[first_] && nowMs - stamp_[first_] >= kChatWindowMs) {
            first_ = (first_ + 1) % N;
            --count_;
        }
    }
    uint64_t stamp_[N] = {};
    unsigned first_ = 0, count_ = 0;
};

}  // namespace mpwire
