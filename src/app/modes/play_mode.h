// The game: a world, a realm behind it, a character in it and the windows over it.
//
// Raised by `--world <name>` with `--play`. Everything here was the `if (inWorld)` arm of
// main()'s frame loop, and the order inside `frame()` is load-bearing to the millimetre --
// every "and only THEN" comment in it was paid for by a picture that was wrong. Moving a line
// of it is a change to the game, not a tidy.
//
// What this mode owns, and what it is handed: it owns the world, the desk, the item models,
// the litter and the ring, because each lives exactly as long as a played run. It is handed
// the window, the renderer and the textures, which outlive it. See app/context.h.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "app/mode.h"
#include "game/fx/litter.h"
#include "game/item_models.h"
#include "game/save.h"
#include "game/ui/desk.h"
#include "game/ui/outline.h"
#include "game/world/world.h"

namespace mu::app {

class PlayMode : public Mode {
public:
    bool open(Context& ctx) override;
    bool quitEarly() const override { return quitEarly_; }
    void frame(Context& ctx, const Frame& at) override;
    const gfx::Camera& camera() const override { return world_.camera(); }
    void report(Context& ctx) override;
    void shutdown(Context& ctx) override;

private:
    // The character's file (game/save.h), read BEFORE the world is, because it decides what
    // the world is raised with: his class, his level and the tile he stood on. The rest -- the
    // bag, the gear, the experience -- is laid on once the realm exists.
    void readSave(Context& ctx);
    // Writes the hero whole: every fifteen seconds of play, so a crash loses little, and once
    // more on the way out.
    void keep(Context& ctx);
    // The scripted hands: --give, --zen, --talk, and the windows --windows opens.
    void runScript(Context& ctx);
    // --shadow-points and --shadow-log, both of them measurement apparatus and neither of them
    // fatal. docs/shadow-probe.md reads the second.
    void openProbes(Context& ctx);
    void writeProbes(Context& ctx, const Frame& at);
    // The arena's hand, and the scripted pointer (--demo-clicks). Both raise exactly the
    // requests a person at the mouse raises and decide nothing else.
    void arenaHand();
    bool scriptedPointer(Context& ctx, const Frame& at, const float* view, const float* proj,
                         float* pointerX, float* pointerY);

    game::World world_;
    // The windows, over a played world and nowhere else: a bench has nobody to show them for.
    game::Desk desk_;
    // One store of item models for both the windows' pictures and what lies on the grass.
    game::ItemModels itemModels_;
    game::Litter litter_;
    game::Outline outline_;

    // Gathered fresh each frame into vectors that keep their capacity: a frame appends to a
    // flat array, as foundation 7 says, and allocates nothing after the first.
    std::vector<gfx::Drawable> townDrawables_;
    std::vector<gfx::Drawable> townCasters_;
    std::vector<gfx::Drawable> hoverDrawables_;

    std::string savePath_;
    game::Saved saved_;
    bool resumed_ = false;
    int64_t keptAt_ = 0;

    // The game's own entrance: the frame fades up from black and the character dissolves in
    // (Play::appear). A review run (--frames) is left alone, because a shot of a black frame
    // measures nothing.
    bool entrance_ = false;
    float entranceSeconds_ = 0.0f;

    // Who the arena's hand is fighting. Kept here rather than read off the hero, because what
    // the hero has been ordered to attack is the realm's private `order_` and is deliberately
    // not published -- a window asks and redraws, it does not read the order back. So the hand
    // remembers what it asked for, exactly as a person at the mouse remembers what they
    // clicked, and asks again only when that body is down.
    uint32_t arenaTarget_ = 0;

    FILE* shadowPoints_ = nullptr;
    FILE* shadowLog_ = nullptr;
    std::vector<float> pointGrid_;  // x, y, z a point

    bool quitEarly_ = false;
};

}  // namespace mu::app
