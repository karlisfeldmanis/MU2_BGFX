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

// Which mark stands beside a section, or beside a row.
// `Ring` and `RingSet` are a socket as Diablo III draws one beside its row (the user's
// reference, 2026-09-28): a round bronze rim, dark and empty, or holding a stone of the mark's
// colour. Drawn larger than the other marks, near the line's own height.
enum class Mark : uint8_t { None, Blade, Shield, Star, Triangle, Diamond, Socket, Note, Ring, RingSet };

// A row: a label and its values, or a free line of prose that runs the width.
struct Row {
    std::string label;
    std::vector<Value> values;
    std::string free;
    Tone freeTone = Tone::White;
    // The affix grammar the item card took from the Diablo IV concept on 2026-09-27: the thing
    // named first and its value last. `keyword` is printed white before the prose ("Luck"),
    // `tail` after it in a brighter, heavier ink of the same tone ("+5%"), and `mark` stands in
    // the gutter beside the row's first line in `markTone` -- the mark says what KIND of line
    // it is (a rolled option, an excellent one), so the text can keep MU's own blue.
    std::string keyword;
    std::string tail;
    Mark mark = Mark::None;
    Tone markTone = Tone::Gray;
};

// The item card's headline: the one number a thing is compared by, large, and the sum that made
// it hung under it on a rail -- "37 - 47 Damage", then "16 - 26 base", "+21 refined to +7".
// MU has no damage per second; what it has is a band built of parts, and the rail says which.
struct Hero {
    std::string value;
    Tone tone = Tone::White;
    std::string word;
    // Against the one worn, as `Value::delta`.
    std::string delta;
    int deltaWay = 0;
    // A rail row is its figure first (`values[0]`, in its tone) and then its words (`label`).
    std::vector<Row> rail;
    bool empty() const { return value.empty(); }
};

// One line of the item card's foot: in its tone, or in the foot's own quiet ink.
struct FootLine {
    std::string text;
    Tone tone = Tone::White;
    bool quiet = false;
};

// ---- the card's own vocabulary, exported ---------------------------------------------------
//
// The container, the inks and the two ways this card sets type, so that anything else drawn in
// the same style uses these numbers rather than a copy of them that drifts. The skill rail above
// the plate is the first other thing (`Hud::setFan`), and the user's rule when they chose it was
// exactly this: *"it has to be the same style"*.
namespace ink {
// The body, graded from its head to its foot, and the iron that is its edge: Sanctuary's ash
// and iron (game/ui/style.h) since 2026-09-28, where it was a cool black under a warm hairline.
// A little see-through since the same day, on the user's word: the world moves behind a card,
// and the words keep their own drop shadow to hold an edge on it.
constexpr uint32_t kBodyTop = gfx::rgba(0.051f, 0.039f, 0.031f, 0.84f);
constexpr uint32_t kBodyFoot = gfx::rgba(0.020f, 0.012f, 0.012f, 0.80f);
constexpr uint32_t kRing = gfx::rgba(0.420f, 0.337f, 0.271f, 1.0f);
constexpr uint32_t kHair = gfx::rgba(1.0f, 1.0f, 1.0f, 0.06f);
constexpr uint32_t kLabel = gfx::rgba(0.769f, 0.757f, 0.706f);
constexpr uint32_t kQuiet = gfx::rgba(0.588f, 0.600f, 0.557f);
constexpr uint32_t kFramed = gfx::rgba(1.0f, 1.0f, 1.0f, 0.03f);
constexpr uint32_t kFrame = gfx::rgba(1.0f, 1.0f, 1.0f, 0.10f);
constexpr uint32_t kPlateEdge = gfx::rgba(1.0f, 1.0f, 1.0f, 0.12f);
constexpr uint32_t kPlateBack = gfx::rgba(0.0f, 0.0f, 0.0f, 0.5f);
// The drop under every letter: type on glass this thin needs its own shadow to hold an edge.
constexpr uint32_t kDrop = gfx::rgba(0.0f, 0.0f, 0.0f, 0.75f);
constexpr float kRadius = 4.0f;  // the corner, in the card's own 1080-line pixels
}  // namespace ink

// The card's unit: one pixel of the design page at 1080 lines.
float unit();

// The shadow on its own, and the graded body on its own: what a window composes when it wants
// its own edge between them (game/ui/sheet.h draws a gradient stroke there). `radius` is in
// pixels for `panel` and in the card's units for `glass`. `shadowUnder`'s is the card's corner in
// pixels, so the cut-out under the glass follows it round.
void shadowUnder(gfx::Canvas& canvas, const gfx::Box& box, float u, float radius = 0.0f);
void panel(gfx::Canvas& canvas, const gfx::Box& box, float radius, uint32_t top, uint32_t foot);
// The same with a radius a corner (top-left, top-right, bottom-right, bottom-left, in pixels):
// what a band laid under a rounded window's head is cut with, so its square corners do not
// stand out past the round ones. `foot` 0 is a fan of one colour.
void rounded(gfx::Canvas& canvas, const gfx::Box& box, const float radius[4], uint32_t top,
             uint32_t foot = 0u);

