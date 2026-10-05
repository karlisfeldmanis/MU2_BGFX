#pragma once

// Blood Castle's run: when the Messenger lets him in, how long it runs, and the castle's grid
// boxes that the run opens. docs/blood-castle-port.md,
// Part A section 4.3. Castle 1 only for now; castles 2-7 share the terrain (MapManager.cpp:
// 1104-1106) and differ only in their monsters.

#include <cstdint>

#include "content/grid.h"

namespace mu::sim {

// MU's map number for castle 1 (WD_11BLOODCASTLE1). The realm keeps its own copy of this map's
// grid, which the run changes as it goes.
constexpr uint32_t kBloodCastleMap = 11;

// **When it opens** (the user, 2026-10-02: 'every 1 hour BC is opened'): WebZen's hourly default.
// The Messenger of Archangel in Devias lets a ticket holder in from hh:25 for five minutes
// (BloodCastle.cpp:778-802, entry closed at hh:30, :1128-1171), on the local wall clock.
// TEST CLOCK (the user, 2026-10-04: 'reduce time to 1 minute so we can test it'): every two
// minutes, open the second one, so the wait is a minute at most. Back to 3600, 25 * 60, 5 * 60.
constexpr int kCastlePeriod = 120;       // seconds between openings
constexpr int kCastleOpensAt = 60;       // into the hour
constexpr int kCastleEntry = 60;         // how long the Messenger lets him in
// **The run, on his own clock from the moment he is in** (the user: 'when you are in there is
// timer and BC starts'). Ours that it starts on entry; WebZen starts every castle together at
// hh:31. Its numbers are WebZen's: 60 s in the safe court ("the quest starts in 60 s", lMsg
// 1163), then 15 minutes of play (BloodCastle.cpp:887-917; the client's SetMatchInfo(15 * 60),
// MuMain NewBloodCastleSystem.cpp:46, 73). OpenMU's is 20. **Ten minutes, ours** (the user,
// 2026-10-05: 'decrease BC duration to 10 minutes').
constexpr int kCastleWait = 60;
constexpr int kCastleRun = 10 * 60;

// Seconds the Messenger has left to let him in, at `daySeconds` into the local day, or 0 when
// the door is shut.
constexpr int castleEntryLeft(int daySeconds) {
    const int phase = ((daySeconds - kCastleOpensAt) % kCastlePeriod + kCastlePeriod) % kCastlePeriod;
    return phase < kCastleEntry ? kCastleEntry - phase : 0;
}

// **The Messenger's door** (WebZen NpcTalk.cpp:1655-1753, protocol.cpp:19629-~20030): spoken to
// with an Invisibility Cloak while the entry is open, he takes the cloak and sends him to exit gate
// 66, the castle's safe court (gObjMoveGate(GATE_BLOODCASTLE_1)). MU has no enter gate for it;
// kCastleEnterGate is ours, a row with no box, so the map change takes the gates' road
// (sim/gates.cpp, Realm::passGate). The cloak's level is the castle; castle 1 asks levels 15 to
// 80 (BloodCastle.h:105-120, CheckEnterLevel :1214-1267).
constexpr int32_t kCastleGate = 66;
constexpr int32_t kCastleEnterGate = 1066;
// The castles a cloak can be made for at 0.97d (+1 to +6, sim/machine.h kCloakMostLevel), each
// with its band (BloodCastle.h:105-120; the sixth "281-MAX", 0 here for no ceiling), and how many
// are built: all six since 2026-10-03, each castle's breeds on castle 1's nests (kCastleBreeds,
// Realm::setCastle). The Messenger's page steps through all six (the user, 2026-10-03: 'allow to
// choose BC levels with arrows sismilair like we show active quests').
constexpr int kCastles = 6;
constexpr int kCastlesBuilt = 6;
constexpr int kCastleBands[kCastles][2] = {{15, 80},   {81, 130},  {131, 180},
                                           {181, 230}, {231, 280}, {281, 0}};
// Only the floor is asked since 2026-10-04 (ours: any castle at or below his band lets him in,
// Realm::castleRefusal); the ceilings stay to choose the page he opens on.
// The castle his level's band holds, 1 to kCastles: the page the Messenger opens on.
constexpr int castleFor(int level) {
    for (int c = 0; c < kCastles; ++c) {
        if (level <= kCastleBands[c][1] || kCastleBands[c][1] == 0) return c + 1;
    }
    return kCastles;
}
// Why he did not let him in, carried in Shout::Greet's b (0 is the old "not ready").
enum class CastleRefusal : int32_t {
    None = 0,
    NoCloak = 1,    // ServerCmd 1,21
    NotYet = 2,     // ServerCmd 1,20: a cloak, outside the entry window
    TooLow = 3,     // result 4
    TooHigh = 4,    // result 3; never said since 2026-10-04, the floor only
    NotBuilt = 5,   // ours: a castle past kCastlesBuilt
};

// **The run** (docs/blood-castle-port.md §5; the user, 2026-10-03: 'lets test scnearou that
// character is already in there is timer to start BC, gates are locked'). Raised on the castle,
// he waits kCastleWait in the safe court with the entrance shut, then the run's kCastleRun
// starts and the entrance opens. WebZen's quotas for one player (SetMonsterKillCount,
// gObjMonster.cpp:1238-1341): 40 kills of anything but the Spirit Sorcerer, then 2 of those.
// **A hundred kills, ours** (the user, 2026-10-05: 'we need increase amount of monsters which has
// to be killed in BC'); the garrison rises again, so the quota is always there to be met.
enum class CastlePhase : uint8_t { None, Waiting, Running, Ended, Won };
constexpr int64_t kCastleTicksPerSecond = 20;  // the realm's tick, MU2's Realm.Hz
constexpr int kCastleKills = 100;
constexpr int kCastleSorcerers = 2;
// How long the drawbridge takes to come down: 1.18 s, landing on eDownGate's thud (game/world/
// drawbridge.h; ours, timed to the sound), 24 ticks. MuMain's ActionObject swings it over 21 of
// its 25 Hz frames and clears the gap's NoGround on the last (ZzzObject.cpp:86-165); WebZen
// clears the server's gap at once and the client's 3 s later (gObjMonster.cpp:1275-1316). Here
// it opens as the door lands, so nobody walks onto a gap still drawn open.
constexpr int kCastleBridgeTicks = 24;
constexpr int32_t kCastleSorcerer = 89;  // the Magic Skeleton, WebZen's Spirit Sorcerer
constexpr int32_t kCastleStatue = 132;   // the Statue of Saint, raised by the run
// Each castle's six breeds, in castle 1's order -- Chief Skeleton Warrior, Archer, Dark Skull
// Soldier, Giant Ogre, Red Skeleton Knight, Magic Skeleton (the Spirit Sorcerer) -- on the same
// nests (MonsterSetBase.txt, maps 11-16; MuMain _enum.h MONSTER_*_1.._6). A castle 2-6 run
// raises castle 1's nests and swaps each body for its castle's own (Realm::setCastle).
constexpr int32_t kCastleBreeds[6][6] = {
    {84, 85, 86, 87, 88, 89},     {90, 91, 92, 93, 94, 95},     {96, 97, 98, 99, 111, 112},
    {113, 114, 115, 116, 117, 118}, {119, 120, 121, 122, 123, 124}, {125, 126, 127, 128, 129, 130},
};
// The three Statues of Saint, one raised at random each run (BC_SAINT_STATUE_1 + rand()%3,
// BloodCastle.cpp:2216), each over its own Archangel weapon (ZzzCharacter.cpp:8952-8966): the
// Divine Staff (5,10), Sword (0,19) and Crossbow (4,18) of Archangel.
constexpr int32_t kCastleStatues[3] = {132, 133, 134};
constexpr int kArchangelWeapons[3][2] = {{5, 10}, {0, 19}, {4, 18}};
// Each castle's statue health (BloodCastle.dat, "Castle Door Health": 65,000 for castle 1 up
// to 265,000 for castle 6).
constexpr int32_t kCastleStatueHealth[6] = {65000, 105000, 145000, 185000, 225000, 265000};
inline bool castleSorcerer(int32_t number) {
    for (const auto& one : kCastleBreeds) {
        if (one[5] == number) return true;
    }
    return false;
}
inline bool castleStatue(int32_t number) { return number >= 132 && number <= 134; }
// And runes by the castle (the user, 2026-10-04: 'i thin we should give some runes also based of
// BC level'): this many Runes of Creation, each with a power his class may set, drawn as a drop
// of a monster of kCastleRuneLevel draws one (sim::drawRunePower) -- castle 1 reaches Rare only,
// castles 2-3 Epic, castles 4-6 Legendary (kRuneRarityLevel). Into the bag with the jewels.
// invention.
constexpr int kCastleMostRunes = 2;
constexpr int kCastleRunes[6] = {1, 1, 1, 1, 2, 2};
constexpr int kCastleRuneLevel[6] = {15, 40, 40, 60, 60, 60};
struct CastleRun {
    CastlePhase phase = CastlePhase::None;
    int castle = 1;
    int64_t startsAt = 0;  // the tick the wait ends and the run starts
    int64_t endsAt = 0;    // the tick the run's time is up
    int kills = 0;         // quota 1
    int sorcerers = 0;     // quota 2
    bool statueBroken = false;  // by him: the statue's bonus
    // Quota 1 met: the tick the drawbridge starts down, or -1; and whether it is down, its gap
    // walkable (Realm::castleTick, kCastleBridgeTicks after).
    int64_t bridgeAt = -1;
    bool bridgeDown = false;
    // The run over, won or out of time: the tick he is sent back to Devias (kCastleRest after),
    // or -1; and `sentOut` once it has passed, for the mode to take him (Play::takeHome).
    int64_t leavesAt = -1;
    bool sentOut = false;
    // What the win paid, for the Archangel's page (Realm::handInStaff).
    int64_t paidExperience = 0, paidZen = 0;
    uint8_t paidRunes[kCastleMostRunes] = {};  // each rune's power, 0 past kCastleRunes
};
// The Archangel, NPC 232, who takes the Divine Staff of Archangel (5,10) back: the win (the
// user, 2026-10-03). His page in the Event window (QuestDialog::kArchangel).
constexpr int kArchangel = 232;
constexpr int kDivineStaffGroup = 5, kDivineStaffNumber = 10;
// WebZen's win for castle 1, one player (GiveReward_Win, BloodCastle.cpp:3100-3577;
// BloodCastle.h:192-205, 238-252, 308-322): 20,000 for the statue he broke, 5,000 for the
// weapon handed in, 160 a second left on the clock; 20,000 Zen; a Jewel of Chaos at his feet.
constexpr int64_t kCastleStatueExp = 20000;
constexpr int64_t kCastleHandInExp = 5000;
constexpr int64_t kCastleExpPerSecond = 160;
constexpr int64_t kCastleWinZen = 20000;
// And every castle's, castle 1 first: g_iBC_Add_Exp's statue, quest and per-second columns
// (BloodCastle.h:308-322, the table without the 2008 schedule update), g_iQuestWinExpendZEN's
// winner column (:192-205), and BloodCastle.dat's "Reward Items", each laid at his feet.
constexpr int64_t kCastleStatueExps[6] = {20000, 50000, 80000, 90000, 100000, 110000};
constexpr int64_t kCastleHandInExps[6] = {5000, 10000, 15000, 20000, 25000, 30000};
constexpr int64_t kCastleExpPerSeconds[6] = {160, 180, 200, 220, 240, 260};
constexpr int64_t kCastleWinZens[6] = {20000, 50000, 100000, 150000, 200000, 250000};
// Jewels of Chaos (12,15), Soul (14,14), Bless (14,13) and Life (14,16); a group of -1 ends a
// castle's list. WebZen's castles 2-5 also pay a Jewel of Creation (14,22), here the Rune of
// Creation, which with no power sets into nothing: kCastleRunes pays it with one instead.
constexpr int kCastleRewardJewels[6][4][2] = {
    {{12, 15}, {-1, -1}, {-1, -1}, {-1, -1}},
    {{12, 15}, {-1, -1}, {-1, -1}, {-1, -1}},
    {{12, 15}, {-1, -1}, {-1, -1}, {-1, -1}},
    {{12, 15}, {14, 14}, {-1, -1}, {-1, -1}},
    {{12, 15}, {14, 14}, {-1, -1}, {-1, -1}},
    {{12, 15}, {14, 14}, {14, 13}, {14, 16}},
};
// After the run, won or timed out, WebZen's PLAYEND rest: a minute, then everyone left in the
// castle is moved to Devias, gate 22 (BloodCastle.cpp:1066-1077; docs/blood-castle-port.md).
constexpr int kCastleRest = 60;
// What the Archangel's page shows (QuestDialog::kArchangel).
enum class AngelState : uint8_t { NotYet, NoStaff, Ready, Done };

// A rectangle of tiles, inclusive, and the attribute bits the run clears from it.
struct GridBox {
    int x1, y1, x2, y2;
    uint16_t bits;
};

// WebZen BloodCastle.h:128-173 (the courtyard and altar from row 80; OpenMU's 78 is castle wall
// in every grid), cleared at BloodCastle.cpp:2506-2625. The entrance at the start, the
// drawbridge's gap at the first quota, the door with the courtyard and altar behind it when the
// Castle Gate dies.
constexpr GridBox kCastleEntrance = {13, 15, 15, 23, content::kNoMove};
constexpr GridBox kCastleBridge = {13, 70, 15, 75, content::kNoGround};
constexpr GridBox kCastleDoor[3] = {
    {13, 76, 15, 79, content::kNoMove},
    {11, 80, 25, 89, content::kNoMove},
    {8, 80, 10, 83, content::kNoMove},
};

}  // namespace mu::sim
