// Devil Square's result: the square's ranks as its time is up, and Claim.
//
// MU's rank box (CSDevilSquareMatch::RenderMatchResult, CSEventMatch.cpp:196-326, its packet 0x93
// PMSG_DEVILSQUARERESULT): "Your bravery is proven in Devil Square" (text 648), then a row for
// each of the square's best -- rank, name, points, the experience and the Zen the rank pays --
// with his own row marked; WebZen sends the top nine and his own (DevilSquareGround.cpp:364-418).
// Drawn in Sanctuary's controls, centred a little above the middle as the Zen box is, with one
// answer: Claim, his row paid and home to Noria now, as Blood Castle's Complete sends him to
// Devias (docs/devil-square-port.md, "The goal and how it is shown"). Shut with its cross, or
// never answered, it pays as the minute's rest runs out and everyone goes home; the clock to that
// is in its foot.
//
// A mirror, as every window is: told the rows each frame by the desk (the realm's
// squareResults, the names as the server or the bench says them), rebuilt only when they or the
// hover moved. Ours to look at; MU's to say.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class SquareResult {
public:
    struct Row {
        int rank = 0;
        std::string name;
        int64_t points = 0, experience = 0, zen = 0;
        bool mine = false;
        bool operator==(const Row&) const = default;
    };
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    void close();
    // One frame. `shown` false takes it down; `square` heads it; `rows` by rank, his own among
    // them; `secondsLeft` to home. Returns true on the frame Claim was pressed.
    bool update(float seconds, bool shown, int square, const std::vector<Row>& rows,
                int secondsLeft, const Pointer& pointer, float width, float height);
    // Shut by its cross until the next result: the rows stay owed, paid on the way home.
    bool dismissed() const { return dismissed_; }
    void reset() { dismissed_ = false; }
    bool up() const { return up_; }
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    int buttonAt(float ux, float uy) const;
    void rebuild();

    gfx::Canvas canvas_;
    bool up_ = false;
    bool dismissed_ = false;
    int square_ = 1;
    std::vector<Row> rows_;  // as drawn: the top nine, and his own after them when lower
    int secondsLeft_ = 0;
    float x_ = 0.0f, y_ = 0.0f;
    float tall_ = 0.0f;  // the window's height, in units, by its rows
    int over_ = -1, pressing_ = -1;  // 0 Claim, 1 the head's close
    float lift_[2] = {0.0f, 0.0f};
    bool built_ = false;
    struct Drawn {
        int square = 0, secondsLeft = 0, over = -1, pressing = -1;
        std::vector<Row> rows;
        float x = 0, y = 0, scale = 0, lift0 = 0, lift1 = 0;
        bool operator==(const Drawn&) const = default;
    };
    Drawn drawn_;
};

}  // namespace mu::game
