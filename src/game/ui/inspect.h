// The inspect window: another player, what he wears and what he is. Ours: MU 0.75 has none.
//
// The user picked it on 2026-10-16 from a canvas of layouts (claude.ai/artifact/M1mgZkkn6E9Q3eN2XsZD8Q,
// "B3 · The figure is the window"): his whole figure in the middle, photographed as the target
// frame's face is (game/portrait.h, Portrait's figure), his armour down the left of it, his wings,
// jewellery, pet and mount down the right, his two hands under each column; his gear score large
// at his feet ("gear score has to be bigger"), his level and resets in the head, and twelve of his
// numbers to the foot, where the canvas had Whisper, Trade and Invite (the user: "remove footer
// buttons but show more stats"). **Nothing names a piece**: the user, "we will use tooltips for
// items" -- a piece is read in the item card, as in the bag.
//
// Nothing is asked of the server: the mirror holds every player on this map whole (Realm::satchelOf
// and his Body), which is what draws him, near or far. A player on another map cannot be
// inspected, and the window shuts when he leaves this one (Desk). His figure is a doll of its own
// (Play::wantDoll): dressed as he is, his weapons on his back, hovering on his wings when he wears
// them, and turned by a drag across it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

// Whether a player can be inspected: another player on this map, whom the mirror holds whole.
bool inspectable(const sim::Realm& realm, uint32_t id);

struct InspectRequests {
    bool close = false;  // the head's close
};

class Inspect {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);
    void useTipStage(Stage* stage) { tipStage_ = stage; }

    // A frame. `who` is his body and `bag` what he carries and wears, out of the mirror; `name` and
    // `guild` as the server said them (the guild empty for none). `figure` is his photograph, or
    // null before the first is taken.
    void update(float width, float height, int column, const sim::Realm& realm, const sim::Body& who,
                const sim::Satchel& bag, const std::string& name, const std::string& guild,
                const gfx::Art* figure, const Pointer& pointer, Stage* stage, InspectRequests* out);
    void hide();
    bool up() const { return up_; }
    // How far his figure is turned from facing the lens, in radians: a drag across it turns him
    // (the user, 2026-10-16: "option to rotate the figure"). Back to 0 for each new player.
    float turn() const { return turn_; }
    void resetTurn() { turn_ = 0.0f; }
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }

    // Where a worn slot stands in the window, in MU's units; a zero box for a slot not shown.
    static gfx::Box slotBox(int slot);

private:
    // Everything the drawing reads, so a frame that changes none of it draws nothing.
    struct Look {
        uint32_t id = 0, version = 0;
        float x = 0, y = 0, scale = 0, unit = 0, screenW = 0, screenH = 0;
        int hovered = -1;
        bool overClose = false, closing = false;
        uint16_t figure = 0xFFFF, items = 0xFFFF;
        int level = 0, resets = 0, kin = 0, score = 0;
        bool second = false;
        std::string labels[12], values[12];
        bool raised[12] = {};  // a figure the stat runes raise: the option blue
        std::string name, guild;
        bool operator==(const Look&) const = default;
    };
    void rebuild(const sim::Realm& realm, const sim::Body& who, const sim::Satchel& bag,
                 const gfx::Art* figure, Stage* stage);

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    panel::Arts* arts_ = nullptr;
    Stage* tipStage_ = nullptr;
    bool up_ = false;
    float x_ = 0.0f, y_ = 0.0f;
    int hovered_ = -1;   // the worn slot under the pointer
    bool overClose_ = false, closing_ = false;
    bool turning_ = false;  // a press went down on the figure and is held
    float turnFrom_ = 0.0f, turn_ = 0.0f;
    std::vector<Standing> standing_;
    Look drawn_, now_;
    bool drawnOnce_ = false;
};

}  // namespace mu::game
