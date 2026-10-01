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
    bool quitting() const override { return desk_.ready() && desk_.quitAsked(); }
    // Back to the character screen, on the menu's Switch Character; or on to another world,
    // which is this mode handing the run to a fresh one of itself (see travel()).
    Next next() const override {
        if (!travelTo_.empty()) return Next::Play;
        return (desk_.ready() && desk_.switchAsked()) || backNow_ ? Next::Lobby : Next::None;
    }
    void frame(Context& ctx, const Frame& at) override;
    const gfx::Camera& camera() const override { return world_.camera(); }
    // The world and its realm, for the performance sweep (app/sweep.h).
    game::World& world() { return world_; }
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
    // Leaves this world for `world` at the end of the frame: the run's arguments are pointed
    // at it and its spawn gate, the save is written as if he already stood there, and the
    // Application opens the next world as the character screen opens the first. Everything a
    // world owns -- land, town, realm, windows -- is shut down and raised again, which is the
    // same path Switch Character has always taken and so the one that is known to let go of
    // what it held. Walking into an enter gate (sim/gates.h) sends him to the tile the realm
    // chose in the target gate, facing the way that gate says; the M key, the stand-in for a
    // Move window, sends him to the world's spawn gate with `column` below 0.
    // `unfaced`: a tile given with no facing to keep, as a town's travel row lands.
    void travel(Context& ctx, const std::string& world, int column = -1, int row = -1,
                float facing = 0.0f, bool unfaced = false);
    std::string travelTo_;
    // Go Back! (app/context.h): the clock run down, the plate told, its click taken, and a world
    // come into by magic given its landing. Every frame, before the pointer.
    void goBack(Context& ctx, double seconds);
    bool landed_ = false;  // this world's first frame has been through goBack()
    static constexpr double kGoBackWaits = 5.4;  // the map name's time on screen (game/ui/arrival.h)
    int arriveColumn_ = 0, arriveRow_ = 0;
    float arriveFacing_ = 0.0f;
    bool arriveFaced_ = false;
    // How much colour is out of the world, and where it is going: the game greys while he is
    // down and comes back as he gets up. The message that says so is the interface's
    // (game/ui/tally.cpp) and is deliberately NOT drained with it.
    float drain_ = 0.0f;
    // And how far it is down to black: only ever for the revive's cut to the gate.
    float dim_ = 0.0f;

    // The one store of item models, wired to everything that draws one. Called from the
    // preloader, and again from the frame for a run whose realm rose some other way; the
    // second call finds it open and does nothing.
    void openItems(Context& ctx);
    // Every model a window can ask for in a single frame, read while the spinner is still up:
    // the shelves of every merchant standing in this world, and what the character already
    // carries. See the note at the call.
    void warmItems();

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
    // The monsters flashing red for turning on him, each its own run of `flashDrawables_`
    // (Play::Flash), and the one being ringed copied out whole, as Outline::show wants it.
    std::vector<gfx::Drawable> flashDrawables_;
    std::vector<game::Play::Flash> flashes_;
    std::vector<gfx::Drawable> flashOne_;

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
    // The quick slot's skill the arena's order was last given with: the desk binds the wizard's
    // Energy Ball a frame or two after the first order is raised, and the order is raised again
    // when it does, or the whole first fight is his staff.
    int32_t arenaSkill_ = 0;

    FILE* shadowPoints_ = nullptr;
    FILE* shadowLog_ = nullptr;
    std::vector<float> pointGrid_;  // x, y, z a point

    bool quitEarly_ = false;
    bool backNow_ = false;  // --lobby-back's frame has come
};

}  // namespace mu::app
