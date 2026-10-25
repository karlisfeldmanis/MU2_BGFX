// **The target frame** (the user, 2026-10-10: "make artifact design proposal how to to show
// charatcer label on screen when chaacter clicks on character or party label, we like WoW style",
// then "implement this"; the proposal, claude.ai/artifact/PV9VmdLkN4mqJcoW2tQdzm, layout B).
//
// The player picked by a click on him in the world (which still walks there) or on his party
// capsule, over the plate in the screen's middle until Escape lets him go:
//   * his head in a round portrait, the helm he wears on it (game/portrait.h), his level on a tag
//     under it, his class's letters on its shoulder, the leader's crown over it;
//   * the party's capsule a size up, tucked under the face: SD a line along its top as thin as
//     the mana along its foot (the user: "SD has to be just line same as mana"), health the body
//     with its points, current over most;
//   * one row of what stands on him over it, debuffs first in their red edge, each cell's time a
//     clock swept round it and no figures (the user: "buff, debufs has to be on one horizontal
//     row", "dont show seconds under buffs/auras", "make buffs/auras little bit smaller");
//   * under it his guild at the left -- `<Bot>` on a bot until guilds exist, as the name over his
//     head says -- and at the right the monster hitting him now, its name and health (the user:
//     "show only whuch monster is hitting char, show also guildname");
//   * dead, the same frame drained and grey: empty bars, "Dead", nothing standing on him, and
//     "Killed by" the last monster on him (the user: "work on death so there is same UI but
//     disabled / dead");
//   * gone, and the target let go, once he is past about the screen's reach or on another map
//     (the user: "if distance is longer than need char label has to disepear").
//
// Read off the mirror: on his map the realm steps every body the server does, so his health, his
// boons and what is fighting him are all here with no message for them.
#pragma once

#include <string>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Play;
namespace panel {
class Arts;
}

class TargetFrame {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    // A frame. `bottom` is where its foot stands (pixels, the plate's top less a gap), `width`
    // the screen's; `face` the portrait's picture, `faceTaken` whether it is his yet.
    void update(float seconds, Play& play, const Pointer& pointer, panel::Arts& arts, float width,
                float bottom, const gfx::Art& face, bool faceTaken);
    bool showing() const { return showing_; }
    float top() const { return box_.y; }  // what stands over it (game/ui/herald.h)
    // The frame takes the pointer: a click on it walks no one.
    bool covers(float x, float y) const { return showing_ && (took_ || box_.has(x, y)); }
    // A press on its menu button this frame asked for his menu, beside the button. Taken once.
    bool takeMenuAsk(std::string* name, float* x, float* y) {
        if (!menuAsk_) return false;
        menuAsk_ = false;
        *name = name_;
        *x = menuX_;
        *y = menuY_;
        return true;
    }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    gfx::Canvas canvas_;
    bool showing_ = false;
    bool took_ = false;
    gfx::Box box_;
    std::string name_;
    std::string killer_;   // the last monster on him while he stood: "Killed by" when he falls
    float clock_ = 0.0f;   // the low health's breath
    bool menuAsk_ = false;
    float menuX_ = 0.0f, menuY_ = 0.0f;
};

}  // namespace mu::game
