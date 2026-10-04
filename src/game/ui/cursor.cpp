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

// MU's pointers are 24 of its 640-line pixels; ours are kSize at the same place, so one of MU's
// is this many of the art's.
constexpr float kMuPixel = kSize / 24.0f;

}  // namespace

// The hammer as RenderCursor has it. At rest it is drawn as the hand is, its tip two in from the
// pointer. With the button down it is RenderBitmapRotate'd 45 degrees about its middle, which it
// moves to (+5, +18) -- the head swung down onto the item, the blow, and back up on release.
void Cursor::drawHammer(const gfx::Art& art, float x, float y, bool pressed) {
    const float factor = panel::unit() * 0.5f * 0.85f;
    const float size = kSize * factor;
    if (!pressed) {
        canvas_.image(art, {x - kTipX * factor, y - kTipY * factor, size, size});
        return;
    }
    // RenderBitmapRotate turns counter-clockwise on the screen (GL's y is up), which on our
    // y-down canvas is a negative angle.
    const float cx = x + 5.0f * kMuPixel * factor, cy = y + 18.0f * kMuPixel * factor;
    const float c = std::cos(0.7853982f), s = std::sin(0.7853982f), h = size * 0.5f;
    const float corners[4][2] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
    const float uv[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    float xy[8];
    for (int i = 0; i < 4; ++i) {
        xy[i * 2] = cx + corners[i][0] * c + corners[i][1] * s;
        xy[i * 2 + 1] = cy - corners[i][0] * s + corners[i][1] * c;
    }
    canvas_.polygon(&art, xy, uv, 4, 0xFFFFFFFFu);
}

void Cursor::open(const gfx::Interface& interface, panel::Arts* arts) {
    arts_ = arts;
    interface.adopt(canvas_);
}

void Cursor::update(float seconds, float x, float y, bool onMonster, bool onLoot, bool onFolk,
                    Perch perch, bool mending, bool pressed) {
    elapsed_ += seconds;
    canvas_.clear();
    if (!arts_ || x < 0.0f || y < 0.0f) return;

    // The repair mode, the bag's own or a mending counter's, is tested before all of it:
    // RenderCursor draws BITMAP_CURSOR + 5, the hammer, whatever lies under the pointer, so it is
    // plain that the next click on a thing mends it (ZzzInterface.cpp:4092-4105).
    if (mending) {
        const gfx::Art& hammer = arts_->get("cursor_repair");
        if (hammer.valid()) {
            drawHammer(hammer, x, y, pressed);
            return;
        }
    }

    // RenderCursor's own ladder: an item on the ground is tested above a townsperson and a
    // townsperson above a monster, so a drop lying under a monster shows the hand that picks it
    // up and not the sword. See Pointer.Show. Something to sit on is below the townsperson and
    // above the monster -- but the pointer only finds one where no body is under it at all.
    const char* key = onLoot                ? "cursor_get"
                      : onFolk              ? "cursor_talk"
                      : perch == Perch::Lean ? "cursor_lean"
                      : perch == Perch::Sit  ? "cursor_sit"
                      : onMonster           ? "cursor_attack"
                                            : "cursor";
    const gfx::Art& art = arts_->get(key);
    if (!art.valid()) return;
    // A departure from MU2, which drew this at the art's own 32 pixels whatever the window's
    // size. This project scales every other piece of interface art with the screen
    // (panel::unit(), 2 at 1080 lines), and a cursor that stayed fixed while the HUD and the
    // panels grew around it would look wrong on anything else -- so it is scaled the same way,
    // 1 at 1080p and proportional either side of it -- and 85% of that since 2026-10-02, the
    // user: *"scale down game cursor little bit"*.
    const float factor = panel::unit() * 0.5f * 0.85f;
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
