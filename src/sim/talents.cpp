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
