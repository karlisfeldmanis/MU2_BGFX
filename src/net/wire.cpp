#include "net/wire.h"

#include <algorithm>
#include <cstring>

#include <zlib.h>

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
    c.kind = kind <= uint8_t(sim::Command::Kind::Leave) ? sim::Command::Kind(kind)
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

uint32_t bitsOf(float f) {
    uint32_t v = 0;
    std::memcpy(&v, &f, sizeof v);
    return v;
}
float floatOf(uint32_t v) {
    float f = 0.0f;
    std::memcpy(&f, &v, sizeof f);
    return f;
}

void putHeld(Out& o, const sim::Held& h) {
    o.i32(h.item);
    o.u32(uint32_t(uint16_t(h.refinement)) | uint32_t(uint16_t(h.durability)) << 16);
    o.u8(uint8_t((h.skill ? 1 : 0) | (h.luck ? 2 : 0)));
    o.u8(uint8_t(h.option));
    o.u8(h.excellent);
    o.u8(h.sockets);
    for (uint8_t p : h.powers) o.u8(p);
    for (uint8_t a : h.affixes) o.u8(a);
    o.u8(h.wing);
}

sim::Held takeHeld(In& in) {
    sim::Held h;
    h.item = in.i32();
    const uint32_t both = in.u32();
    h.refinement = int16_t(uint16_t(both));
    h.durability = int16_t(uint16_t(both >> 16));
    const uint8_t flags = in.u8();
    h.skill = (flags & 1) != 0;
    h.luck = (flags & 2) != 0;
    h.option = int8_t(in.u8());
    h.excellent = in.u8();
    h.sockets = in.u8();
    for (uint8_t& p : h.powers) p = in.u8();
    for (uint8_t& a : h.affixes) a = in.u8();
    h.wing = in.u8();
    return h;
}

void putKept(Out& o, const sim::Kept& k) {
    const sim::HeroRecord& r = k.hero;
    o.u8(uint8_t(r.kin));
    o.i32(r.column);
    o.i32(r.row);
    o.u32(bitsOf(r.facing));
    o.i32(r.level);
    o.u64(r.experience);
    o.i32(r.pointsInHand);
    o.i32(r.points.strength);
    o.i32(r.points.agility);
    o.i32(r.points.vitality);
    o.i32(r.points.energy);
    o.i32(r.health);
    o.i32(r.mana);
    o.i64(r.money);
    o.u64(r.learned);
    for (int64_t left : r.coolsLeft) o.i64(left);
    o.i32(r.boonSkill);
    o.u32(bitsOf(r.boonDamageTaken));
    o.i64(r.boonTicksLeft);
    o.i64(r.aleTicksLeft);
    o.i32(r.might);
    o.i64(r.mightTicksLeft);
    for (const sim::Held& h : r.slots) putHeld(o, h);
    for (const sim::QuestProgress& q : r.quests) {
        o.u8(uint8_t(q.state));
        for (uint16_t c : q.counts) o.u32(c);
        o.i64(q.availableAt);
        o.u32(q.completions);
    }
    o.u32(r.found);
    o.i32(r.summonSkill);
    o.i32(r.summonHealth);
    for (int cell = 0; cell < sim::kVaultCells; ++cell) putHeld(o, k.vault[cell]);
    o.i64(k.vault.zen());
    for (int cell = 0; cell < sim::kMachineCells; ++cell) putHeld(o, k.machine[cell]);
    const sim::WayBack& way = r.wayBack;
    o.i32(way.map);
    o.i32(way.column);
    o.i32(way.row);
    o.u32(bitsOf(way.facing));
    o.i64(way.ticksLeft);
    o.i64(way.closedTicks);
}

