#include "net/wire.h"

#include <cstring>

#include "sim/realm.h"

namespace mu::net {

namespace {

// Little-endian writers and a reader that refuses to run past its end.
struct Out {
    std::vector<uint8_t>& bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) bytes.push_back(uint8_t(v >> (8 * i)));
    }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void str(const std::string& s) {
        const size_t n = s.size() < 255 ? s.size() : 255;
        u8(uint8_t(n));
        bytes.insert(bytes.end(), s.begin(), s.begin() + long(n));
    }
};

struct In {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n) {
        if (at + n > bytes.size()) ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? bytes[at++] : 0; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(bytes[at++]) << (8 * i);
        return v;
    }
    uint64_t u64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(bytes[at++]) << (8 * i);
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    int64_t i64() { return int64_t(u64()); }
    std::string str() {
        const size_t n = u8();
        if (!need(n)) return {};
        std::string s(bytes.begin() + long(at), bytes.begin() + long(at + n));
        at += n;
        return s;
    }
    bool done() const { return ok && at == bytes.size(); }
};

// A frame: the length after it, the kind, the body.
template <typename Write>
void frame(std::vector<uint8_t>& out, Kind kind, Write write) {
    const size_t start = out.size();
    Out o{out};
    o.u32(0);
    o.u8(uint8_t(kind));
    write(o);
    const uint32_t length = uint32_t(out.size() - start - 4);
    for (int i = 0; i < 4; ++i) out[start + size_t(i)] = uint8_t(length >> (8 * i));
}

void putCommand(Out& o, const sim::Command& c) {
    o.u8(uint8_t(c.kind));
    o.u32(c.player);
    o.u32(c.ticket);
    o.i32(c.a);
    o.i32(c.b);
    o.i32(c.c);
    o.i32(c.d);
    o.u32(c.target);
    o.i64(c.zen);
    o.u8(uint8_t(c.service));
}

sim::Command takeCommand(In& in) {
    sim::Command c;
    const uint8_t kind = in.u8();
    // An unknown kind is the realm's None and does nothing; it is never trusted further.
    c.kind = kind <= uint8_t(sim::Command::Kind::ClaimCastle) ? sim::Command::Kind(kind)
                                                               : sim::Command::Kind::None;
    c.player = in.u32();
    c.ticket = in.u32();
    c.a = in.i32();
    c.b = in.i32();
    c.c = in.i32();
    c.d = in.i32();
    c.target = in.u32();
    c.zen = in.i64();
    c.service = sim::Service(in.u8());
    return c;
}

void putConfig(Out& o, const sim::RealmConfig& c) {
    o.i32(c.castle.period);
    o.i32(c.castle.opensAt);
    o.i32(c.castle.entry);
    o.u8(c.questDemo ? 1 : 0);
}

sim::RealmConfig takeConfig(In& in) {
    sim::RealmConfig c;
    c.castle.period = in.i32();
    c.castle.opensAt = in.i32();
    c.castle.entry = in.i32();
    c.questDemo = in.u8() != 0;
    // A period of nought would divide by it.
    if (c.castle.period <= 0) c.castle = sim::RealmConfig{}.castle;
    return c;
}

}  // namespace

void put(std::vector<uint8_t>& out, const Hello& one) {
    frame(out, Kind::Hello, [&](Out& o) {
        o.u32(one.version);
        o.str(one.world);
        o.u8(one.kin);
        o.i32(one.level);
        o.i32(one.column);
        o.i32(one.row);
        o.str(one.weapon);
        o.str(one.shield);
    });
}

void put(std::vector<uint8_t>& out, const Welcome& one) {
    frame(out, Kind::Welcome, [&](Out& o) {
        o.u32(one.version);
        o.u64(one.seed);
        o.str(one.world);
        o.u8(one.kin);
        o.i32(one.level);
        o.i32(one.column);
        o.i32(one.row);
        o.str(one.weapon);
        o.str(one.shield);
        putConfig(o, one.config);
    });
}

void put(std::vector<uint8_t>& out, const sim::Command& one) {
    frame(out, Kind::Command, [&](Out& o) { putCommand(o, one); });
}

void put(std::vector<uint8_t>& out, const Tick& one) {
    frame(out, Kind::Tick, [&](Out& o) {
        o.u32(one.tick);
        o.i64(one.wallClock);
        o.u8(one.rain ? 1 : 0);
        o.u32(uint32_t(one.commands.size()));
        for (const sim::Command& c : one.commands) putCommand(o, c);
    });
}

void put(std::vector<uint8_t>& out, const Hash& one) {
    frame(out, Kind::Hash, [&](Out& o) {
        o.u32(one.tick);
        o.u64(one.hash);
    });
}

int take(std::vector<uint8_t>& buffer, Kind& kind, std::vector<uint8_t>& body) {
    if (buffer.size() < 5) return 0;
    uint32_t length = 0;
    for (int i = 0; i < 4; ++i) length |= uint32_t(buffer[size_t(i)]) << (8 * i);
    if (length < 1 || length > kMostFrame) return -1;
    if (buffer.size() < 4 + size_t(length)) return 0;
    const uint8_t k = buffer[4];
    if (k < uint8_t(Kind::Hello) || k > uint8_t(Kind::Hash)) return -1;
    kind = Kind(k);
    body.assign(buffer.begin() + 5, buffer.begin() + 4 + long(length));
    buffer.erase(buffer.begin(), buffer.begin() + 4 + long(length));
    return 1;
}

bool parse(const std::vector<uint8_t>& body, Hello& out) {
    In in{body};
    out.version = in.u32();
    out.world = in.str();
    out.kin = in.u8();
    out.level = in.i32();
    out.column = in.i32();
    out.row = in.i32();
    out.weapon = in.str();
    out.shield = in.str();
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Welcome& out) {
    In in{body};
    out.version = in.u32();
    out.seed = in.u64();
    out.world = in.str();
    out.kin = in.u8();
    out.level = in.i32();
    out.column = in.i32();
    out.row = in.i32();
    out.weapon = in.str();
    out.shield = in.str();
    out.config = takeConfig(in);
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, sim::Command& out) {
    In in{body};
    out = takeCommand(in);
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Tick& out) {
    In in{body};
    out.tick = in.u32();
    out.wallClock = in.i64();
    out.rain = in.u8() != 0;
    const uint32_t count = in.u32();
    // Each command is 35 bytes: a count past what the body holds is a lie, refused before any
    // memory is reserved for it.
    if (!in.ok || size_t(count) * 35 > body.size()) return false;
    out.commands.clear();
    out.commands.reserve(count);
    for (uint32_t i = 0; i < count && in.ok; ++i) out.commands.push_back(takeCommand(in));
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Hash& out) {
    In in{body};
    out.tick = in.u32();
    out.hash = in.u64();
    return in.done();
}

uint64_t stateHash(const sim::Realm& realm) {
    // FNV-1a, 64 bits.
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&](const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    const std::vector<sim::Happening>& said = realm.happenings();
    if (!said.empty()) mix(said.data(), said.size() * sizeof(sim::Happening));
    const uint64_t draws = realm.draws();
    mix(&draws, sizeof draws);
    if (!realm.bodies().empty()) {
        const sim::Body& hero = realm.hero();
        mix(&hero.x, sizeof hero.x);
        mix(&hero.y, sizeof hero.y);
        mix(&hero.health, sizeof hero.health);
    }
    return h;
}

}  // namespace mu::net
