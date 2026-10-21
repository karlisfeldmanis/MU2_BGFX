#pragma once

// Elites: one of a map's own monsters, promoted -- harder, harder-hitting, chromed, and worth a
// detour (the user, 2026-10-21: "new monster type for each map - its called Elite ... special
// chrome effect, they are much harder to kill, they do more dmg and the drop decent loot, no
// timers, its not a event, its just a spawn which just is there"). **Ours entirely**: MU has no
// such thing, and its own "Elite Bull Fighter", "Elite Yeti" and "Elite Goblin" are plain breeds
// named so, which is why the tier is a tag on the name plate and never a word in the name.
//
// The realm's part (realm_elite.cpp): a body a slot raised after everything else on the map, so
// no id before it moves; at each rise a breed and a tile drawn again from the map's own nests,
// off its own dice, so a seeded hunt elsewhere rolls as it did. It takes no crowd control (the
// user: "no cc is good idea"), and leaves its own spoils: excellent gear yes, legendary runes
// never (the user: "dont drop legenedary runes from elites but excelent drops is good idea").
// The drawing's part: the chrome, its size, the ELITE tag, and a pulsing gold mark on the minimap
// with its bell (game/ui/minimap.cpp) -- the only alert there is.

#include <cstdint>

namespace mu::sim {

// How many stand on each map, by MU's map number. Lorencia's first (the user, 2026-10-21: "lets
// build first elite in lorencia"), Devias's the same day, then every other field map ("lets make
// elites on all maps"). Never the events' maps (Blood Castle, Devil Square): raiseElites skips them.
// And its life against its breed's, by map (the user, 2026-10-21: "do life multiplier per map"):
// the strong maps' breeds have tens of times the weak ones' life already, and one x8 for all made
// Devias's Elite Yeti 9,600 and Aida's riders 1,320,000 -- the first one a level-400 knight did
// not kill in 40 s. High where the breeds are weak, low where they are already huge. Each
// doubled on 2026-10-21 (the user: "elite monsters has to be stronger, more hp and better exc
// drops").
struct EliteMap {
    uint32_t map;
    int count;
    float life;
};
constexpr EliteMap kEliteMaps[] = {
    {0, 1, 16.0f},  // Lorencia: the Lich 255 -> 4,080
    {3, 1, 12.0f},  // Noria: the Forest Monster 295 -> 3,540
    {2, 2, 6.0f},   // Devias (the user, 2026-10-21: "show some devias kill"): the Elite Yeti 1,200 -> 7,200
    {1, 2, 6.0f},   // Dungeon: the Poison Bull 2,500 -> 15,000
    {4, 2, 5.0f},   // Lost Tower: the Devil 5,000 -> 25,000
    {7, 2, 5.0f},   // Atlans: the Lizard King 9,000 -> 45,000
    {8, 2, 4.0f},   // Tarkan: the Beam Knight 25,000 -> 100,000
    {10, 1, 4.0f},  // Icarus: the Phantom Knight 41,000 -> 164,000
    {33, 2, 3.0f},  // Aida: the Bloody Death Rider 165,000 -> 495,000
};
// The bosses, which are never made Elites: an Elite is an ordinary breed made strong. MU's
// numbers: the Gorgon, the Ice Queen, the Balrog, the Hydra, Zaikan, the Death Beam Knight, the
// Dark Phoenix, the Witch Queen and Hell Maine.
constexpr int32_t kNeverElite[] = {18, 25, 38, 49, 59, 63, 77, 304, 309};
inline bool mayBeElite(int32_t number) {
    for (int32_t one : kNeverElite) {
        if (one == number) return false;
    }
    return true;
}
// Only the map's strongest ordinary breeds are made Elites: its top kEliteStrongest by level.
constexpr int kEliteStrongest = 3;
inline int elitesOn(uint32_t map) {
    for (const EliteMap& row : kEliteMaps) {
        if (row.map == map) return row.count;
    }
    return 0;
}
// Its life against its breed's on that map, 1 on a map with none.
inline float eliteLifeOn(uint32_t map) {
    for (const EliteMap& row : kEliteMaps) {
        if (row.map == map) return row.life;
    }
    return 1.0f;
}
// An Elite's life of a breed's on that map, rounded.
inline int32_t eliteHealth(int32_t health, uint32_t map) {
    return int32_t(double(health) * double(eliteLifeOn(map)) + 0.5);
}

// What it is against its breed (the proposal of 2026-10-21, to tune in play).
constexpr float kEliteDamage = 2.5f;       // both ends of the damage band x2.5 (x2 until 2026-10-21)
constexpr float kEliteDefense = 1.5f;      // defence x1.5
constexpr float kEliteSwing = 1.15f;       // swings 15% quicker
constexpr float kEliteStride = 1.1f;       // walks 10% quicker
constexpr int kEliteExperience = 10;       // the kill's experience x10

// Back 20 to 40 minutes after it falls, drawn at the fall, a new breed in a new place; nothing
// counts down on the screen.
constexpr int64_t kEliteRiseLeastTicks = 20 * 60 * 20;
constexpr int64_t kEliteRiseMostTicks = 40 * 60 * 20;

// Never within this many tiles of a gate, nor on a safe tile: found in the field, not at the door.
constexpr int kEliteGateClear = 15;

// Its spoils (Realm::eliteSpoils), the drop's whole -- the ordinary leave() is not rolled.
// 4 to 7 pieces of gear (2 to 4 until the user's "we need more items droped", 2026-10-21) at up
// to kEliteLevelsUp over its level, luck kEliteLuckIn100, the option rolled twice and the better
// kept; one of them always excellent, and a second kEliteSecondExcellentIn100 of the time (one in
// four had one until the user's "better exc drops", 2026-10-21).
constexpr int kEliteLeastPieces = 4;
constexpr int kEliteMostPieces = 7;
constexpr int kEliteLevelsUp = 2;
constexpr int kEliteLuckIn100 = 10;
constexpr int kEliteSecondExcellentIn100 = 35;
// A jewel kEliteJewelIn100 of the time (always, since the same day), a second on top
// kEliteSecondJewelIn100: the Bless, the Soul or the Chaos (the Life never).
constexpr int kEliteJewelIn100 = 100;
constexpr int kEliteSecondJewelIn100 = 50;
// And its Zen, straight to the purse as every kill's: kEliteZen times an ordinary kill's.
constexpr int kEliteZen = 6;
// Its things lie longer than a kill's, for the walk back to them after a long fight.
constexpr int64_t kEliteLingerTicks = 120 * 20;

}  // namespace mu::sim
