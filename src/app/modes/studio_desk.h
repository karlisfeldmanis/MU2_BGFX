// The studio's desk: the panels over the bench in `mu2 --studio` (docs/order-plan.md §9c, F2).
//
// The bench mode's Studio kind owns one and calls it at four points of its frame: begin before
// its steering, panels where the overlay's list was drawn, end after the world, and wantsPointer
// to decide whether a drag orbits the camera. What it shows of the bench is the bench's own
// browser -- tabs, names, the one standing there -- and each name's `mu make` target; what it
// cooks goes through `mu serve`, which it starts when none is running.
//
// Off under --no-list, as the overlay's list was, so tools/studio.py's sweeps (which pass it)
// shoot the subject with nothing over it.
#pragma once

#include <string>

#include "gfx/gui.h"
#include "studio/cooker.h"
#include "studio/panels.h"

namespace mu::game {
class ModelBench;
}

namespace mu::app {

struct Context;

class StudioDesk {
public:
    bool open(Context& ctx);
    bool isOpen() const { return gui_.isOpen(); }
    void begin(Context& ctx, float deltaSeconds);
    void panels(Context& ctx, game::ModelBench& bench);
    void end();
    bool wantsPointer() const { return gui_.wantsPointer(); }
    bool wantsKeys() const { return gui_.wantsKeys(); }
    void shutdown();

    // What `mu make` calls the thing on the bench: world/Name for a world object, the
    // figure's own name for a body; empty when the entry is neither.
    static std::string targetOf(const game::ModelBench& bench);

private:
    gfx::Gui gui_;
    studio::Cooker cooker_;
    studio::Panels panels_;
    studio::Shelf shelf_;
    int shelfCategory_ = -1;   // the tab shelf_.names was read for
};

}  // namespace mu::app
