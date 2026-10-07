// The realm as bytes, and back (Realm::snapshot): what a server sends a newcomer instead of every
// tick since the world was raised (docs/sprints/21-the-snapshot.md).
//
// One field list, serialize(), walked by a writer and by a reader, so the two cannot drift. Plain
// data -- a Body, a Player, a flight, a die -- goes whole; the few containers go by count. A
// field added to the realm must be added there, or a mirror raised from a snapshot will not
// stand where the server stands, which the lockstep hash says within a second.

#include <cstring>
#include <type_traits>

#include "sim/realm.h"

namespace mu::sim {

namespace {

constexpr uint32_t kMagic = 0x4E53554Du;  // "MUSN"
constexpr uint32_t kShape = 1;            // bumped when serialize() changes

struct Writer {
    static constexpr bool reading = false;
    std::vector<uint8_t>& bytes;
    template <class T>
    void pod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "a snapshot copies plain data only");
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    template <class T>
    void vec(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>, "a snapshot copies plain data only");
        pod(uint32_t(v.size()));
        if (v.empty()) return;
        const auto* p = reinterpret_cast<const uint8_t*>(v.data());
        bytes.insert(bytes.end(), p, p + v.size() * sizeof(T));
    }
    bool ok = true;
};

struct Reader {
    static constexpr bool reading = true;
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool ok = true;
    template <class T>
    void pod(T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "a snapshot copies plain data only");
        if (!ok || bytes.size() - at < sizeof(T)) {
            ok = false;
            return;
        }
        std::memcpy(&v, bytes.data() + at, sizeof(T));
        at += sizeof(T);
    }
    template <class T>
    void vec(std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>, "a snapshot copies plain data only");
        uint32_t n = 0;
        pod(n);
        if (!ok || (bytes.size() - at) / sizeof(T) < n) {
            ok = false;
            return;
        }
        v.resize(n);
        if (n != 0) std::memcpy(v.data(), bytes.data() + at, size_t(n) * sizeof(T));
        at += size_t(n) * sizeof(T);
    }
};

// A townsperson's rounds as data: his row by its index in kStrollers, not by its address.
struct StrollerData {
    uint32_t id;
    int32_t row;
    int32_t stop;
    bool there;
    int64_t leaves;
    bool held;
    int64_t freeAt;
    int32_t perch;
    int32_t chatLine;
    int64_t chatAt;
};

}  // namespace

int strollIndex(const StrollRow* row);  // realm_folk.cpp
const StrollRow* strollAt(int index);

