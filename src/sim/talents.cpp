// Talents' rules (sim/talents.h).
#include "sim/talents.h"

#include <algorithm>

namespace mu::sim {

TalentTree talentTree(Kin kin) {
    switch (kin) {
        case Kin::DarkKnight: return {kDarkKnightTalents, int(std::size(kDarkKnightTalents))};
        case Kin::DarkWizard: return {kDarkWizardTalents, int(std::size(kDarkWizardTalents))};
        case Kin::FairyElf: return {kFairyElfTalents, int(std::size(kFairyElfTalents))};
        case Kin::MagicGladiator:
            return {kMagicGladiatorTalents, int(std::size(kMagicGladiatorTalents))};
        // His tree is phase 8's (docs/plans/dark-lord.md); none until then.
        case Kin::DarkLord: break;
    }
    return {};
}

int talentPoints(Kin kin, int level, int resets) {
    const int per = pointsPerLevel(kin);
    const int statPoints = (std::max(1, level) - 1) * per + std::max(0, resets) * kMaximumLevel * per;
    return statPoints / kStatPointsPerTalent;
}

int talentsSpent(const TalentRanks& ranks) {
    int spent = 0;
    for (const auto& column : ranks) {
        for (const uint8_t rank : column) spent += rank;
    }
    return spent;
}

bool talentOpen(const std::array<uint8_t, kTalentNodes>& column, int i) {
    const int tier = i / 3;
    if (isMajor(i)) return column[size_t(i - 2)] >= kTalentMajorOpensAt || column[size_t(i - 1)] >= kTalentMajorOpensAt;
    return tier == 0 || column[size_t(tier * 3 - 1)] >= 1;
}

bool talentTakeable(const std::array<uint8_t, kTalentNodes>& column, int i) {
    if (column[size_t(i)] == 0) return false;
    std::array<uint8_t, kTalentNodes> next = column;
    --next[size_t(i)];
    for (int j = 0; j < kTalentNodes; ++j) {
        if (next[size_t(j)] > 0 && !talentOpen(next, j)) return false;
    }
    return true;
}

bool talentsValid(Kin kin, const TalentRanks& ranks, int points) {
    const TalentTree tree = talentTree(kin);
    for (int c = 0; c < kTalentColumnsMost; ++c) {
        for (int i = 0; i < kTalentNodes; ++i) {
            const int rank = ranks[size_t(c)][size_t(i)];
            if (rank == 0) continue;
            if (c >= tree.count || rank > mostOf(i) || !talentOpen(ranks[size_t(c)], i)) return false;
        }
    }
    return talentsSpent(ranks) <= points;
}

namespace {

bool holds(const TalentAsk& ask, Hold hold) {
    if (hold == Hold::Any) return true;
    for (const Hold one : ask.holding) {
        if (one == hold) return true;
    }
    return false;
}

bool counts(const TalentEffect& effect, const TalentAsk& ask) {
    if (!holds(ask, effect.hold)) return false;
    if (effect.elements != 0 && (ask.element == Element::None || (effect.elements & bit(ask.element)) == 0)) {
        return false;
    }
    // Frostbite's: every spell and nothing else.
    if (effect.special == Special::VsChilled && effect.skills[0] == 0 && !ask.spell) return false;
    if (effect.skills[0] != 0) {
        for (const int32_t skill : effect.skills) {
            if (skill != 0 && skill == ask.skill) return true;
        }
        return false;
    }
    return true;
}

}  // namespace

float talentSum(Kin kin, const TalentRanks& ranks, TalentStat stat, const TalentAsk& ask) {
    const TalentTree tree = talentTree(kin);
    float sum = 0.0f;
    for (int c = 0; c < tree.count; ++c) {
        for (int i = 0; i < kTalentNodes; ++i) {
            const int rank = ranks[size_t(c)][size_t(i)];
            const TalentEffect& effect = tree.columns[c].nodes[i].effect;
            if (rank == 0 || effect.stat != stat || !counts(effect, ask)) continue;
            sum += effect.amount * float(isMajor(i) ? 1 : rank);
        }
    }
    return sum;
}

TalentShares talentShares(Kin kin, const TalentRanks& ranks, const TalentAsk& holding) {
    TalentShares s;
    const TalentTree tree = talentTree(kin);
    for (int c = 0; c < tree.count; ++c) {
        for (int i = 0; i < kTalentNodes; ++i) {
            const int rank = ranks[size_t(c)][size_t(i)];
            if (rank == 0) continue;
            s.any = true;
            const TalentEffect& e = tree.columns[c].nodes[i].effect;
            if (e.skills[0] != 0 || !holds(holding, e.hold)) continue;
            const float v = e.amount * float(isMajor(i) ? 1 : rank);
            switch (e.stat) {
                case TalentStat::Damage: s.damage += v; break;
                case TalentStat::Wizardry: s.wizardry += v; break;
                case TalentStat::DamageAndWizardry: s.damage += v, s.wizardry += v; break;
                case TalentStat::ElementDamage:
                    for (int el = 1; el < kElements; ++el) {
                        if (e.elements & bit(Element(el))) s.element[el] += v;
                    }
                    break;
                case TalentStat::AttackSpeed: s.attackSpeed += v; break;
                case TalentStat::CastSpeed: s.castSpeed += v; break;
                case TalentStat::LightningChannel: s.lightningChannel += v; break;
                case TalentStat::AttackRate: s.attackRate += v; break;
                case TalentStat::DefenseRate: s.defenseRate += v; break;
                case TalentStat::Defense: s.defense += v; break;
                case TalentStat::CriticalChance: s.critical += v; break;
                case TalentStat::ExcellentChance: s.excellent += v; break;
                case TalentStat::IgnoreDefense: s.ignoreDefense += v; break;
                case TalentStat::DamageDecrease: s.damageDecrease += v; break;
                case TalentStat::Reflect: s.reflect += v; break;
                case TalentStat::BlockChance: s.block += v; break;
                case TalentStat::GuardEffect: s.guardEffect += v; break;
                case TalentStat::BarrierEffect: s.barrierEffect += v; break;
                case TalentStat::GuardAndBarrierEffect: s.guardEffect += v, s.barrierEffect += v; break;
                case TalentStat::MaxLife: s.maxLife += v; break;
                case TalentStat::MaxMana: s.maxMana += v; break;
                case TalentStat::MaxShield: s.maxShield += v; break;
                case TalentStat::ManaRegen: s.manaRegen += v; break;
                case TalentStat::LifeOnKill: s.lifeOnKill += v; break;
                case TalentStat::LifeSteal: s.lifeSteal += v; break;
                case TalentStat::IcePoisonResistance: s.icePoisonResistance += v; break;
                case TalentStat::Threat: s.threat += v; break;
                case TalentStat::ChillLonger: s.chillLonger += v; break;
                case TalentStat::PoisonLonger: s.poisonLonger += v; break;
                case TalentStat::ChilledSlower: s.chilledSlower += v; break;
                case TalentStat::HealingDone: s.healing += v; break;
                case TalentStat::AidManaCost: s.aidManaCost += v; break;
                case TalentStat::BuffEffect: s.buffEffect += v; break;
                case TalentStat::AidReach: s.aidReach += v; break;
                case TalentStat::SummonDamage: s.summonDamage += v; break;
                case TalentStat::SummonLife: s.summonLife += v; break;
                case TalentStat::SummonDefense: s.summonDefense += v; break;
                case TalentStat::SummonAttackRate: s.summonAttackRate += v; break;
                case TalentStat::SummonManaCost: s.summonManaCost += v; break;
                case TalentStat::SummonDamageTaken: s.summonDamageTaken += v; break;
                case TalentStat::SummonDamageAndLife: s.summonDamage += v, s.summonLife += v; break;
                case TalentStat::MoreZen: s.moreZen += v; break;
                case TalentStat::MoreJewels: s.moreJewels += v; break;
                case TalentStat::MoreItems: s.moreItems += v; break;
                case TalentStat::MoreExcellentItems: s.moreExcellent += v; break;
                case TalentStat::PotionHealing: s.potionHealing += v; break;
                case TalentStat::Special:
                    if (e.special == Special::OffHand) s.offHand += v;
                    if (e.special == Special::WizardryIntoSwings) s.wizardryIntoSwings += v;
                    break;
                case TalentStat::SkillDamage:
                case TalentStat::SkillManaCost: break;
            }
        }
    }
    return s;
}

float talentAttunement(Kin kin, const TalentRanks& ranks, Element element, const int runes[kElements]) {
    if (element == Element::None) return 0.0f;
    const TalentTree tree = talentTree(kin);
    float sum = 0.0f;
    for (int c = 0; c < tree.count; ++c) {
        for (int i = 2; i < kTalentNodes; i += 3) {
            const TalentEffect& effect = tree.columns[c].nodes[i].effect;
            if (ranks[size_t(c)][size_t(i)] == 0 || effect.special != Special::Attunement ||
                (effect.elements & bit(element)) == 0) {
                continue;
            }
            int worn = 0;
            for (int e = 1; e < kElements; ++e) {
                if (effect.elements & bit(Element(e))) worn += runes[e];
            }
            sum += effect.amount * float(std::min(worn, kTalentAttunedMost));
        }
    }
    return sum;
}

float talentSpecial(Kin kin, const TalentRanks& ranks, Special special, const TalentAsk& ask,
                    const TalentEffect** found) {
    const TalentTree tree = talentTree(kin);
    float sum = 0.0f;
    if (found) *found = nullptr;
    for (int c = 0; c < tree.count; ++c) {
        for (int i = 2; i < kTalentNodes; i += 3) {
            const TalentEffect& effect = tree.columns[c].nodes[i].effect;
            if (ranks[size_t(c)][size_t(i)] == 0 || effect.special != special || !counts(effect, ask)) continue;
            if (found && !*found) *found = &effect;
            sum += effect.amount;
        }
    }
    return sum;
}

}  // namespace mu::sim
