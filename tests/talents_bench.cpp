// The talent bench (docs/plans/talents.md): clang++ -std=c++20 -I src tests/talents_bench.cpp src/sim/talents.cpp
// What a talent tree adds to a build's main skill, with the plan's rule 1 (one sum, multiplied once).
// Procs and conditions at stated, pack-fight assumptions.
#include <cstdio>
#include <string>
#include <vector>
#include "sim/talents.h"
using namespace mu::sim;

// A column spent: "F" full (63), "S" spine (one small at 10 + major, each tier: 33), or explicit.
using Col = std::array<uint8_t, 9>;
const Col F{10, 10, 1, 10, 10, 1, 10, 10, 1}, S{10, 0, 1, 10, 0, 1, 10, 0, 1}, Z{};
const Col S2{0, 10, 1, 0, 10, 1, 0, 10, 1};  // the spine down the right-hand smalls

// Pack-fight assumptions for the conditional majors.
constexpr float kProcUptime = 0.65f;     // 10% a swing or cast at ~2 a second, 5 s: 1 - 0.9^10
constexpr float kMonstersInReach = 5.0f;
constexpr float kRunesWorn = 3.0f;       // element runes of the skill's element
constexpr float kUnder30 = 0.25f;        // share of blows on monsters under 30% life
constexpr float kRiposteShare = 0.25f;   // share of blows armed by a miss
constexpr float kFarShare = 0.7f;        // share of arrows at 5 tiles or more
constexpr float kChilledShare = 0.8f;    // share of spell blows on chilled monsters, Ice in the rotation

struct Build {
    const char* name; Kin kin; std::vector<Col> cols;
    const char* skillName; int32_t skill; Element element; Hold hold; bool spell; bool iceInRotation;
};

float bonus(const Build& b, const TalentRanks& r, std::string* why) {
    TalentAsk ask; ask.skill = b.skill; ask.element = b.element; ask.holding[0] = b.hold;
    if (b.hold == Hold::TwoHanded || b.hold == Hold::Bow) ask.holding[1] = Hold::Any;
    float flat = talentSum(b.kin, r, TalentStat::Damage, ask) + talentSum(b.kin, r, TalentStat::SkillDamage, ask) +
                 talentSum(b.kin, r, TalentStat::ElementDamage, ask) + talentSum(b.kin, r, TalentStat::DamageAndWizardry, ask);
    if (b.spell) flat += talentSum(b.kin, r, TalentStat::Wizardry, ask);
    float cond = 0.0f;
    auto sp = [&](Special x) { return talentSpecial(b.kin, r, x, ask); };
    cond += sp(Special::DamageProc) * kProcUptime;
    cond += sp(Special::EyeOfTheStorm) * kMonstersInReach;
    cond += sp(Special::Crowd);
    cond += sp(Special::Executioner) * kUnder30;
    cond += sp(Special::Riposte) * kRiposteShare;
    cond += sp(Special::FarShot) * kFarShare;
    cond += sp(Special::Attunement) * kRunesWorn;
    if (b.spell && b.iceInRotation) cond += sp(Special::VsChilled) * kChilledShare;
    if (why) *why = "flat " + std::to_string(int(flat)) + " + conditional " + std::to_string(int(cond));
    return flat + cond;
}

int main() {
    const std::vector<Build> builds = {
        {"Knight, two-handed whirlwind", Kin::DarkKnight, {F, Z, Z, F, F, {10,0,0,0,0,0,0,0,0}}, "Twisting Slash", 41, Element::Wind, Hold::TwoHanded, false, false},
        {"Knight, dual wield", Kin::DarkKnight, {Z, F, Z, S, F, {10,10,1,10,9,0,0,0,0}}, "Cyclone", 22, Element::Wind, Hold::DualWield, false, false},
        {"Knight, two-handed tank", Kin::DarkKnight, {F, Z, F, Z, F, {10,0,0,0,0,0,0,0,0}}, "Death Stab", 43, Element::None, Hold::TwoHanded, false, false},
        {"Wizard, fire", Kin::DarkWizard, {F, Z, S, F, Z, {10,10,1,10,9,0,0,0,0}}, "Hellfire", 10, Element::Fire, Hold::Any, true, true},
        {"Wizard, lightning and ice", Kin::DarkWizard, {Z, F, F, F, Z, {10,0,0,0,0,0,0,0,0}}, "Lightning", 3, Element::Lightning, Hold::Any, true, true},
        {"Wizard, evil spirit", Kin::DarkWizard, {Z, Z, S, F, F, {10,10,1,10,9,0,0,0,0}}, "Evil Spirit", 9, Element::None, Hold::Any, true, true},
        {"Elf, multi-shot", Kin::FairyElf, {F, S, Z, Z, F, {10,10,1,10,9,0,0,0,0}}, "Multi-Shot", 24, Element::None, Hold::Bow, false, false},
        {"Elf, penetration", Kin::FairyElf, {F, F, Z, Z, F, {10,0,0,0,0,0,0,0,0}}, "Penetration", 52, Element::Wind, Hold::Bow, false, false},
        {"Elf, support", Kin::FairyElf, {S, Z, F, S, F, {7,0,0,0,0,0,0,0,0}}, "Multi-Shot", 24, Element::None, Hold::Bow, false, false},
        {"MG, spellblade", Kin::MagicGladiator, {F, F, Z, Z, F, F, {10,10,1,6,0,0,0,0,0}}, "Twisting Slash", 41, Element::Wind, Hold::Any, false, false},
        {"MG, fire caster", Kin::MagicGladiator, {F, Z, F, S, F, S, {10,10,1,3,0,0,0,0,0}}, "Flame", 5, Element::Fire, Hold::Any, true, false},
        // The Dark Lord's, a scepter in hand (talents_lord.inc).
        {"Lord, force wave", Kin::DarkLord, {F, S, S, Z, F, {10,0,0,0,0,0,0,0,0}}, "Force Wave", 60, Element::None, Hold::OneHanded, false, false},
        {"Lord, fire burst", Kin::DarkLord, {S, F, S, Z, F, {10,0,0,0,0,0,0,0,0}}, "Fire Burst", 61, Element::None, Hold::OneHanded, false, false},
        {"Lord, electric spike", Kin::DarkLord, {S, S, F, Z, F, {10,0,0,0,0,0,0,0,0}}, "Electric Spike", 65, Element::None, Hold::OneHanded, false, false},
    };
    std::printf("%-30s %-15s %6s %6s  %s\n", "build", "skill", "spent", "build", "full tree");
    for (const Build& b : builds) {
        TalentRanks r{};
        for (size_t c = 0; c < b.cols.size(); ++c) r[c] = b.cols[c];
        const int points = talentPoints(b.kin, 400, 0);
        const int spent = talentsSpent(r);
        TalentRanks full{};
        for (int c = 0; c < talentTree(b.kin).count; ++c) full[size_t(c)] = F;
        std::string why, whyFull;
        const float at = bonus(b, r, &why), all = bonus(b, full, &whyFull);
        std::printf("%-30s %-15s %3d/%-3d %s+%3d%%  +%3d%%   (%s; full %s)\n", b.name, b.skillName, spent, points,
                    talentsValid(b.kin, r, points) ? "" : "!", int(at), int(all), why.c_str(), whyFull.c_str());
    }
}
