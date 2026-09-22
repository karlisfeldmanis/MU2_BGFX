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
