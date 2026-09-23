// The item tooltip as a card: the thing's own picture and name in a head, then a rail a
// section -- what it does, its options, what it asks of you -- and a quiet foot for wear and
// price.
//
// MU draws this as one centred stack of single lines, sectioned by blank half-lines and
// coloured by meaning (`RenderItemInfo` / `RenderTipTextList`, MuMain ZzzInventory.cpp:305 and
// :2113). That stack is kept as the VOCABULARY -- blue options, red shortfalls, yellow +7 and
// jewels, green excellent -- and dropped as the LAYOUT: a label on the left and its value on
// the right, so two things compare by eye. The user chose this on 2026-09-22 from a design
// page of four, and asked for the item's own picture in the head.
//
// Two rules from that page, both of them fixes for MU's own tooltip:
//   * one row a label. MU prints Luck twice and a line per class, which reads as a stutter;
//     here they share a row and the values sit under each other.
//   * a fixed width, so lore and long option names wrap. MU has no wrapping at all, which is
//     why a full-option item's tooltip is as wide as the screen.
//
// The sections are named after what they are for, not after what the game has today: most of
// what MU can print (luck, skill, the additional option, excellent options, harmony, ancient
// bonuses, set and socket blocks) has no rules here yet, and a section with nothing in it is
// simply not drawn. The catalogue of every line MU prints, with its era, is in
// docs/mu-tooltip-lines.md.
#pragma once

#include <string>
#include <vector>

#include "game/ui/stage.h"
#include "gfx/interface.h"

namespace mu::game::tip {

// What a value is drawn in. MU's TEXT_COLOR_* set, by the names its source uses.
enum class Tone : uint8_t { White, Blue, Red, Yellow, Green, Gray, Violet, RedPurple, Orange };
uint32_t colourOf(Tone tone);

// One value on a row. `chip` boxes it -- a class you may be, or may not.
struct Value {
    std::string text;
    Tone tone = Tone::White;
    bool chip = false;
    // What this is against the one worn, as MU2's own comparison: the text, and which way.
    std::string delta;
    int deltaWay = 0;  // +1 better, -1 worse, 0 the same
};

// A row: a label and its values, or a free line of prose that runs the width.
struct Row {
    std::string label;
    std::vector<Value> values;
    std::string free;
    Tone freeTone = Tone::White;
};

// ---- the card's own vocabulary, exported ---------------------------------------------------
//
// The container, the inks and the two ways this card sets type, so that anything else drawn in
// the same style uses these numbers rather than a copy of them that drifts. The skill rail above
// the plate is the first other thing (`Hud::setFan`), and the user's rule when they chose it was
// exactly this: *"it has to be the same style"*.
namespace ink {
// The body, graded from its head to its foot, and the warm hairline ring that is its edge.
constexpr uint32_t kBodyTop = gfx::rgba(0.008f, 0.009f, 0.012f, 0.95f);
constexpr uint32_t kBodyFoot = gfx::rgba(0.002f, 0.002f, 0.004f, 0.78f);
constexpr uint32_t kRing = gfx::rgba(0.627f, 0.549f, 0.373f, 0.32f);
constexpr uint32_t kHair = gfx::rgba(1.0f, 1.0f, 1.0f, 0.06f);
constexpr uint32_t kLabel = gfx::rgba(0.769f, 0.757f, 0.706f);
constexpr uint32_t kQuiet = gfx::rgba(0.588f, 0.600f, 0.557f);
constexpr uint32_t kFramed = gfx::rgba(1.0f, 1.0f, 1.0f, 0.03f);
constexpr uint32_t kFrame = gfx::rgba(1.0f, 1.0f, 1.0f, 0.10f);
constexpr uint32_t kPlateEdge = gfx::rgba(1.0f, 1.0f, 1.0f, 0.12f);
constexpr uint32_t kPlateBack = gfx::rgba(0.0f, 0.0f, 0.0f, 0.5f);
// The drop under every letter: type on glass this thin needs its own shadow to hold an edge.
constexpr uint32_t kDrop = gfx::rgba(0.0f, 0.0f, 0.0f, 0.75f);
constexpr float kRadius = 7.0f;  // the corner, in the card's own 1080-line pixels
}  // namespace ink

// The card's unit: one pixel of the design page at 1080 lines.
float unit();

// The container on its own: the three-falloff shadow, the ring, and the graded body, at `box`.
// What `draw` lays down before it prints anything, and all a rail needs.
void glass(gfx::Canvas& canvas, const gfx::Box& box, float u, float radius = ink::kRadius);

// A line of type with its own drop shadow, and the same with CSS's letter-spacing -- `track` ems
// after every letter, drawn a glyph at a time because the canvas advances by the face alone.
float printed(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t colour,
              const std::string& s, float drop);
float trackedWidth(const gfx::Face& face, float size, float track, const std::string& s);
void tracked(gfx::Canvas& canvas, float x, float baseline, float size, float track,
             uint32_t colour, const std::string& s, float drop);
// Where a line of `size` sits to be centred in a box `tall` high from `top`: the half-leading
// above it and the same below, which is what `line-height` means.
float middle(const gfx::Face& face, float top, float tall, float size);

// Which mark stands beside a section.
enum class Mark : uint8_t { None, Blade, Shield, Star, Triangle, Diamond, Socket, Note };

struct Section {
    std::string kicker;  // empty draws no heading
    Mark mark = Mark::None;
    bool framed = false;  // a sub-panel, as MU frames its set and socket blocks
    std::vector<Row> rows;
};

struct Sheet {
    std::string name;
    Tone nameTone = Tone::White;
    std::string base;  // "TWO-HANDED SWORD · DARK KNIGHT"
    std::vector<Section> sections;
    // The foot. `wear` is drawn with a bar; either may be empty.
    std::string wear;
    float worn = 1.0f;  // how much of it is left, 0 to 1
    std::string price;
    Tone priceTone = Tone::Yellow;
    // How wide the card is drawn, in the windows' own units; 0 takes the item card's 346. A
    // narrower card for a sheet that has no option list and no lore to wrap -- a skill's four
    // lines in a 346-wide card is a page of air with a sentence on it.
    float wide = 0.0f;
    // The item's own picture and where it sits in it: the window's stage, which has already
    // drawn this thing at this size for the bag.
    gfx::Art picture;
    gfx::Box from;

    bool empty() const { return name.empty(); }
};

// The picture in the head is rendered on a stage of its own rather than cut out of the window
// under the pointer: the bag turns what is hovered (MU's own feedback), and the head's picture
// has to hold still. `kPlateUnits` is that stage's size in the windows' MU units, which is the
// plate's 56 px at 1080 lines.
constexpr float kPlateUnits = 28.0f;

// Stands `item` alone on the tooltip's stage and hands back what to draw in the head. The
// picture is last frame's, as every stage's is; an item that has just been hovered draws its
// plate empty for one frame.
void stand(Stage& stage, int32_t item, int refinement, Sheet& sheet);

// Draws the card standing on (x, y) and kept on screen. The point is the TOP of what is being
// described -- the hovered item's own cell, not the pointer -- and the card is centred over it
// and lifted clear, so the thing you are reading about is never under the thing describing it.
// MU anchors the same way (NewUIInventoryCtrl.cpp:1513: the cells' centre and top, then up).
void draw(gfx::Canvas& canvas, const Sheet& sheet, float x, float y, float screenWidth,
          float screenHeight);

}  // namespace mu::game::tip