sim::Kept takeKept(In& in, int layout = kKeptLayout) {
    sim::Kept k;
    sim::HeroRecord& r = k.hero;
    r.kin = sim::Kin(std::min<int>(in.u8(), int(sim::Kin::MagicGladiator)));
    r.column = in.i32();
    r.row = in.i32();
    r.facing = floatOf(in.u32());
    r.level = in.i32();
    r.experience = in.u64();
    r.pointsInHand = in.i32();
    r.points.strength = in.i32();
    r.points.agility = in.i32();
    r.points.vitality = in.i32();
    r.points.energy = in.i32();
    r.health = in.i32();
    r.mana = in.i32();
    r.money = in.i64();
    r.learned = in.u64();
    for (int64_t& left : r.coolsLeft) left = in.i64();
    r.boonSkill = in.i32();
    r.boonDamageTaken = floatOf(in.u32());
    r.boonTicksLeft = in.i64();
    r.aleTicksLeft = in.i64();
    r.might = in.i32();
    r.mightTicksLeft = in.i64();
    for (sim::Held& h : r.slots) h = takeHeld(in);
    // Layouts 3 and 4 carry 24 quests; the rest stand untaken.
    const int quests = layout >= 5 ? sim::kQuests : std::min(24, sim::kQuests);
    for (int i = 0; i < quests; ++i) {
        sim::QuestProgress& q = r.quests[size_t(i)];
        q.state = sim::QuestState(in.u8());
        for (uint16_t& c : q.counts) c = uint16_t(in.u32());
        q.availableAt = in.i64();
        q.completions = in.u32();
    }
    r.found = in.u32();
    r.summonSkill = in.i32();
    r.summonHealth = in.i32();
    k.vault.clear();
    for (int cell = 0; cell < sim::kVaultCells; ++cell) {
        const sim::Held h = takeHeld(in);
        if (!h.empty()) k.vault.put(cell, h);
    }
    k.vault.setZen(in.i64());
    k.machine.clear();
    for (int cell = 0; cell < sim::kMachineCells; ++cell) {
        const sim::Held h = takeHeld(in);
        if (!h.empty()) k.machine.put(cell, h);
    }
    if (layout >= 4) {
        sim::WayBack& way = r.wayBack;
        way.map = in.i32();
        way.column = in.i32();
        way.row = in.i32();
        way.facing = floatOf(in.u32());
        way.ticksLeft = in.i64();
        way.closedTicks = in.i64();
    }
    return k;
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
        o.u64(one.token);
        o.str(one.account);
        o.str(one.name);
    });
}

void put(std::vector<uint8_t>& out, const Who& one) {
    frame(out, Kind::Who, [&](Out& o) {
        o.u32(uint32_t(one.players.size()));
        for (const Who::One& p : one.players) {
            o.u32(p.id);
            o.str(p.name);
            o.u8(p.bot ? 1 : 0);
        }
    });
}

void put(std::vector<uint8_t>& out, const Account& one) {
    frame(out, Kind::Account, [&](Out& o) {
        o.u32(one.version);
        o.str(one.key);
        o.u32(uint32_t(one.claims.size()));
        for (const Claim& c : one.claims) {
            o.u64(c.token);
            o.str(c.name);
            o.i32(c.slot);
        }
    });
}

void put(std::vector<uint8_t>& out, const Roster& one) {
    frame(out, Kind::Roster, [&](Out& o) {
        o.u8(uint8_t(one.refused));
        o.u32(uint32_t(one.seats.size()));
        for (const Seat& s : one.seats) {
            o.u64(s.token);
            o.i32(s.slot);
            o.str(s.name);
            o.u8(s.kin);
            o.i32(s.level);
            o.u8(s.second ? 1 : 0);
            o.str(s.world);
            o.u8(uint8_t(s.worn.size()));
            for (const Seat::Worn& w : s.worn) {
                o.u8(w.slot);
                putHeld(o, w.held);
            }
        }
    });
}

void put(std::vector<uint8_t>& out, const Create& one) {
    frame(out, Kind::Create, [&](Out& o) {
        o.str(one.name);
        o.u8(one.kin);
    });
}

void put(std::vector<uint8_t>& out, const Delete& one) {
    frame(out, Kind::Delete, [&](Out& o) { o.u64(one.token); });
}

void put(std::vector<uint8_t>& out, const Elsewhere& one) {
    frame(out, Kind::Elsewhere, [&](Out& o) {
        o.str(one.world);
        o.i32(one.column);
        o.i32(one.row);
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
        o.u32(one.you);
        o.u32(one.backlog);
        o.u64(one.token);
        o.u8(one.kept ? 1 : 0);
        if (one.kept) putKept(o, one.first);
        o.i32(one.castle);
        o.u8(one.copy);
        o.u64(one.udpKey);
        o.u32(uint32_t(one.snapshot.size()));
        o.bytes.insert(o.bytes.end(), one.snapshot.begin(), one.snapshot.end());
    });
}

void put(std::vector<uint8_t>& out, const sim::Command& one) {
    frame(out, Kind::Command, [&](Out& o) { putCommand(o, one); });
}

