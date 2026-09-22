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

// The body starts eight units down: the crest sits in the strip above it with the world behind.
constexpr float kPlateTop = 8.0f;
constexpr float kPlateHeight = 28.0f;
constexpr float kHeadButton = 24.0f;
constexpr float kHeadInset = 7.0f;
constexpr float kHeadDrop = 1.0f;
constexpr float kTitleSize = 11.0f;

// The seat for a button in the plate's end cap -- the close, on the right.
gfx::Box headSocket(bool right);
inline gfx::Box frameClose() { return headSocket(true); }

// The leather, the plate, the crest and the title. `Panel.Head` over `Panel.Frame`.
void frame(gfx::Canvas& canvas, Arts& arts, float x, float y, const std::string& title);
// The X in the plate's right-hand cap, in its two lights.
void close(gfx::Canvas& canvas, Arts& arts, float x, float y, bool pressed);
// A hollow well at any size, nine-sliced with the art's own 12-texel border. `Panel.Field`.
void field(gfx::Canvas& canvas, Arts& arts, float x, float y, const gfx::Box& units,
           const char* key = "bag_field");
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