template <class Archive>
void Realm::serialize(Archive& a) {
    // The shapes it was written by: another layout of these is another program.
    uint32_t sizes[] = {uint32_t(sizeof(Body)), uint32_t(sizeof(Player)), uint32_t(sizeof(Happening)),
                        uint32_t(sizeof(Flight)), uint32_t(sizeof(Kept)), uint32_t(sizeof(Lying))};
    for (uint32_t& size : sizes) {
        const uint32_t mine = size;
        a.pod(size);
        if (size != mine) a.ok = false;
    }
    if (!a.ok) return;

    a.pod(config_);
    a.pod(dice_);
    a.vec(roads_);
    a.vec(bodies_);
    // Each body's route, by the same index.
    {
        uint32_t n = uint32_t(routes_.size());
        a.pod(n);
        if constexpr (Archive::reading) {
            if (!a.ok || n > bodies_.size()) {
                a.ok = false;
                return;
            }
            routes_.assign(n, {});
        }
        for (uint32_t i = 0; i < n && a.ok; ++i) a.vec(routes_[i]);
    }
    a.vec(players_);
    a.vec(indexOfId_);
    a.vec(happenings_);
    a.vec(commands_);
    a.vec(carried_);
    a.pod(flights_);
    a.pod(spiritBlows_);
    a.pod(fires_);
    a.pod(loosingPlague_);
    a.pod(wingDemo_);
    a.pod(tick_);
    a.pod(nextId_);
    for (Random* dice : {&wearDice_, &wardenDice_, &summonDice_, &runeDice_, &trapDice_, &bossDice_,
                         &chillDice_, &crackerDice_, &featherDice_, &novaScrollDice_, &wingDice_,
                         &gearDice_, &ticketDice_, &treasureDice_, &orbDice_, &showerDice_,
                         &invasionDice_, &raidDice_, &raiderDice_, &mixDice_}) {
        a.pod(*dice);
    }
    a.vec(traps_);
    a.pod(invaderSlot_);
    a.pod(invasion_);
    a.pod(invasionOwed_);
    a.pod(invasionNow_);
    // The raid's own (the Golden Dragon), all but its party, which a snapshot does not carry.
    a.pod(raid_);
    a.pod(raidPlayers_);
    a.pod(raidAsked_);
    a.pod(raidLanding_);
    a.pod(raidHand_);
    a.vec(raiderSlots_);
    a.vec(minionSlots_);
    a.vec(raiderBags_);
    a.pod(hazards_);
    a.pod(reactAt_);
    a.pod(reactSerial_);
    a.pod(raidPotions_);
    a.pod(raidNext_);
    a.pod(drinkAt_);
    a.pod(run_);
    a.pod(byFloor_);
    a.vec(floors_);
    // The townspeople.
    {
        std::vector<StrollerData> folk;
        if constexpr (!Archive::reading) {
            for (const Stroller& s : strollers_) {
                // Zeroed first, padding and all, so the same realm always says the same bytes.
                StrollerData d;
                std::memset(&d, 0, sizeof d);
                d.id = s.id;
                d.row = int32_t(strollIndex(s.row));
                d.stop = s.stop;
                d.there = s.there;
                d.leaves = s.leaves;
                d.held = s.held;
                d.freeAt = s.freeAt;
                d.perch = s.perch;
                d.chatLine = s.chatLine;
                d.chatAt = s.chatAt;
                folk.push_back(d);
            }
        }
        a.vec(folk);
        if constexpr (Archive::reading) {
            strollers_.clear();
            for (const StrollerData& d : folk) {
                Stroller s;
                s.id = d.id;
                s.row = strollAt(d.row);
                if (s.row == nullptr && d.row != -1) {
                    a.ok = false;
                    return;
                }
                s.stop = d.stop;
                s.there = d.there;
                s.leaves = d.leaves;
                s.held = d.held;
                s.freeAt = d.freeAt;
                s.perch = d.perch;
                s.chatLine = d.chatLine;
                s.chatAt = d.chatAt;
                strollers_.push_back(s);
            }
        }
    }
    a.pod(wall_);
    a.pod(castleOpen_);
    a.vec(lying_);
    a.vec(heroes_);
    a.pod(me_);
    // A grid the realm changed (Blood Castle's barrier, bridge and doors): all of it.
    {
        bool changed = own_ != nullptr;
        a.pod(changed);
        if (changed) {
            std::vector<uint16_t> words;
            if constexpr (!Archive::reading) words = own_->grid.words();
            a.vec(words);
            if constexpr (Archive::reading) {
                if (!a.ok || !own_ || words.size() != own_->grid.words().size()) {
                    a.ok = false;
                    return;
                }
                own_->grid.set(own_->grid.size(), std::move(words));
            }
        }
    }
}

bool Realm::snapshot(std::vector<uint8_t>& out) const {
    if (!party_.empty()) return false;
    out.clear();
    Writer w{out};
    w.pod(kMagic);
    w.pod(kShape);
    const_cast<Realm*>(this)->serialize(w);
    return w.ok;
}

bool Realm::restoreSnapshot(const std::vector<uint8_t>& bytes) {
    if (!tables_) return false;
    Reader r{bytes};
    uint32_t magic = 0, shape = 0;
    r.pod(magic);
    r.pod(shape);
    if (!r.ok || magic != kMagic || shape != kShape) return false;
    serialize(r);
    if (!r.ok || r.at != bytes.size()) return false;
    // What is worked out from the rest rather than kept: whose the queries are (me_ was read)
    // and nothing else -- the router's buffers are scratch, refilled by its next plan.
    return me_ < heroes_.size();
}

}  // namespace mu::sim