// The container on its own: the three-falloff shadow, the ring, and the graded body, at `box`.
// What `draw` lays down before it prints anything, and all a rail needs.
//
// `top` and `foot` are the body's two ends, and 0 takes the card's own. A window asks for its
// own pair: the card's are set for a thing read for a second, and a sheet you stand in front of
// wants a heavier glass at its foot (see game/ui/sheet.h).
void glass(gfx::Canvas& canvas, const gfx::Box& box, float u, float radius = ink::kRadius,
           uint32_t top = 0u, uint32_t foot = 0u);

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

// One mark drawn at (cx, cy) at `size` across: the same small figures the card sets beside a
// section, and what the windows' heads carry.
void glyphAt(gfx::Canvas& canvas, Mark which, float cx, float cy, float size, uint32_t colour);

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
    // The bar's ink: White is the lettering, as a quiver's; gear takes its wear band's colour.
    Tone wearTone = Tone::White;
    std::string price;
    Tone priceTone = Tone::Yellow;
    // What it fetches over any counter, as a figure, and MU's Zen coin to set beside it: a strip
    // of its own under the foot, "Sells for" on the left and the coin and the figure on the
    // right, where Diablo IV prints a thing's sell value. Empty for what cannot be sold.
    std::string sell;
    gfx::Art coin;
    // And the foot's left, opposite the price: one short standing fact about the thing, which
    // today is only an orb's "Already learned" (the user, 2026-09-23). It is in the foot and not
    // in a section because it is not something the orb DOES or ASKS -- it is the card's verdict
    // on it, which is what a foot is for, and it wants to sit across from the Zen so the two
    // halves of "should I buy this" are read in one glance. It shares the left with `wear` and
    // stands after the bar where a row somehow has both.
    std::string note;
    Tone noteTone = Tone::Red;
    // How wide the card is drawn, in the windows' own units; 0 takes the item card's 346. A
    // narrower card for a sheet that has no option list and no lore to wrap -- a skill's four
    // lines in a 346-wide card is a page of air with a sentence on it.
    float wide = 0.0f;
    // The item's own picture and where it sits in it: the window's stage, which has already
    // drawn this thing at this size for the bag.
    gfx::Art picture;
    gfx::Box from;

    // **The item card**, concept B of the design page of 2026-09-27 (the Diablo IV tooltip read
    // in MU's inks). Set by `describe`, and nothing else sets it, so a skill's card, the vault's
    // and the wear column's keep the look above. What it changes:
    //   * the head: the name in the window titles' Cinzel, capitals, with the item's own picture
    //     large at the right instead of in a plate; `base` under it in the name's tone;
    //   * `hero` under the head, before any section;
    //   * no kicker is printed -- the marks on the rows say what kind of line each is;
    //   * the foot is two columns: `who` on the left (the class and the requirements), and on
    //     the right the wear, the note, `keep` and the price, one a line.
    bool item = false;
    // How much of the head's picture square the thing is drawn at. The stage fits every model
    // to its square, so a one-cell jewel came out as tall as a three-cell sword -- "the jewel is
    // too huge", the user on first sight. `describe` sets it from the row's cells.
    float artScale = 1.0f;
    Hero hero;
    std::vector<FootLine> who;
    std::vector<FootLine> keep;

    bool empty() const { return name.empty(); }
};

// The picture in the head is rendered on a stage of its own rather than cut out of the window
// under the pointer: the bag turns what is hovered (MU's own feedback), and the head's picture
// has to hold still. `kPlateUnits` is that stage's size in the windows' MU units: 56, twice the
// old plate's, since 2026-09-27, because the item card draws it at 104 px and a 56 px render
// magnified that far is a smear.
constexpr float kPlateUnits = 56.0f;

// Stands `item` alone on the tooltip's stage and hands back what to draw in the head. The
// picture is last frame's, as every stage's is; an item that has just been hovered draws its
// plate empty for one frame.
void stand(Stage& stage, int32_t item, int refinement, Sheet& sheet);

// Draws the card standing on (x, y) and kept on screen. The point is the TOP of what is being
// described -- the hovered item's own cell, not the pointer -- and the card is centred over it
// and lifted clear, so the thing you are reading about is never under the thing describing it.
// MU anchors the same way (NewUIInventoryCtrl.cpp:1513: the cells' centre and top, then up).
//
// Given the whole box of what is described, a card with no room above it drops under the box,
// and one with room for neither stands beside it -- near the top of the screen a card clamped
// down would cover the very thing it describes. The point form is a box of no size.
void draw(gfx::Canvas& canvas, const Sheet& sheet, const gfx::Box& over, float screenWidth,
          float screenHeight);
void draw(gfx::Canvas& canvas, const Sheet& sheet, float x, float y, float screenWidth,
          float screenHeight);

}  // namespace mu::game::tip
