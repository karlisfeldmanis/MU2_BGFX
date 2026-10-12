// The bench: one thing to look at, turning in front of the whole frame.
//
// Four ways of opening it, one frame. They differ in what is put on the bench and in what
// stands behind it, and not in how a frame is drawn, which is why this is one class and not
// four:
//
//   Model    --model <path>   one .glb as it sits on disk, read through cgltf
//   Figure   --figure <name>  one cooked figure with its clips, on a patch of real ground
//   Browser  --browse         every cooked model by category, with the list down the left
//   Studio   --browse --studio  the browser's subject beside the map's own bonfire, with the
//                               world -- town, ground, baked light, lamps and fires -- opened
//                               behind it and drawn from the bench's camera
//
// A world is a world and a bench is a bench, and never both at once: they are two different
// things to look at and the camera belongs to whichever it is. The studio is not an exception
// to that -- the world is scenery there, and the thing on the bench is what is looked at.
#pragma once

#include <string>
#include <vector>

#include "app/mode.h"
#include "app/modes/studio_desk.h"
#include "game/bench.h"
#include "game/fx/set_shine.h"
#include "game/studio_stage.h"
#include "game/world/world.h"

namespace mu::app {

class BenchMode : public Mode {
public:
    enum class Kind { Model, Figure, Browser, Studio };

    // Decided from the arguments alone, before anything is opened. `--figure` is the monster
    // bench and `--model` the model bench: never both, and the figure wins.
    static Kind kindOf(const core::Args& args);

    bool open(Context& ctx) override;
    void frame(Context& ctx, const Frame& at) override;
    const gfx::Camera& camera() const override { return bench_.camera(); }
    void report(Context& ctx) override;
    bool quitEarly() const override { return quitEarly_; }
    bool quitting() const override { return desk_.quitAsked(); }
    void shutdown(Context& ctx) override;

private:
    // The browser's keys: the list walked, the categories tabbed, the clips stepped and the
    // time of day changed. Shared by the viewer on its plot and the viewer in a world, because
    // it is the same list and the same keys whichever ground is under the subject.
    void steer(Context& ctx);
    // The list down the left, and the pointer it eats while it is under one.
    void drawList(Context& ctx);
    // A drag orbits the camera and the wheel zooms it: what the pointer does over no list.
    void look(Context& ctx);
    // The studio's stage: the map's own bonfire nearest the middle of the town, and the
    // subject stood 1.6 m to its -x and +z, which is the left of MU's picture.
    bool openStudioWorld(Context& ctx, const std::string& benchWorld);
    // The studio's scenes (app/modes/bench_scenes.cpp, docs/order-plan.md §9c F3b): "Stage",
    // "Home" (the subject's own place: a monster at its spawn, an armour set on the character
    // screen's middle pedestal, anything else at Lorencia's bonfire) or a world by name.
    void setScene(Context& ctx, const std::string& scene);
    // Every item on the bench at +plus, as the game shines it (the inspector's refinement):
    // the world's item rows and the showing's two shine sheets, read the first time.
    void studioPlus(Context& ctx, int plus);
    // The world the studio stands in now, which the Stage keeps open and does not draw.
    std::string sceneWorld() const { return sceneWorld_; }

    game::ModelBench bench_;
    // The studio's scenery, and nothing else's. A plain bench never opens it, and
    // `world_.shutdown()` on an unopened world is a no-op -- which is what lets shutdown()
    // be one sequence for all four kinds.
    game::World world_;
    Kind kind_ = Kind::Model;
    float studioStand_[3] = {0.0f, 0.0f, 0.0f};
    float studioFire_[3] = {0.0f, 0.0f, 0.0f};
    std::vector<gfx::Drawable> townDrawables_;
    // The Armour sets tab's suit at --plus 9 and up, lit as a complete set is in play: its arm
    // lights from +9, its flares and ribbons at +10 and +11 (game/fx/set_shine.h).
    game::SetShine setShine_;
    // The studio's panels (`mu2 --studio`, docs/order-plan.md §9c): in place of the overlay's
    // list, and closed under --no-list as it was.
    StudioDesk desk_;
    game::StudioStage stage_;
    bool onStage_ = false;
    bool quitEarly_ = false;   // the window closed under the studio's preloader
    std::string sceneWorld_;
    std::vector<content::ItemRow> plusRows_;   // studioPlus's rows, once read
};

}  // namespace mu::app
