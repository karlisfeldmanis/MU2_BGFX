// Lugard's window, the Rebirth (sim/rebirth.h): the quest dialog's side column at the left, the
// user's layout A of 2026-10-22 (the canvas board "A · Lugard's window", project/Main.dc.html and
// NotReady.dc.html in https://claude.ai/artifact/Akuk8dQeyimkgCpeY4CPs9).
//
//   * At level 400: Lugard's words, then "The rite" -- Rebirth n -> n+1, Level 400 -> 1, stats,
//     skills and talents kept, talent points before and after, the worn gear that goes to the
//     bag, what the next rebirth asks -- and what this one needs, each ticked. Not yet / Be reborn.
//   * Below 400: his one line, "The rite asks" with the level as a bar and the Zen, and Be reborn
//     shut.
//   * Be reborn asks once more ("Kneel before Lugard?"), since it takes the Zen and the levels and
//     cannot be undone; not on the canvas.
//
// The answer comes back through Desk::expect as the realm's (Realm::rebirth). A yes shuts the
// window in the realm, and the rite takes over (fx/rite.h, ui/reborn.h); a no is said over the
// foot.
#pragma once

#include <string>

#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

struct RebirthRequests {
    bool reborn = false;  // the rite asked, confirmed
    bool click = false;   // a press that asks nothing of the realm
    bool close = false;
};

class RebirthWindow {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);
    void update(float width, float height, const sim::Realm& realm, const Pointer& pointer,
                RebirthRequests* out);
    // The realm's answer to the rite (Realm::rebirth), and why not.
    void answered(bool ok, const std::string& refusal);
    void cancel();
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    enum Hit : int { kNone = -1, kLeave = 0, kRun = 1, kNo = 2, kYes = 3 };
    int hitAt(float ux, float uy) const;
    std::string key(const sim::Realm& realm) const;
    void rebuild(const sim::Realm& realm);

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    float x_ = 0.0f, y_ = 0.0f;
    bool up_ = false;
    bool confirming_ = false;
    bool waiting_ = false;
    std::string said_;  // the last refusal, in words
    int over_ = kNone, pressing_ = kNone;
    bool overClose_ = false, closing_ = false;
    std::string drawn_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
