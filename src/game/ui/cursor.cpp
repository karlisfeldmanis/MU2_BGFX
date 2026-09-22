#include "game/ui/cursor.h"

#include <cmath>

namespace mu::game {
namespace {

// Where in the 32-square art the point actually is. MU2's Tip: the client draws its own 24-pixel
// cursor two twenty-fourths in, which on this art's 32 is a hair under three -- three, because a
// hot spot is whole pixels and that is where the drawn hand's tip sits.
constexpr float kTipX = 3.0f, kTipY = 3.0f;
constexpr float kSize = 32.0f;

// Ten steps a second through the talk sheet's six quadrants -- a loop out to the far corner and
// back, not a plain 0-1-2-3. MU2's TalkFrames, in the sheet's own unit square.
constexpr double kTalkStepsPerSecond = 10.0;
constexpr float kTalkFrames[6][2] = {
    {0.0f, 0.0f}, {0.5f, 0.0f}, {0.0f, 0.5f}, {0.5f, 0.5f}, {0.0f, 0.5f}, {0.5f, 0.0f},
};

}  // namespace

void Cursor::open(const gfx::Interface& interface, panel::Arts* arts) {
    arts_ = arts;
    interface.adopt(canvas_);
}

void Cursor::update(float seconds, float x, float y, bool onMonster, bool onLoot, bool onFolk) {
    elapsed_ += seconds;
    canvas_.clear();
    if (!arts_ || x < 0.0f || y < 0.0f) return;

    // RenderCursor's own ladder: an item on the ground is tested above a townsperson and a
    // townsperson above a monster, so a drop lying under a monster shows the hand that picks it
    // up and not the sword. See Pointer.Show.
    const char* key = onLoot ? "cursor_get" : onFolk ? "cursor_talk" : onMonster ? "cursor_attack"
                                                                                  : "cursor";
    const gfx::Art& art = arts_->get(key);
    if (!art.valid()) return;
    // A departure from MU2, which drew this at the art's own 32 pixels whatever the window's
    // size. This project scales every other piece of interface art with the screen
    // (panel::scale(), 2 at 1080 lines), and a cursor that stayed fixed while the HUD and the
    // panels grew around it would look wrong on anything else -- so it is scaled the same way,
    // 1 at 1080p and proportional either side of it.
    const float factor = panel::scale() * 0.5f;
    const float size = kSize * factor;
    const gfx::Box to{x - kTipX * factor, y - kTipY * factor, size, size};

    if (onFolk) {
        // One quadrant of the two-by-two sheet at a time, at whatever step the clock is on the
        // moment the pointer crosses onto a merchant -- the clock runs always, not from zero.
        const int step = int(elapsed_ * kTalkStepsPerSecond) % 6;
        const float half = art.width * 0.5f;
        canvas_.region(art, to, {kTalkFrames[step][0] * art.width, kTalkFrames[step][1] * art.height,
                                 half, art.height * 0.5f});
    } else {
        canvas_.image(art, to);
    }
}

}  // namespace mu::game
