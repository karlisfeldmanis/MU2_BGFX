// The party, the realm's half (docs/party.md): which of its bodies share one, and a kill's
// experience and quest count shared among the members near it. The parties themselves are the
// server's, since a party spans maps and a map is a realm; it says each change in as
// Command::Kind::PartySet.
#include "sim/realm.h"

#include <algorithm>
#include <limits>

#include "sim/realm_tuning.h"
#include "sim/skills.h"

namespace mu::sim {

bool Realm::setParty(uint32_t id, uint32_t party) {
    Body* one = body(id);
    if (one == nullptr || !one->player) return false;
    one->party = party;
    return true;
}

void Realm::partyInView(const Body& dead, const Body& earner, std::vector<Body*>* out) {
    out->clear();
    if (earner.party == 0) {
        out->push_back(&bodies_[size_t(&earner - bodies_.data())]);
        return;
    }
    for (const Player& player : heroes_) {
        Body& one = bodies_[player.body];
        if (one.gone || !one.alive() || one.party != earner.party) continue;
        const float dx = one.x - dead.x, dy = one.y - dead.y;
        // `gObjCalDistance < 10`, on the kill's map: the realm is one map.
        if (dx * dx + dy * dy >= kPartyReach * kPartyReach) continue;
        out->push_back(&one);
    }
    // The one the kill is his always shares it, however far his arrow flew.
    if (std::find(out->begin(), out->end(), &earner) == out->end()) {
        out->push_back(&bodies_[size_t(&earner - bodies_.data())]);
    }
}

void Realm::payKill(const Body& dead, Body& earner) {
    std::vector<Body*> view;
    partyInView(dead, earner, &view);
    // The Rings of Wisdom's on top, each his own, and the game's rate (as Realm::kill's paid()).
    const auto rated = [&](const Body& to, double experience) {
        return int32_t(experience * kExperienceRate * double(100 + to.excel.moreExperience) / 100.0);
    };
    if (view.size() <= 1) {
        gain(earner, rated(earner, killExperience(dead.level, earner.level)));
        return;
    }
    int totalLevel = 0;
    bool wizard = false, knight = false, elf = false;
    for (const Body* one : view) {
        totalLevel += one->level;
        wizard |= one->kin == Kin::DarkWizard;
        knight |= one->kin == Kin::DarkKnight;
        elf |= one->kin == Kin::FairyElf;
    }
    const double total = partyExperience(dead.level, totalLevel, int(view.size()), wizard && knight && elf);
    for (Body* one : view) {
        double share = total * double(one->level) / double(totalLevel);
        // No more than his own level's worth (`if( exp > mymaxexp ) exp = mymaxexp`).
        share = std::min(share, double(neededExperience(one->level)));
        For his(*this, *one);
        gain(*one, rated(*one, share));
    }
}

void Realm::countPartyKill(const Body& dead, const Body& earner) {
    std::vector<Body*> view;
    partyInView(dead, earner, &view);
    for (Body* one : view) {
        For his(*this, *one);
        countKill(dead);
    }
}

bool Realm::tanking(const Body& one) const {
    return one.player && (one.kin == Kin::DarkKnight || one.kin == Kin::MagicGladiator) &&
           one.boonSkill == skill::kDefense && one.boonUntil > tick_;
}

int32_t Realm::threatOf(const Body& beast, uint32_t id) const {
    for (int i = 0; i < Body::kThreatSlots; ++i) {
        if (beast.threatBy[i] == id) return beast.threat[i];
    }
    return 0;
}

void Realm::addThreat(Body& beast, const Body& by, int32_t amount) {
    if (amount <= 0) return;
    int slot = -1, weakest = 0;
    for (int i = 0; i < Body::kThreatSlots; ++i) {
        if (beast.threatBy[i] == by.id) {
            slot = i;
            break;
        }
        if (beast.threatBy[i] == 0 && (slot < 0 || beast.threatBy[slot] != 0)) slot = i;
        if (beast.threat[i] < beast.threat[weakest]) weakest = i;
    }
    // Six held; a seventh takes the weakest's place only when he outdoes it.
    if (slot < 0) {
        if (amount <= beast.threat[weakest]) return;
        slot = weakest;
        beast.threat[slot] = 0;
    }
    beast.threatBy[slot] = by.id;
    const int64_t sum = int64_t(beast.threat[slot]) + amount;
    beast.threat[slot] = int32_t(std::min<int64_t>(sum, std::numeric_limits<int32_t>::max()));
}

void Realm::retarget(Body& beast) {
    const Body* current = find(beast.quarry);
    const bool holds = current != nullptr && current->alive() && !current->gone;
    const int32_t held = holds ? threatOf(beast, beast.quarry) : 0;
    uint32_t best = 0;
    int32_t most = 0;
    for (int i = 0; i < Body::kThreatSlots; ++i) {
        const Body* one = beast.threatBy[i] != 0 ? find(beast.threatBy[i]) : nullptr;
        if (one == nullptr || !one->alive() || one->gone || beast.threat[i] <= most) continue;
        best = beast.threatBy[i];
        most = beast.threat[i];
    }
    if (best == 0 || best == beast.quarry) return;
    // A target that is no party's, or gone, gives way at once; one held, only past the margin.
    if (holds && held > 0) {
        const Body& rival = *find(best);
        const double margin = within(beast, rival, kThreatNear) ? kThreatMarginNear : kThreatMarginFar;
        if (double(most) <= double(held) * margin) return;
    }
    beast.quarry = best;
}

void Realm::partyBlow(Body& beast, const Body& attacker, int32_t damage) {
    const bool tank = tanking(attacker);
    int64_t amount = int64_t(std::max(1, damage)) * (tank ? kPartyTankThreat : 1);
    if (tank && threatOf(beast, attacker.id) == 0) amount += beast.maxHealth / kTankOpener;
    addThreat(beast, attacker, int32_t(std::min<int64_t>(amount, std::numeric_limits<int32_t>::max())));
    retarget(beast);
}

void Realm::healThreat(const Body& healer, const Body& healed, int32_t mended) {
    if (healer.party == 0 || mended <= 0) return;
    const int32_t amount = std::max(1, int32_t(double(mended) * kHealThreat));
    for (Body& one : bodies_) {
        if (!one.monster() || !one.alive() || one.quarry != healed.id) continue;
        addThreat(one, healer, amount);
        retarget(one);
    }
}

}  // namespace mu::sim
