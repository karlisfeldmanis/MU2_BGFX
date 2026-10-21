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
// build first elite in lorencia"); the rest are added here as each is judged.
struct EliteMap {
    uint32_t map;
    int count;
};
constexpr EliteMap kEliteMaps[] = {
    {0, 1},  // Lorencia
};
inline int elitesOn(uint32_t map) {
    for (const EliteMap& row : kEliteMaps) {
        if (row.map == map) return row.count;
    }
    return 0;
}

// What it is against its breed (the proposal of 2026-10-21, to tune in play).
constexpr int kEliteLife = 8;              // health x8
constexpr int kEliteDamage = 2;            // both ends of the damage band x2
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
// 2 to 4 pieces of gear at up to kEliteLevelsUp over its level, luck kEliteLuckIn100, the option
// rolled twice and the better kept; one of them excellent kEliteExcellentIn100 of the time.
constexpr int kEliteLeastPieces = 2;
constexpr int kEliteMostPieces = 4;
constexpr int kEliteLevelsUp = 2;
constexpr int kEliteLuckIn100 = 10;
constexpr int kEliteExcellentIn100 = 25;
// A jewel kEliteJewelIn100 of the time, a second on top kEliteSecondJewelIn100: the Bless, the
// Soul or the Chaos, whatever the map's own drops reach (the Life never).
constexpr int kEliteJewelIn100 = 60;
constexpr int kEliteSecondJewelIn100 = 35;
// And its Zen, straight to the purse as every kill's: kEliteZen times an ordinary kill's.
constexpr int kEliteZen = 6;
// Its things lie longer than a kill's, for the walk back to them after a long fight.
constexpr int64_t kEliteLingerTicks = 120 * 20;

}  // namespace mu::sim
