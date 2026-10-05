// What an item's plus looks like: the level MuMain draws it at and the colour of its chrome.
//
// MuMain's RenderPartObjectEffect (ZzzObject.cpp:9472) does two things before its ladder:
//   * some models are drawn at a level of their own whatever their plus (:9531) -- every jewel
//     at +8, so a jewel always carries the chrome, and the orb of summoning at +0 whatever
//     number of monster it summons;
//   * the chrome's colour is PartObjectColor's (:6536): per model for some weapons and
//     shields, per set index for the armour, and orange for everything else.
// Both are MuMain's Season 6 tables cut to the rows that exist in this game's item table.
// docs/sprints/14-the-shine.md.
#pragma once

#include <string>

#include "content/showing.h"
#include "content/tables.h"
#include "content/texture.h"
#include "gfx/renderer.h"

namespace mu::game {

// Hands the renderer the two sheets out of the showing table: Chrome01 for +7, Shiny01 for
// +9. Without them a refined item takes its tint and nothing more, which is said once.
void lendShine(const content::Showing& table, const std::string& assetDir,
               content::Textures& textures, gfx::Renderer& renderer);

struct ShineLook {
    int level = 0;
    float colour[3] = {1.0f, 0.5f, 0.0f};
    // MuMain's excellent pass on top (ZzzObject.cpp:10492): shaders/shine.sh's
    // shineExcellentAdded, carried to the shader as the level plus kShineExcellent.
    bool excellent = false;
    // At +11 the Chrome02 sweep's colour, PartObjectColor2's number (ZzzObject.cpp:6827):
    // 0 white, 1 orange, 2 blue, 3 white at full. shaders/shine.sh's shineSweepAdded.
    int sweep = 0;
};

// How the three ride the one float the shader is handed: the level (a quiver's doubled plus
// reaches 23), then 32 for excellent, then 64 a sweep colour. Twenty was the flag until +10 and
// +11 arrived (2026-10-05): a +10 quiver is drawn at 21 and read as excellent.
constexpr int kShineExcellent = 32;
constexpr int kShineSweep = 64;
inline int shineLevelOf(int refine) { return refine % kShineExcellent; }
inline int shineFlagsOf(int refine) { return refine - shineLevelOf(refine); }

// The level and colour `row` at `plus` is drawn with, and whether it is excellent.
ShineLook shineOf(const content::ItemRow& row, int plus, bool excellent = false);

// Puts it on a drawable.
inline void wear(const ShineLook& look, gfx::Drawable& drawable) {
    drawable.refine = look.level + (look.excellent ? kShineExcellent : 0) + look.sweep * kShineSweep;
    drawable.refineColour[0] = look.colour[0];
    drawable.refineColour[1] = look.colour[1];
    drawable.refineColour[2] = look.colour[2];
}

}  // namespace mu::game