void put(std::vector<uint8_t>& out, const Tick& one) {
    frame(out, Kind::Tick, [&](Out& o) {
        o.u32(one.tick);
        o.i64(one.wallClock);
        o.u8(uint8_t((one.rain ? 1 : 0) | (one.invasionElsewhere ? 2 : 0)));
        o.u32(uint32_t(one.commands.size()));
        for (const sim::Command& c : one.commands) putCommand(o, c);
        o.u32(uint32_t(one.arrivals.size()));
        for (const Arrival& a : one.arrivals) {
            o.u32(a.ticket);
            putKept(o, a.kept);
        }
        o.u8(one.early ? 1 : 0);
    });
}

void put(std::vector<uint8_t>& out, const Ping& one) {
    frame(out, Kind::Ping, [&](Out& o) { o.u32(one.nonce); });
}

void put(std::vector<uint8_t>& out, const Hash& one) {
    frame(out, Kind::Hash, [&](Out& o) {
        o.u32(one.tick);
        o.u64(one.hash);
    });
}

int take(std::vector<uint8_t>& buffer, Kind& kind, std::vector<uint8_t>& body, uint32_t most) {
    if (buffer.size() < 5) return 0;
    uint32_t length = 0;
    for (int i = 0; i < 4; ++i) length |= uint32_t(buffer[size_t(i)]) << (8 * i);
    if (length < 1 || length > most) return -1;
    if (buffer.size() < 4 + size_t(length)) return 0;
    const uint8_t k = buffer[4];
    if (k < uint8_t(Kind::Hello) || k > uint8_t(Kind::Who)) return -1;
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
    out.token = in.u64();
    out.account = in.str();
    out.name = in.str();
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Who& out) {
    In in{body};
    const uint32_t n = in.u32();
    // A world of a few hundred players at the most; a count past that is not one.
    if (n > 4096) return false;
    out.players.clear();
    for (uint32_t i = 0; i < n && in.ok; ++i) {
        Who::One p;
        p.id = in.u32();
        p.name = in.str();
        p.bot = in.u8() != 0;
        out.players.push_back(std::move(p));
    }
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Account& out) {
    In in{body};
    out.version = in.u32();
    out.key = in.str();
    const uint32_t n = in.u32();
    // A machine has five characters at most; a list longer than a screen's is not one.
    if (n > 16) return false;
    out.claims.clear();
    for (uint32_t i = 0; i < n && in.ok; ++i) {
        Claim c;
        c.token = in.u64();
        c.name = in.str();
        c.slot = in.i32();
        out.claims.push_back(std::move(c));
    }
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Roster& out) {
    In in{body};
    out.refused = Refused(in.u8());
    const uint32_t n = in.u32();
    if (n > 16) return false;
    out.seats.clear();
    for (uint32_t i = 0; i < n && in.ok; ++i) {
        Seat s;
        s.token = in.u64();
        s.slot = in.i32();
        s.name = in.str();
        s.kin = in.u8();
        s.level = in.i32();
        s.second = in.u8() != 0;
        s.world = in.str();
        const uint8_t worn = in.u8();
        for (uint8_t w = 0; w < worn && in.ok; ++w) {
            Seat::Worn one;
            one.slot = in.u8();
            one.held = takeHeld(in);
            s.worn.push_back(one);
        }
        out.seats.push_back(std::move(s));
    }
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Create& out) {
    In in{body};
    out.name = in.str();
    out.kin = in.u8();
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Delete& out) {
    In in{body};
    out.token = in.u64();
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Elsewhere& out) {
    In in{body};
    out.world = in.str();
    out.column = in.i32();
    out.row = in.i32();
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
    out.you = in.u32();
    out.backlog = in.u32();
    out.token = in.u64();
    out.kept = in.u8() != 0;
    if (out.kept) out.first = takeKept(in);
    out.castle = in.i32();
    out.copy = in.u8();
    out.udpKey = in.u64();
    const uint32_t snapshot = in.u32();
    out.snapshot.clear();
    if (!in.need(snapshot)) return false;
    out.snapshot.assign(body.begin() + long(in.at), body.begin() + long(in.at + snapshot));
    in.at += snapshot;
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
    const uint8_t weather = in.u8();
    out.rain = (weather & 1) != 0;
    out.invasionElsewhere = (weather & 2) != 0;
    const uint32_t count = in.u32();
    // Each command is 35 bytes: a count past what the body holds is a lie, refused before any
    // memory is reserved for it.
    if (!in.ok || size_t(count) * 35 > body.size()) return false;
    out.commands.clear();
    out.commands.reserve(count);
    for (uint32_t i = 0; i < count && in.ok; ++i) out.commands.push_back(takeCommand(in));
    // A kept character is a few KB: more than the body holds is a lie, as above.
    const uint32_t arriving = in.u32();
    if (!in.ok || size_t(arriving) * 1024 > body.size()) return false;
    out.arrivals.clear();
    for (uint32_t i = 0; i < arriving && in.ok; ++i) {
        Arrival a;
        a.ticket = in.u32();
        a.kept = takeKept(in);
        out.arrivals.push_back(std::move(a));
    }
    out.early = in.u8() != 0;
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Ping& out) {
    In in{body};
    out.nonce = in.u32();
    return in.done();
}

