// The windows' skin: the item card's own material, cut for furniture.
//
// Sprint 7 drew the inventory, the character window and the vendor on MU's leather -- `bag_back`
// nine-sliced, the carved crest, the plate with the title across it, `bag_cell` a cell,
// `bag_close` the X. On 2026-09-23 the user asked for the three windows to be dressed like the
// tooltips and the damage figures: *"use the original window which we have before, and just make
// a really nice skin"*. So every rectangle in `Bag`, `Card` and `Shelf` stays exactly where MU
// put it, and what is PAINTED in them is the card's vocabulary (`tip::ink`, `tip::glass`).
//
// Everything that differs from the card differs because a window is looked at for a minute and a
// card for a second, and each says so where it is:
//
//   * **The body is heavier.** The card is 0.95 to 0.78; a window at 0.78 over Lorencia's grass
//     cannot be read and at 0.95 it is a slab. 0.91 settling to 0.82.
//   * **The head is a kicker and a rule**, not a plate: a mark, tracked caps, a hairline under
//     them, and the close drawn as a cross. No art file is asked for by any of it.
//   * **A cell is a hairline well**, not a gold-rimmed frame: the picture in it is the thing to
//     look at, and MU's rim competes with every item it holds.
//
// The measurements here are in SCREEN PIXELS, not units: the caller has already put its own
// rectangle through `panel::scaled`, which is what keeps MU's tables untouched.
#pragma once

#include <string>

#include "game/ui/tip.h"
#include "gfx/interface.h"

namespace mu::game::sheet {

// ---- the material --------------------------------------------------------------------------

// The window's glass: the card's three falloffs of shadow under it, a nearly flat body, and a
// GRADIENT STROKE for its edge -- bright along the top, fading down the sides to almost nothing
// at the foot, which is the one thing that makes a flat panel read as lit rather than as a
// rectangle. The user's list, 2026-09-23: drop shadows, gradient stroke, clean UI.
// `radius` is in screen pixels.
void glass(gfx::Canvas& canvas, const gfx::Box& window, float radius);

// A band of light laid under a head or over a foot: white at a few thousandths, fading to
// nothing across `tall`. What separates the title from the body without a second rule.
// `radius` is the window's corner in pixels, which the band's outer two corners are cut to.
void band(gfx::Canvas& canvas, const gfx::Box& box, bool downward = true, float radius = 0.0f);

// A hairline across `wide` from (x, y), fading out at both ends as the card's rails do.
void rule(gfx::Canvas& canvas, float x, float y, float wide, float thick);

// A framed well -- 3% white under a 10% hairline -- which is what the leather's nine-sliced
// field becomes. `tip::ink::kFramed` and `kFrame`, the same two the card frames a block with.
void well(gfx::Canvas& canvas, const gfx::Box& box, float thick);

// ---- the parts -----------------------------------------------------------------------------

// What a cell is saying. `Fits` and `Blocked` are the drop target's two answers, asked of the
// same gate the realm refuses by, so a red cell and a refusal cannot disagree.
enum class Cell : uint8_t { Rest, Over, Held, Fits, Blocked };
void cell(gfx::Canvas& canvas, const gfx::Box& box, Cell state, float thick);
// A whole grid of resting cells as one ruled block: `columns` by `rows` across `box`, with a
// single hairline between neighbours and no air. What the bag and the shelf are drawn as.
void grid(gfx::Canvas& canvas, const gfx::Box& box, int columns, int rows, float thick);

// The cross that closes a window, in its three lights.
void close(gfx::Canvas& canvas, const gfx::Box& box, bool over, bool pressed);

// The button that spends a point: a gold ring with a plus, the close button's shape. MU's
// `+` without MU's button art; the name is from when it was a diamond.
void diamond(gfx::Canvas& canvas, const gfx::Box& box, bool over, bool pressed);

// A bar the card's own way: a dark well with a lit fill, for the experience.
void bar(gfx::Canvas& canvas, const gfx::Box& box, float share, uint32_t ink, float thick);

// ---- type ----------------------------------------------------------------------------------

constexpr float kKickerTrack = 0.16f;
constexpr float kTitleTrack = 0.18f;

// A tracked heading in caps, and where it ends. `size` is in pixels.
float kicker(gfx::Canvas& canvas, float x, float baseline, float size, const std::string& text,
             uint32_t ink = tip::ink::kQuiet, float track = kKickerTrack);
// A line of type over its own drop, and the same ranged right in a box.
void printed(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink,
             const std::string& text);
void ranged(gfx::Canvas& canvas, float right, float baseline, float size, uint32_t ink,
            const std::string& text);

// The same text in capitals, for a kicker taken from a name.
std::string shouted(const std::string& text);

// A price in a cell's width: exact under ten thousand, then 13.5K and 1.4M. The card under the
// pointer carries the figure in full, which is where it is read when it matters.
std::string compactZen(long long zen);

// ---- inks ------------------------------------------------------------------------------------
//
// Named here rather than in each window, so the three cannot drift apart.
namespace ink {
// Gold, which is skin B's own choice: A set the title in bone and B lifts it.
inline constexpr uint32_t kTitle = gfx::rgba(0.941f, 0.847f, 0.604f);
inline constexpr uint32_t kLabel = tip::ink::kLabel;
inline constexpr uint32_t kQuiet = tip::ink::kQuiet;
inline constexpr uint32_t kFigure = gfx::rgba(0.953f, 0.937f, 0.882f);
inline constexpr uint32_t kGold = gfx::rgba(1.0f, 0.831f, 0.353f);
inline constexpr uint32_t kMark = gfx::rgba(0.878f, 0.741f, 0.365f, 0.95f);
}  // namespace ink

}  // namespace mu::game::sheet
