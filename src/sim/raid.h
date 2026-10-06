#pragma once

// The Golden Dragon as a raid boss: its four stages, what it throws in each, and the party of
// end-game characters who fight it. docs/golden-dragon-raid.md is the design, with every rule
// below traced there or marked "invention" in it; the comments here say which is which.
//
// The realm's part (realm_raid.cpp) is the whole fight -- the stages, the telegraphs and the
// hazards they lay, the minions, and the raiders' minds -- and says what happens as What::Raid.
// Drawing it is the game's.

#include <cstdint>
#include <string>
#include <vector>

#include "sim/rules.h"

namespace mu::sim {

// The raid minion: OpenMU Version095d's Golden Budge Dragon (InvasionMobsInitialization.cs:44-79).
constexpr int32_t kGoldenBudgeDragonNumber = 43;

enum class RaidStage : uint8_t {
    None = 0,       // no raid: no dragon standing, or it has not landed
    Ground = 1,     // 100 to 70%: bite, breath, roar
    Flight = 2,     // 70 to 40%: on its legs still -- minions, and fire from the sky in lines
    Enraged = 3,    // 40 to 15%: meteors and fire pools
    LastStand = 4,  // 15 to 0%: the Golden Inferno, shadows the only shelter
};

// Where each stage begins, in percent of the dragon's health. Invention (the design's §1).
constexpr int kFlightAt = 70;
constexpr int kSecondWaveAt = 50;
constexpr int kEnragedAt = 40;
constexpr int kLastStandAt = 15;

// **How tough, for how many** (§2). `players` is fixed when it lands; play's is kRaidPlayers.
// The health runs from OpenMU's 22,000 for one to WebZen's 100,000 for ten (WZD Monster.txt:287),
// a straight line between (invention), times kRaidHealthScale -- what the headless tune moves
// (tools/raid), since an end-game party took WZD's ten-player number in a minute. 9.75 since the
// party's weapon runes fire, the dragon shrugs off holds and the raiders pay mana for their
// skills (2026-10-06): 142 of 240 won, the kill about 7:25, the losses its clock.
constexpr int kRaidPlayers = 10;
constexpr int32_t kRaidHealthOne = 22000;
constexpr int32_t kRaidHealthTen = 100000;
constexpr float kRaidHealthScale = 9.75f;
inline int32_t raidHealth(int players) {
    const int n = players < 1 ? 1 : players;
    const double line =
        double(kRaidHealthOne) + double(kRaidHealthTen - kRaidHealthOne) * double(n - 1) / 9.0;
    return int32_t(line * double(kRaidHealthScale));
}
// And its blow: OpenMU's band times this, walked toward WZD's 6,000-8,000 by the tune.
constexpr float kRaidBlowScale = 15.0f;

// ---- the moves, in ticks (20 a second) and tiles. Invention unless a source is named. -------
// Every this often, on the ground, it rears for a Breath or a Roar Shock.
constexpr int64_t kMoveEvery = 12 * 20;
// The Breath: three jets from the mouth at -30, 0 and +30 degrees (MuMain ZzzCharacter.cpp:
// 1939-1948) -- a 60 degree cone, kBreathReach tiles long, told kBreathTell ahead, burning a
// kBreathShare of max health every kBreathEvery while it lasts.
constexpr int64_t kBreathTell = 24;
constexpr int64_t kBreathTicks = 40;
constexpr int64_t kBreathEvery = 5;
constexpr float kBreathReach = 6.0f;
constexpr float kBreathHalfAngle = 0.5235988f;  // 30 degrees
constexpr float kBreathShare = 0.06f;
// The Roar Shock: everyone within kShockReach shocked and shoved a tile out, as the Lightning push
// is -- MuMain's fire landing shocks every character within 200 units (ZzzEffect.cpp:7732-7770);
// the shove is ours.
constexpr int64_t kShockTell = 16;
constexpr float kShockReach = 2.0f;
constexpr float kShockShare = 0.15f;
// **Hellfire**, from the enraged stage on, in the Roar's place when two or more crowd it (the
// user, 2026-10-06: 'dragon also has to usee helfire oon some stages'): MU's wizard's Hellfire
// round it -- the sigil four tiles out and the wall of fire three (fx/hellfire.h,
// ZzzEffect.cpp:2137-2164) -- told kHellfireTell ahead, kHellfireShare of max health to all
// within kHellfireReach. On the dragon it is ours.
constexpr int64_t kHellfireTell = 20;
constexpr float kHellfireReach = 3.0f;
constexpr float kHellfireShare = 0.3f;
// The second stage, on its legs: fire from the sky every kStrafeEvery, kStrafeFires impacts
// along a kStrafeLength line, each kImpactTell after it is let go and kImpactGap after the last.
constexpr int64_t kStrafeEvery = 10 * 20;
constexpr int kStrafeFires = 6;
constexpr float kStrafeLength = 12.0f;
constexpr int64_t kImpactTell = 30;
constexpr int64_t kImpactGap = 4;
constexpr float kImpactReach = 1.0f;
constexpr float kStrafeShare = 0.25f;
// How many minions a wave: 2 + 2 a player, at most kMinionsMost (slots raised at the start) --
// a swarm, the user, 2026-10-06: 'i never saw stage with massive golden budge dragon spawn'.
// And scaled for the party that meets them, as the dragon is: OpenMU's 2,500 health and
// 120-125 bite died to an end-game party in a second. Ours.
constexpr int kMinionsMost = 24;
constexpr float kMinionHealthScale = 12.0f;
constexpr float kMinionBlowScale = 6.0f;
// A wave is summoned, not struck: it halts, drops any blow in hand and stands through its roar
// (Monster32's clip 1, 16 keys at MU's 0.8, about 0.8 s) and a breath after it, so no swing
// lands on the wave (the user, 2026-10-06: 'dragon can do summoning animations (not
// attacking)'). Ours.
constexpr int64_t kSummonTicks = 30;
// **A boss is immune to crowd control** (the user, 2026-10-06: 'bosses has to be immune to CC',
// 'and there has to be damage text immune'): no freeze, chill, push or pull takes the dragon,
// and "Immune" goes up over it at most this often -- ten runed fighters would say it every
// tick. Every wound still lands. Ours.
constexpr int64_t kImmuneSayTicks = 10;
// **What a minion leaves** beside its Zen (the user, 2026-10-06: 'we need also some decent drops
// for small golden budge dragons'), the design's §1: a refining jewel one in kMinionJewelOdds, a
// Rune of Creation one in kMinionRuneOdds, its power drawn for the hero at the dragon's level.
// OpenMU's Box of Luck waits for the boxes (docs/kundun-box.md). Ours.
constexpr int kMinionJewelOdds = 4;
constexpr int kMinionRuneOdds = 10;
constexpr int64_t kSpoilsLingerTicks = 60 * 20;  // a kill's drop's minute (realm_items.cpp)
inline int minionsFor(int players) {
    const int n = 2 + 2 * (players < 1 ? 1 : players);
    return n > kMinionsMost ? kMinionsMost : n;
}
// The enraged swing: its band's clock times this.
constexpr float kEnragedSwing = 0.8f;
// The meteor storm, every kMeteorStormEvery: one rock a living fighter, on the tile he stood on
// kImpactTell before; the Balrog's storm (play_show.cpp) for the look. Each leaves a pool.
constexpr int64_t kMeteorStormEvery = 8 * 20;
constexpr float kMeteorShare = 0.35f;
constexpr int64_t kPoolTicks = 12 * 20;
constexpr int64_t kPoolEvery = 20;
constexpr float kPoolReach = 1.0f;
constexpr float kPoolShare = 0.05f;
// The Golden Inferno, every kInfernoEvery from the last stand: told kInfernoTell, kInfernoShare
// of max health to all within kInfernoReach but those in a wing's shadow (kShadowReach); two
// shadows up to five players, three above (the design's §1).
constexpr int64_t kInfernoEvery = 30 * 20;
constexpr int64_t kInfernoTell = 6 * 20;
constexpr float kInfernoReach = 14.0f;
constexpr float kInfernoShare = 0.9f;
constexpr float kShadowReach = 2.0f;
constexpr float kShadowOut = 4.0f;
// The fight's clock: this long after it lands, not dead, it takes off and is gone kDepartTicks
// later -- the invasion over and the weather clearing (the user, 2026-10-06). Ours.
constexpr int64_t kHardEnrage = 8 * 60 * 20;
constexpr int64_t kDepartTicks = 100;
// What an Inferno without its shadows would be; none is thrown since the dragon leaves instead.
constexpr float kWipeShare = 10.0f;
constexpr float kWipeReach = 64.0f;

// What::Raid's `a`. b and c are its numbers as each says; the happening's x and y its place.
enum class RaidEvent : int32_t {
    Stage = 0,   // b: the RaidStage begun
    Tell = 1,    // b: the Hazard kind told, c: ticks until it lands; x, y its place, whom its aim
    Strike = 2,  // b: the Hazard kind landing now; x, y
    Wave = 3,    // b: how many minions came down
    Aloft = 4,   // b: 1 up, 0 down
    Shadow = 5,  // a shadow laid for an Inferno; x, y
    Raider = 6,  // a raider's own: b: the RaiderAct, c: a skill's number or a potion's worth
    Immune = 7,  // what would hold or move it shrugged off (Realm::shrugs)
};

enum class RaiderAct : int32_t {
    Swing = 0,   // a plain blow or shot at whom
    Cast = 1,    // c: the skill thrown at whom
    Drink = 2,   // c: the health it gave
    Dodge = 3,   // stepping out of a marked tile
};

// One thing laid on the ground by the dragon, told before it lands.
enum class HazardKind : uint8_t { None, Breath, Shock, Impact, Pool, Inferno, Hellfire };

struct Hazard {
    HazardKind kind = HazardKind::None;
    float x = 0.0f, y = 0.0f;   // tiles
    float reach = 0.0f;
    float facing = 0.0f;        // the Breath's
    float share = 0.0f;
    int64_t landsAt = 0;        // when it strikes first
    int64_t endsAt = 0;         // and when it is over
    int64_t nextAt = 0;         // its next pulse, for what burns
    uint32_t serial = 0;        // which volley it is, for the raiders' reactions
    bool pools = false;         // a meteor's: it leaves a burning pool where it lands
    // An Inferno's shelter: the wings' shadows, kShadowReach round each. None is the hard
    // enrage's.
    int shades = 0;
    float shadeX[3] = {}, shadeY[3] = {};
};
constexpr int kHazards = 48;
constexpr int kShadowsMost = 3;

// ---- the party (§2a) ----------------------------------------------------------------------
enum class RaidRole : uint8_t { Tank, Melee, Healer, Archer, Wizard };

// One worn thing in a kit, by the item's name in the cooked tables.
struct KitPiece {
    int slot = -1;                 // sim/items.h's worn slot
    std::string item;              // the row's name
    int refinement = 0;
    bool luck = false;
    int option = 0;
    uint8_t excellent = 0;
    uint8_t sockets = 0;
    uint8_t powers[3] = {0, 0, 0};
};

// One character of the party: class, level, points and what he wears. Read from
// source/raid/party.json by whoever raises a raid (tools/raid, sim_test); the realm only checks
// it is legal (`Realm::kitRefusal`).
struct RaiderKit {
    std::string name;
    RaidRole role = RaidRole::Melee;
    Kin kin = Kin::DarkKnight;
    bool second = false;
    int level = 1;
    HeroPoints points;
    std::vector<KitPiece> pieces;
    // The skills it throws, by MU's number, in its role's priority order.
    std::vector<int32_t> skills;
    int potions = 20;
};

// The raiders' reactions (§2a): a tell is stepped out of after kReactLeast..kReactMost ticks,
// and one tell in kDodgeMissOdds is not stepped out of at all. Invention, and the difficulty's
// two knobs. A potion below kDrinkBelow of max health, one a kDrinkEvery.
constexpr int64_t kReactLeast = 8;
constexpr int64_t kReactMost = 20;
constexpr int kDodgeMissOdds = 8;
constexpr float kDrinkBelow = 0.4f;
constexpr int64_t kDrinkEvery = 20;
// How far a ranged raider stands from what it fights, and a healer's reach.
constexpr float kRangedStand = 6.0f;
// The threat a body holds on the dragon: what it dealt, halved every kThreatHalf ticks; the
// tank's counts kTankThreat times; a new quarry must hold kThreatMargin times the old's.
constexpr int64_t kThreatHalf = 7 * 20;
constexpr float kTankThreat = 3.0f;
constexpr float kThreatMargin = 1.3f;
constexpr int kRaidersMost = 9;

}  // namespace mu::sim
