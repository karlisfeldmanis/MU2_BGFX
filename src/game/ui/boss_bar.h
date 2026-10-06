// The Golden Dragon's bar and its party's (docs/golden-dragon-raid.md §3, the user, 2026-10-06:
// 'we will probaly need improvment monster label which shows that this is a special monster').
//
// While he is in its fight (Play::raidFighting): a wide bar at the top of the screen under the
// event's banner -- the name in gold over it, notches at the stages' 70, 40 and 15 percent, the
// stage under it, and a thin bar under that while a move is told, naming it ('Fire Breath',
// 'Roar', 'Golden Inferno'); meteors are not named, as they are not marked (the user: 'they
// just has to happen'). And over every raider standing, a small green bar with its name, as her
// summon's escort bar. Sanctuary's: no ornaments, no strokes, one palette (game/ui/style.h).
#pragma once

#include <cstdint>
#include <string>

#include "gfx/interface.h"

namespace mu::game {

class Play;

class BossBar {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    // A frame: the fade, and the canvas laid again.
    void update(float seconds, const Play& play, const float* viewProj, int width, int height);
    bool showing() const { return !canvas_.empty(); }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    gfx::Canvas canvas_;
    float shown_ = 0.0f;  // the fade, 0 to 1
    float lag_ = 1.0f;    // the trail's edge, as a fraction of the bar
};

}  // namespace mu::game