bool parse(const std::vector<uint8_t>& body, Hash& out) {
    In in{body};
    out.tick = in.u32();
    out.hash = in.u64();
    return in.done();
}

void putKept(std::vector<uint8_t>& out, const sim::Kept& one) {
    Out o{out};
    putKept(o, one);
}

bool keptFrom(const std::vector<uint8_t>& bytes, sim::Kept& out, int layout) {
    In in{bytes};
    out = takeKept(in, layout);
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
    // Every player in join order, never "the hero": a mirror looks at its own player and the
    // server at the first, and the hash must not care which.
    if (!realm.bodies().empty()) {
        for (int p = 0; p < realm.playerCount(); ++p) {
            const sim::Body& hero = realm.playerAt(p);
            mix(&hero.x, sizeof hero.x);
            mix(&hero.y, sizeof hero.y);
            mix(&hero.health, sizeof hero.health);
        }
    }
    return h;
}

// The most a packed snapshot may say it unpacks to: a guard on a length read off the line.
constexpr uint32_t kMostUnpacked = 256u << 20;

bool pack(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out) {
    out.clear();
    if (raw.empty()) return true;
    uLongf size = compressBound(uLong(raw.size()));
    out.resize(4 + size);
    for (int i = 0; i < 4; ++i) out[size_t(i)] = uint8_t(uint32_t(raw.size()) >> (8 * i));
    if (compress2(out.data() + 4, &size, raw.data(), uLong(raw.size()), Z_BEST_SPEED) != Z_OK) {
        out.clear();
        return false;
    }
    out.resize(4 + size);
    return true;
}

bool unpack(const std::vector<uint8_t>& packed, std::vector<uint8_t>& out) {
    out.clear();
    if (packed.empty()) return true;
    if (packed.size() < 4) return false;
    uint32_t raw = 0;
    for (int i = 0; i < 4; ++i) raw |= uint32_t(packed[size_t(i)]) << (8 * i);
    if (raw > kMostUnpacked) return false;
    out.resize(raw);
    uLongf size = raw;
    if (uncompress(out.data(), &size, packed.data() + 4, uLong(packed.size() - 4)) != Z_OK || size != raw) {
        out.clear();
        return false;
    }
    return true;
}

namespace {

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

uint32_t get32(const std::vector<uint8_t>& in, size_t at) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(in[at + size_t(i)]) << (8 * i);
    return v;
}

}  // namespace

void putBind(std::vector<uint8_t>& out, uint64_t key) {
    out.clear();
    put32(out, kBindMagic);
    put32(out, uint32_t(key));
    put32(out, uint32_t(key >> 32));
}

bool takeBind(const std::vector<uint8_t>& datagram, uint64_t& key) {
    if (datagram.size() != 12 || get32(datagram, 0) != kBindMagic) return false;
    key = uint64_t(get32(datagram, 4)) | (uint64_t(get32(datagram, 8)) << 32);
    return key != 0;
}

bool putTicks(std::vector<uint8_t>& out, const std::vector<const std::vector<uint8_t>*>& frames) {
    out.clear();
    size_t room = kMostDatagramBytes - 4, first = frames.size();
    while (first > 0 && frames[first - 1]->size() <= room) {
        room -= frames[first - 1]->size();
        --first;
    }
    if (first == frames.size()) return false;
    put32(out, kTicksMagic);
    for (size_t i = first; i < frames.size(); ++i) out.insert(out.end(), frames[i]->begin(), frames[i]->end());
    return true;
}

bool ticksOf(const std::vector<uint8_t>& datagram, std::vector<uint8_t>& frames) {
    if (datagram.size() < 4 || get32(datagram, 0) != kTicksMagic) return false;
    frames.assign(datagram.begin() + 4, datagram.end());
    return true;
}

}  // namespace mu::net
