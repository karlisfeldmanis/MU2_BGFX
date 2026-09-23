// What the windows share: the art by its index key, the two coordinate frames, the frame every
// right-hand panel is drawn in, and the tooltip.
//
// MU2's `client/core/Panel.cs`, number for number. Two arrangements and they are different
// functions on purpose, as they are there:
//   * the HUD is placed in MU's 640x480, scaled uniformly by the window's height and anchored
//     to the bottom centre (`Panel.Screen`);
//   * the panels are MU's 190x429 at a fixed twice (`Panel.Scale`), pinned to the right edge in
//     flush columns (`Panel.ColumnX`) -- which is what MU does with them.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "content/texture.h"
#include "game/ui/sheet.h"
#include "gfx/interface.h"

namespace mu::game::panel {

// ---- the art -------------------------------------------------------------------------------

// Interface art by the index's own key -- `bag_back`, `hud_gem_life` -- as `Panel.Art` looks it
// up: `index.json`'s effects table names the file. Loaded on first ask and kept, a miss kept too
// so a missing picture is looked for once and not once a frame.
class Arts {
public:
    bool open(const std::string& assetDir, content::Textures* textures);
    const gfx::Art& get(const std::string& key);
    // Every piece in the index, read at once. Called from the preloader (Desk::open), because
    // "on first ask" for a window that opens mid-play is a first ask of twelve slot pictures in
    // the frame a merchant's counter opens, and that is a hitch nobody can attribute. The whole
    // interface is a couple of megabytes and all of it is asked for eventually. Returns how many
    // it read.
    int warm();

private:
    std::string assetDir_;
    content::Textures* textures_ = nullptr;
    std::unordered_map<std::string, std::string> paths_;
    std::unordered_map<std::string, gfx::Art> loaded_;
};

// ---- the two frames ------------------------------------------------------------------------

// MU's interface, which the HUD is measured against.
constexpr float kReferenceWidth = 640.0f;
constexpr float kReferenceHeight = 480.0f;

// Where MU's 640x480 sits in a window: uniform scale, centred, bottom-anchored. A point p in the
// original's coordinates is at origin + p * scale.
struct Screen {
    float originX = 0.0f, originY = 0.0f, scale = 1.0f;
    gfx::Box of(const gfx::Box& units) const {
        return {originX + units.x * scale, originY + units.y * scale, units.w * scale,
                units.h * scale};
    }
};
Screen screenOf(float width, float height);

// The panels: MU's window at twice its size at 1080 lines, which keeps every number as the
// original wrote it. MU2 fixes the two in viewport pixels; this scales it with the screen's
// height instead, because this Mac's backbuffer is 1894 lines and a fixed two there draws the
// window at 45% of the screen's height where MU2's 1080 drew it at 79%. A finding, recorded in
// docs/sprints/07-the-windows.md; at 1080 it is MU2's number exactly.
void setScreen(float height);
float scale();
constexpr float kWidth = 190.0f;
constexpr float kHeight = 429.0f;
constexpr float kRightMargin = 24.0f;
// The air between two open windows. MU butts its columns flush, which worked while every window
// was a slab of leather with its own carved border; two hairline-edged panels flush against each
// other read as one panel with a seam, so the skin puts six units between them.
constexpr float kColumnGap = 6.0f;
// Where a panel in the n-th column from the right begins, and its top: centred down the screen.
// MU moves the INVENTORY left to column two when the character window opens, not the other way.
float columnX(float screenWidth, int column);
float panelY(float screenHeight);

// A rectangle in MU's panel frame, on screen, for a panel whose corner is at (x, y).
inline gfx::Box scaled(float x, float y, const gfx::Box& units) {
    const float k = scale();
    return {x + units.x * k, y + units.y * k, units.w * k, units.h * k};
}

// ---- the frame -------------------------------------------------------------------------------
//
// **The skin is `game/ui/sheet.h`**, chosen by the user on 2026-09-23 from a page of four:
// *"B, obsidian, deeper wells"*. MU's rectangles are untouched -- this is paint, and every
// number below is where MU already put something. What went with the leather: `bag_back`,
// `bag_plate`, `bag_crest`, `bag_cell`, `bag_field` and `bag_close`, all six replaced by shapes
// the canvas draws.

// The corner, the head's band of light, the margin a rule keeps, and where the mark and the
// title stand in the head. MU's own units, like everything else on this page.
constexpr float kRadius = 2.5f;
constexpr float kHeadBand = 36.0f;
constexpr float kEdge = 11.0f;  // the wells' own margin, which every rule is cut to
// The title stands on the wells' own margin: there is no mark in front of it any more.
constexpr float kTitleX = kEdge;

// The body starts eight units down: the crest sat in the strip above it with the world behind.
constexpr float kPlateTop = 8.0f;
constexpr float kPlateHeight = 28.0f;
constexpr float kHeadButton = 24.0f;
constexpr float kHeadInset = 7.0f;
constexpr float kHeadDrop = 1.0f;
constexpr float kTitleSize = 9.5f;  // Cinzel reads a size larger than the body face does

// The seat for a button in the plate's end cap -- the close, on the right.
gfx::Box headSocket(bool right);
inline gfx::Box frameClose() { return headSocket(true); }

// The glass, its gradient stroke, the head's band, the mark, the title and the rule under it.
void frame(gfx::Canvas& canvas, Arts& arts, float x, float y, const std::string& title);
// **The head's own face: Cinzel Medium**, the map name's and the fight's figures' family, baked
// once for every window. The user, 2026-09-23: *"maybe use that gothic font somehow"* -- it is
// the one voice this game already says its own name in, and a window's title is the same kind of
// thing. Baked by the desk at start-up; without it a title falls back to the interface's own
// face, which is what every window drew before.
bool openTitleFace(const gfx::Interface& interface);
void closeTitleFace();
// The cross in the head's right-hand end, in its three lights.
void close(gfx::Canvas& canvas, Arts& arts, float x, float y, bool pressed);
void close(gfx::Canvas& canvas, float x, float y, bool over, bool pressed);
// A framed block at any size, in the card's own 3% fill under a 10% hairline. `Panel.Field`.
void field(gfx::Canvas& canvas, Arts& arts, float x, float y, const gfx::Box& units,
           const char* key = "bag_field");
// One well at any size: the deep-cut cell every grid and every worn slot is drawn as.
void cell(gfx::Canvas& canvas, float x, float y, const gfx::Box& units, sheet::Cell state);
// Where a baseline goes to centre a line of a given size in a box: Godot's own arithmetic,
// half the leftover above the cap-line.
float centredBaseline(const gfx::Face& face, const gfx::Box& box, float fontSize);

// One state of a button sheet: `states` equal states stacked, rest on top.
gfx::Box buttonState(const gfx::Art& art, bool pressed, int states = 2);

// ---- words -----------------------------------------------------------------------------------

// A number the way MuDream prints one: thousands parted by a space.
std::string grouped(long long value);
// A number the way Godot's "N0" prints one: thousands parted by a comma.
std::string commas(long long value);

// The frame's own lettering, parchment rather than paper, and the tooltip's rungs.
inline constexpr uint32_t kLettering = gfx::rgba(0.90f, 0.86f, 0.76f);
inline constexpr uint32_t kOrdinary = gfx::rgba(1.0f, 1.0f, 1.0f);
inline constexpr uint32_t kRefined = gfx::rgba(1.0f, 0.8f, 0.1f);
inline constexpr uint32_t kUnmet = gfx::rgba(1.0f, 0.2f, 0.1f);
inline constexpr uint32_t kOptioned = gfx::rgba(0.5f, 0.7f, 1.0f);
inline constexpr uint32_t kDetail = gfx::rgba(0.4f, 0.4f, 0.4f);

// One line of a tip, and what colour it is.
struct Line {
    std::string text;
    uint32_t colour = 0xFFFFFFFFu;
    bool bold = false;
};

// A tip standing on a point, centred over it, kept on screen. `Panel.Tooltip`: MU's black at
// four fifths, a hard black hairline, and a dim warm one inside it.
void tooltip(gfx::Canvas& canvas, float x, float y, const std::vector<Line>& lines,
             float fontSize, float screenWidth, float screenHeight);

}  // namespace mu::game::panel
