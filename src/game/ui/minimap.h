// The minimap: what is around him, in the top right corner over the quest tracker, always up while a world is played.
//
// Flat and colourless, as the user asked on 2026-09-29 ("more simple, flat without colors, clean,
// minimal", then "some terrain, some cartograph"): one ash sheet on an iron hairline, the land
// on it as a chart drawn off the attribute grid -- walkable ground one quiet tone, the town a step
// lighter, every shore and wall an iron hairline -- and over it the marks, each a single flat
// tone from style.h and one clean glyph, baked in code as the quest marker is:
//   * him: a bone arrow at the middle, turned the way he faces;
//   * the quest's monsters: a red dot on each live one of a breed a quest still counts -- the
//     one colour on it, Sanctuary's accent;
//   * the quest giver while he has something for him: a "!" or a "?";
//   * the gates out of the map: an arch;
//   * the townsfolk who do something, quieter: a pouch for a merchant, an anvil for a smith, a
//     chest for the vault, and a small ring for anyone else worth a name (guards are left off).
// The giver and the gates hold to the edge when they are out of reach, so the way to them is
// always shown. Where he stands, MU's own coordinates, is set small in the lower corner. The
// pointer on a mark names it; the wheel over the square zooms.
//
// Turned by the camera's heading, read off the view matrix each frame, so up on the square is
// up on the screen and a step to the right is to the right. MU 0.75 has no minimap (Season 3's
// CNewUIMiniMap is the later thing); having one at all is ours.
//
// A mirror, as every window is: it reads the realm and the camera and redraws when what it
// shows moved.
#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::sim {
struct EnterGate;
}
namespace mu::content {
struct Tables;
class Ground;
}

namespace mu::game {

class Play;

class Minimap {
public:
    void open(const gfx::Interface& interface);
    void close();

    // The camera this frame: its view matrix, whose right and up rows turn the marks.
    void setView(const float* view);
    // A frame. `scroll` is the wheel, spent only while the pointer is over the square.
    void update(float seconds, const Play& play, const Pointer& pointer, float scroll, int width,
                int height);
    void dismiss() {
        canvas_.clear();
        showing_ = false;
    }

    bool showing() const { return showing_; }
    // Whether a point is over it, so a click there does not walk him.
    bool covers(float x, float y) const {
        const float dx = x - map_.midX(), dy = y - map_.midY();
        return showing_ && dx * dx + dy * dy < radius_ * radius_;
    }
    // The land the chart reads its water from, by the floor MU paints it with. Null charts
    // everything unwalkable alike.
    void setGround(const content::Ground* ground) { ground_ = ground; }
    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

    // What a mark is: the glyph's cell in the baked strip.
    enum class Glyph : uint8_t { Hero, Quarry, Offer, HandIn, Vendor, Smith, Vault, Gate, Folk, Potion };

private:
    // One thing on the square, in sixteenths of a screen pixel, rounded, so two frames that would draw the same
    // picture compare equal and nothing is rebuilt.
    struct Mark {
        Glyph glyph = Glyph::Folk;
        int x = 0, y = 0;
        int angle = 0;       // the hero's arrow, in tenths of a degree clockwise from up
        bool pinned = false; // held to the edge: out of reach
        int name = -1;       // the folk index, or -1; the hint's words come from here
        int32_t kind = -1;   // a monster's breed, or a gate's number, for the hint
        int fade = 16;       // how much of it the rim's fade leaves, in sixteenths
        bool operator==(const Mark& o) const {
            return glyph == o.glyph && x == o.x && y == o.y && angle == o.angle && fade == o.fade &&
                   pinned == o.pinned && name == o.name && kind == o.kind;
        }
    };

    bool bake(float unit);
    // The chart of `tables`' map, off its attribute grid: see minimap.cpp.
    bool bakeChart(const content::Tables& tables, const content::Ground* ground);
    void rebuild(const Play& play);
    // Screen offset from the square's middle, in pixels, of a point `dc, dr` tiles from him.
    void place(float dc, float dr, float* sx, float* sy) const;

    gfx::Canvas canvas_;
    bool showing_ = false;
    uint64_t rebuilds_ = 0;

    // The glyphs, one strip of white cells baked at the interface unit and tinted when drawn.
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
    gfx::Art glyphs_;
    float bakedUnit_ = 0.0f;
    int cell_ = 0;

    // The chart, and the map it was drawn for (MU's number, -1 for none yet).
    bgfx::TextureHandle chartTexture_ = BGFX_INVALID_HANDLE;
    gfx::Art chart_;
    int chartMap_ = -1;
    float chartTiles_ = 256.0f;
    const content::Ground* ground_ = nullptr;
    const content::Ground* chartGround_ = nullptr;  // the land the chart was drawn from

    // The camera's right and up on the ground, as tile vectors (column, row), unit length.
    float right_[2] = {1.0f, 0.0f};
    float up_[2] = {0.0f, -1.0f};

    // The enter gates on the map the list was made for (MU's map number, -1 for none yet).
    std::vector<const sim::EnterGate*> gates_;
    int gatesMap_ = -1;

    // How many tiles the square spans across, one of kSpans.
    int zoom_ = 1;
    // His facing as the arrow shows it, eased toward the realm's so a 45-degree tick turn glides.
    float facing_ = 0.0f;
    bool faced_ = false;

    // This frame's layout, in pixels.
    gfx::Box outer_;  // the square with its hairline, what the pointer is refused by
    gfx::Box map_;    // the circle's box
    float radius_ = 0.0f;
    float scale_ = 1.0f;  // pixels a tile at this zoom
    float heroX_ = 0.0f, heroY_ = 0.0f;  // his tile

    // What the canvas shows, and what this frame would show: rebuilt when they differ.
    struct Face {
        int width = 0, height = 0;
        int column = 0, row = 0;  // the coordinates in the corner
        int heroX = 0, heroY = 0; // his tile in sixteenths, which slides the chart
        int turn = 0;             // the camera's heading in tenths of a degree, which turns it
        int zoom = -1;
        int hovered = -1;
        std::vector<Mark> marks;
        bool operator==(const Face& o) const {
            return width == o.width && height == o.height && column == o.column &&
                   row == o.row && heroX == o.heroX && heroY == o.heroY && turn == o.turn &&
                   zoom == o.zoom && hovered == o.hovered && marks == o.marks;
        }
    };
    Face now_, drawn_;
    bool built_ = false;
};

}  // namespace mu::game
