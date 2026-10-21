// Moss the Gambler's window, the Jeweler (sim/jeweler.h): one column at the vault's place, beside
// the bag, with three tabs -- Recut, Salvage, Combine -- the user's layout A of 2026-10-20
// (https://claude.ai/artifact/EQ6axt3WtpV2LjXQDghV9B).
//
//   * A piece comes in by a right-click on it in the bag (Desk), and goes out by a click on its
//     slot or the foot's Take out, on every page (the user, 2026-10-21: "we dont have button take
//     out for all services"). The slots name bag slots: the piece never leaves the bag, so there is nothing to keep
//     or pour back when he walks away.
//   * Recut: the piece, its signature (locked) and its further powers as rows; one picked, the
//     foot's Recut sends it. The new power is lit gold in its row.
//   * Salvage: the piece and the five prizes it may become, as pictures, with no odds (the user:
//     "dont show graph ... just description with jewel art"); Salvage asks once.
//   * Combine: three slots; the foot's Combine sends them.
//
// Each answer comes back through Desk::expect as the realm's value (Realm::recut, salvage,
// combine) and is said on the page until the next ask. What it made is shown, not only said (the
// user, 2026-10-21: "i dont like idea that we dont show what created"): Salvage's prize and
// Combine's new piece stand in a card of their own, with the name and its powers; Recut's new line
// is lit and the line it replaced is named under it.
#pragma once

#include <cstdint>
#include <string>

#include "content/tables.h"
#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

struct JewelerRequests {
    int recutSlot = -1, recutLine = -1;  // a Recut asked
    int salvageSlot = -1;                // a Salvage asked, confirmed
    int combine[3] = {-1, -1, -1};       // a Combine asked
    bool click = false;                  // a press that asks nothing of the realm
    bool turned = false;                 // a tab changed: the page sound
    bool close = false;
};

class Jeweler {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);
    void update(float width, float height, int column, const sim::Realm& realm,
                const Pointer& pointer, Stage* stage, JewelerRequests* out);
    // A right-click on a bag piece while the window is up: into the page's slot, or the next
    // free of Combine's three. False, untaken, for anything he does not work.
    bool offer(int slot, const sim::Realm& realm);
    // The realm's answer to the last ask (sim::Realm::recut, salvage, combine), and why not.
    void answered(bool ok, int32_t value, const std::string& refusal);
    void cancel();
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    enum class Page : int { Recut, Salvage, Combine };
    enum Hit : int { kNone = -1, kTab0 = 0, kRow0 = 3, kSlot0 = 6, kRun = 9, kNo = 10, kYes = 11, kOut = 12 };
    struct Slot {
        int bag = -1;
        int32_t item = -1;
    };
    int hitAt(float ux, float uy) const;
    bool holding() const;
    void keep(const sim::Realm& realm);
    std::string key(const sim::Realm& realm) const;
    void rebuild(const sim::Realm& realm, Stage* stage);

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    float x_ = 0.0f, y_ = 0.0f;
    bool up_ = false;
    Page page_ = Page::Recut;
    Slot slots_[3];        // Recut and Salvage use the first; Combine all three
    // What the last ask made, shown in its card until the next ask, a slot or a tab change:
    // Salvage's prize (which of the five, how many) or Combine's piece (its bag slot, read live).
    struct Made {
        Page page = Page::Recut;
        int prize = -1, count = 0;
        int bag = -1;
        int32_t item = -1;
    };
    int line_ = -1;        // Recut's picked further power, 0 to 2
    int lit_ = -1;         // the line the last recut changed, lit until the next ask
    std::string was_;      // that line before the recut, in words
    Made made_;
    bool confirming_ = false;
    Page asked_ = Page::Recut;
    bool waiting_ = false;
    std::string said_;     // the last answer, in words
    bool saidOk_ = true;
    int over_ = kNone, pressing_ = kNone;
    bool overClose_ = false, closing_ = false;
    std::vector<Standing> standing_;
    std::string drawn_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
