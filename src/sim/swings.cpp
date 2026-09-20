#include "sim/swings.h"

#include <algorithm>
#include <cmath>

namespace mu::sim {
namespace {

// MU's item groups, as far as the ladder cares. 0 swords, 1 axes, 2 maces -- and the client's
// first test spans all three at once, which is why a Small Axe swings a sword.
constexpr int32_t kMaces = 2;
constexpr int32_t kSpears = 3;
constexpr int32_t kStaves = 5;

// `AttackSpeed1 = CharacterAttribute->AttackSpeed * 0.004f`, ZzzCharacter.cpp:813.
constexpr float kSpeedToPlaySpeed = 0.004f;
// The frame rate MU's play speeds are stated against: a play speed is a fraction of MU's own
// frame rate rather than a rate itself.
constexpr float kReferenceFps = 25.0f;

// What one point of agility is worth in attack speed, by class.
// ClassDarkKnight.cs:58, ClassDarkWizard.cs:58, ClassFairyElf.cs:63.
float attackSpeedPerAgility(Kin kin) {
    switch (kin) {
        case Kin::DarkWizard: return 1.0f / 20.0f;
        case Kin::FairyElf: return 1.0f / 50.0f;
        case Kin::DarkKnight: break;
    }
    return 1.0f / 15.0f;
}

}  // namespace

int attackActions(const content::Arm* right, const content::Arm* left, int32_t* out) {
    // 38 Attack fist, and the only rung above the ladder: nothing in either hand.
    if (!right && !left) {
        out[0] = 38;
        return 1;
    }
    const bool rightSwings = right && !right->isShield() && right->group >= 0 &&
                             right->group <= kMaces;
    const bool leftSwings = left && !left->isShield() && left->group >= 0 &&
                            left->group <= kMaces;

    // Swords, axes and maces together.
    if (rightSwings) {
        if (right->twoHanded()) {
            // 43, 44, 45 by the client's own counter. The four two-handed blades that play
            // something else instead are all Season 3 and later.
            out[0] = 43;
            out[1] = 44;
            out[2] = 45;
            return 3;
        }
        if (leftSwings) {
            // A weapon in each hand alternates them: right 1, left 1, right 2, left 2.
            out[0] = 39;
            out[1] = 41;
            out[2] = 40;
            out[3] = 42;
            return 4;
        }
        out[0] = 39;
        out[1] = 40;
        return 2;
    }

    // A sword in the off hand only, which the client picks at random rather than by the counter.
    if (leftSwings) {
        out[0] = 41;
        out[1] = 42;
        return 2;
    }

    // A one-handed staff swings like a sword; a two-handed one casts, and a cast's own clips are
    // the skill weapons, which this game has no skills for yet. Bare hands until it does, and
    // that is a gap rather than a decision -- PLAN.md's skills are their own sprint.
    if (right && right->group == kStaves) {
        if (!right->twoHanded()) {
            out[0] = 39;
            out[1] = 40;
            return 2;
        }
        out[0] = 38;
        return 1;
    }

    // 46 Attack spear 1, for the two the client names individually: the Spear at (3,1) and the
    // Dragon Lance at (3,2). Every other polearm in the group falls to the scythe below, the
    // Berdysh included -- which is the ladder's order doing real work.
    if (right && right->group == kSpears && (right->number == 1 || right->number == 2)) {
        out[0] = 46;
        return 1;
    }
    if (right && right->group == kSpears) {
        out[0] = 47;
        out[1] = 48;
        out[2] = 49;
        return 3;
    }

    // 50 Attack bow, 51 Attack crossbow. The bow is a left-hand item and the crossbow a
    // right-hand one, which is why the two are tested on different hands.
    if (left && left->bow()) {
        out[0] = 50;
        return 1;
    }
    if (right && right->crossbow()) {
        out[0] = 51;
        return 1;
    }

    // Holding something the ladder does not recognise -- a shield alone, a jewel -- is the fist
    // again, and the client's final else says so.
    out[0] = 38;
    return 1;
}

float attackSpeedStat(Kin kin, int agility, const content::Arm* right, const content::Arm* left) {
    float weapon = 0.0f;
    if (right && !right->isShield()) weapon += float(right->attackSpeed);
    if (left && !left->isShield()) weapon += float(left->attackSpeed);
    // Two weapons halve what the weapons contribute, before it is added to what agility buys.
    if (right && left && !right->isShield() && !left->isShield()) weapon *= 0.5f;
    return float(agility) * attackSpeedPerAgility(kin) + weapon;
}

int swingMilliseconds(const content::Tables& tables, Kin kin, int agility,
                      const content::Arm* right, const content::Arm* left) {
    int32_t actions[4] = {};
    const int count = attackActions(right, left, actions);
    const float bonus = attackSpeedStat(kin, agility, right, left) * kSpeedToPlaySpeed;

    float total = 0.0f;
    int counted = 0;
    for (int i = 0; i < count; ++i) {
        const content::PlayerAction* clip = tables.action(actions[i]);
        if (!clip || clip->keys <= 0) continue;
        const float rate = (clip->speed + bonus) * kReferenceFps;
        if (rate <= 0.0f) continue;
        total += float(clip->keys) / rate;
        ++counted;
    }
    // Averaged over the clips he cycles through, which is the one place this departs from the
    // client: in MU the interval is per swing, because each swing is whichever clip the counter
    // landed on and they are not all the same length, while a body here carries a single swing
    // delay. The mean is the rate that comes out of it over any stretch longer than three
    // swings; taking the first instead would make a two-handed axe's rate depend on which of
    // its three actions happened to be written down first.
    if (counted == 0) return 0;
    return int(total / float(counted) * 1000.0f);
}

int32_t swingTicks(int milliseconds) {
    if (milliseconds <= 0) return 0;
    // max(1, ceil(ms / 50)) -- MU2's Realm.Ticks, and it rounds UP on purpose.
    return int32_t(std::max(1, (milliseconds + 49) / 50));
}

}  // namespace mu::sim
