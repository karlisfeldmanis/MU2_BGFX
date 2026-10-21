// The Helper's window (game/helper.h): where it hunts, how each skill on his bar is used, the
// auto-drink lines, what it picks up, and Start or Stop. The proposal's A, as the user tuned it on
// 2026-10-20 (claude.ai/artifact/DaBoq8ccx79xmoqc2j5YuX): the side window at the left where the
// quest log stands, each bar key a row with its own icon and the uses its kind offers ("update
// design helper with actual skill icons because its hard to understand"), T and the right button
// among them, runes picked and Zen not offered ("remove zen, its build in"), quest items not, no
// price ("helper has to be free").
//
// Its first opening is the first H: after that, H and the minimap's H start and stop it, and a
// right click on the minimap's H opens this.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/helper.h"
#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Play;
namespace panel {
class Arts;
}

class HelperWindow {
public:
    struct Result {
        bool close = false;
        bool clicked = false;   // something pressed: the window's click
        bool changed = false;   // the settings changed: to be kept
        bool start = false, stop = false;
        int health = -1, mana = -1;  // an auto-drink line set, in percent
    };
    void open(const gfx::Interface& interface, panel::Arts* arts);
    void close();
    void show();
    void hide();
    bool up() const { return up_; }
    // One frame while up. `bar` is his skill bar (the five keys, then the right button's slot).
    void update(float seconds, const Play& play, HelperSettings& settings, const int32_t* bar, bool helping,
                const Helper::Stats& stats, float width, float height, const Pointer& pointer, Result* out);
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    struct Row {
        int key = 0;            // the bar's index: 0-4 Q to T, 5 the right button
        int32_t skill = 0;
        HelperKind kind = HelperKind::Single;
        HelperUse use = HelperUse::Off;
        bool suits = true;      // his hands may throw it (SkillRow::suits)
        gfx::Box box, chip;
    };
    void layout(const Play& play, const HelperSettings& settings, const int32_t* bar);
    void rebuild(const Play& play, const HelperSettings& settings, bool helping, const Helper::Stats& stats);

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    bool up_ = false;
    std::vector<Row> rows_;
    // A row worded at its left with a switch at its right: the walk back, Follow my quest, the
    // party's aids, the trades.
    struct Toggle {
        gfx::Box row, knob;
    };
    // The two pages (the user, 2026-10-21, four more settings than the window had room for):
    // Hunt, and Loot & Party.
    int page_ = 0;
    gfx::Box tabs_[2], ranges_[3], segs_[2][9], picks_[7], invites_[3], go_;
    Toggle back_, quest_, aid_, trades_;
    float huntTop_ = 0.0f, skillsTop_ = 0.0f, drinkTop_ = 0.0f, pickTop_ = 0.0f, partyTop_ = 0.0f;
    // over_/pressing_: 0 the close, 10 + a range, 20 the walk back, 21 Follow my quest, 22 the
    // party's aids, 23 the trades, 24 + an invite's answer, 30 + a row, 50 + 10 x a meter + a
    // segment, 80 + a pick, 90 Start or Stop, 95 + a tab.
    int over_ = -1, pressing_ = -1;
    float x_ = 0.0f, y_ = 0.0f, unit_ = 1.0f, wide_ = 325.0f, tall_ = 734.0f;
    std::vector<int64_t> drawn_;
    bool built_ = false;
};

}  // namespace mu::game
