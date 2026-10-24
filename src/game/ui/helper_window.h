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
        bool clearLog = false;  // the last run's log let go
        int health = -1, mana = -1;  // an auto-drink line set, in percent
    };
    // What the keyboard did this frame, while its name field has it (typing).
    struct Keys {
        std::string typed;
        int backspaces = 0;
        bool enter = false;
        bool escape = false;
    };
    void open(const gfx::Interface& interface, panel::Arts* arts);
    void close();
    void show();
    void hide();
    bool up() const { return up_; }
    // Its name field has the keyboard.
    bool typing() const { return up_ && focus_; }
    // One frame while up. `bar` is his skill bar (the five keys, then the right button's slot).
    // `log`, the Helper's pickup log, may be null.
    void update(float seconds, const Play& play, HelperSettings& settings, const int32_t* bar, bool helping,
                const Helper::Stats& stats, const HelperLog* log, float width, float height, const Pointer& pointer, const Keys& keys,
                Result* out);
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
    // party's aids, the trades, the repair.
    struct Toggle {
        gfx::Box row, knob;  // the setting's row, and its Off | On at the right
    };
    // The pages (the user, 2026-10-21, four more settings than the window had room for): Hunt, Loot &
    // Party, and the pickup log (HelperLog, 2026-10-23).
    int page_ = 0;
    gfx::Box tabs_[3], ranges_[3], segs_[2][9], picks_[8], invites_[3], go_;
    // Pick up by name: the field, its Add, and a chip for each name kept (a click takes it off).
    gfx::Box nameField_, nameAdd_, names_[HelperSettings::kNameMost];
    std::string typed_;
    bool focus_ = false;
    // What the field suggests under it as he types: the items' names holding what is typed, those
    // beginning with it first, a click one taken whole. Found again only when the text changes.
    static constexpr int kSuggestMost = 6;
    std::vector<std::string> suggested_;
    std::string suggestedFor_;
    gfx::Box suggest_[kSuggestMost];
    void suggest(const Play& play, const HelperSettings& settings);
    double clock_ = 0.0;  // the caret's blink
    // The rows Range and Party invites stand in, and their choices' whole boxes.
    gfx::Box rangeRow_, rangeBox_, inviteRow_, inviteBox_;
    Toggle back_, quest_, aid_, trades_, repair_;
    // The Log page: Totals or Timeline, the first row shown, and its boxes -- the two views, the
    // pager's Newer and Older, the last run's Clear, the ended line and the list.
    const HelperLog* log_ = nullptr;
    int view_ = 0, logFrom_ = 0;
    gfx::Box views_[2], pager_[2], clear_, endedBox_, zenBox_;
    float logTop_ = 0.0f, listTop_ = 0.0f, listBottom_ = 0.0f;
    // The rows the view lists, made again only when the log or the view changes; how many stand in
    // the list at once.
    struct LogRow {
        std::string time, name, count;
        uint32_t ink = 0;
    };
    std::vector<LogRow> logRows_;
    std::vector<int64_t> logKey_;
    int logFit_ = 0;
    bool helping_ = false;
    void listLog();
    float huntTop_ = 0.0f, skillsTop_ = 0.0f, drinkTop_ = 0.0f, pickTop_ = 0.0f, namesTop_ = 0.0f, partyTop_ = 0.0f;
    // over_/pressing_: 0 the close, 10 + a range, 20 the walk back, 21 Follow my quest, 22 the
    // party's aids, 23 the trades, 24 + an invite's answer, 27 the repair, 30 + a row, 50 + 10 x a meter + a
    // segment, 70 the name field, 71 its Add, 80 + a pick, 90 Start or Stop, 95 + a tab, 100 + a name,
    // 120 + a suggestion, 130 + a view, 132 Newer, 133 Older, 134 Clear.
    int over_ = -1, pressing_ = -1;
    float x_ = 0.0f, y_ = 0.0f, unit_ = 1.0f, wide_ = 325.0f, tall_ = 734.0f;
    std::vector<int64_t> drawn_;
    bool built_ = false;
};

}  // namespace mu::game
