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

void Realm::partyInView(const Body& dead, const Body& earner, std::vector<Body*>* out,
                        std::vector<Body*>* gapped) {
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
    // Past the gap below the highest of them: nothing (kPartyGap), the kill's own earner too --
    // else the high one weakens it and the low one lands the blow.
    int top = 0;
    for (const Body* one : *out) top = std::max(top, one->level);
    std::erase_if(*out, [&](Body* one) {
        if (one->level >= top - kPartyGap) return false;
        if (gapped) gapped->push_back(one);
        return true;
    });
}

void Realm::payKill(const Body& dead, Body& earner) {
    std::vector<Body*> view, gapped;
    partyInView(dead, earner, &view, &gapped);
    // Each one left out hears why, where his experience would have been (game/ui/tally.cpp).
    int top = 0;
    for (const Body* one : view) top = std::max(top, one->level);
    for (const Body* one : gapped) {
        say(What::Gapped, *one, top);
        happenings_.back().whom = dead.id;
        happenings_.back().audience = one->id;
    }
    // The Rings of Wisdom's on top, each his own, and the game's rate (as Realm::kill's paid()).
    const auto rated = [&](const Body& to, double experience) {
        return int32_t(experience * kExperienceRate * double(100 + to.excel.moreExperience) / 100.0);
    };
    if (view.size() <= 1) {
        // Alone in view, or the only one left of it past the gap: his own kill's worth.
        Body& only = view.empty() ? earner : *view.front();
        For his(*this, only);
        gain(only, rated(only, killExperience(dead.level, only.level)));
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

void Realm::noteHurt(Body& beast, const Body& by, int amount) {
    if (!beast.monster() || amount <= 0) return;
    const Body* who = by.player ? &by : by.summoner != 0 ? find(by.summoner) : nullptr;
    if (who == nullptr || !who->player) return;
    int slot = -1;
    for (int i = 0; i < Body::kHurtSlots; ++i) {
        if (beast.hurtBy[i] == who->id) {
            slot = i;
            break;
        }
        // A free place, or else the one whose last is oldest (WebZen frees the forgotten).
        if (slot < 0 || (beast.hurtBy[slot] != 0 && (beast.hurtBy[i] == 0 || beast.hurtAt[i] < beast.hurtAt[slot]))) {
            slot = i;
        }
    }
    if (beast.hurtBy[slot] != who->id) {
        beast.hurtBy[slot] = who->id;
        beast.hurt[slot] = 0;
    }
    // No more than its whole life to anyone (`if( HitDamage > MaxLife ) HitDamage = MaxLife`).
    beast.hurt[slot] = int32_t(std::min<int64_t>(int64_t(beast.hurt[slot]) + amount, std::max(1, beast.maxHealth)));
    beast.hurtAt[slot] = tick_;
}

bool Realm::hurtCounts(const Body& beast, int i) const {
    if (beast.hurtBy[i] == 0 || beast.hurt[i] <= 0) return false;
    const Body* one = find(beast.hurtBy[i]);
    if (one == nullptr || one->gone || !one->alive()) return false;
    if (tick_ - beast.hurtAt[i] > int64_t(kHurtForgetSeconds) * 20) return false;
    return std::max(std::abs(one->column() - beast.column()), std::abs(one->row() - beast.row())) <= kHurtReach;
}

Body* Realm::topHurter(const Body& beast) {
    int best = -1;
    for (int i = 0; i < Body::kHurtSlots; ++i) {
        if (hurtCounts(beast, i) && (best < 0 || beast.hurt[i] > beast.hurt[best])) best = i;
    }
    return best >= 0 ? body(beast.hurtBy[best]) : nullptr;
}

void Realm::shareKill(const Body& dead, Body& earner) {
    if (earner.party != 0) {
        For his(*this, earner);
        payKill(dead, earner);
        return;
    }
    const double life = double(std::max(1, dead.maxHealth));
    bool paidAny = false;
    for (int i = 0; i < Body::kHurtSlots; ++i) {
        if (!hurtCounts(dead, i)) continue;
        Body* one = body(dead.hurtBy[i]);
        For his(*this, *one);
        const double whole = double(killExperience(dead.level, one->level)) * kExperienceRate *
                             double(100 + one->excel.moreExperience) / 100.0;
        gain(*one, int32_t(whole * double(dead.hurt[i]) / life));
        paidAny = true;
    }
    // Nobody counted (a blow that left no mark): the last hitter's, as before.
    if (!paidAny) {
        For his(*this, earner);
        payKill(dead, earner);
    }
}

void Realm::forgetThreat(uint32_t id) {
    for (Body& one : bodies_) {
        for (int i = 0; i < Body::kThreatSlots; ++i) {
            if (one.threatBy[i] != id) continue;
            one.threatBy[i] = 0;
            one.threat[i] = 0;
        }
    }
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

// Out of realm.h, unchanged, to pay the size ratchet for the bleed (2026-10-12).
bool Realm::mayTake(const Lying& one) const {
    // A bound thing (Held::bound) is its earner's, and nobody else's even once he has left.
    if (one.what.bound) return one.owner == mine().id;
    if (one.owner == 0 || one.owner == mine().id || playerOfId(one.owner) < 0) return true;
    // Gone from the world, a player keeps his place in heroes_ (Realm::depart): his things
    // are free as if he had never been, or the castle's weapon lay locked when its breaker left.
    const Body* owner = find(one.owner);
    if (owner == nullptr || owner->gone) return true;
    return mine().party != 0 && owner->party == mine().party;
}

}  // namespace mu::sim
