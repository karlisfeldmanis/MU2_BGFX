#include "app/modes/studio_desk.h"

#include <chrono>

#include "app/context.h"
#include "core/log.h"
#include "game/bench.h"
#include "game/figures.h"

namespace mu::app {

bool StudioDesk::open(Context& ctx) {
    // Open Sans, the face the game's warnings are set in; ImGui's own if it is not there.
    if (!gui_.open(ctx.window, ctx.paths.root + "/extern/OpenSans-SemiBold.ttf", 15.0f)) {
        core::logError("studio: the panels did not open; the bench runs without them");
        return false;
    }
    cooker_.open(ctx.paths.root);
    cooker_.poll();
    // No server answering: start one. It outlives the studio, so the next studio finds it.
    if (!cooker_.connected()) cooker_.startServer();
    return true;
}

void StudioDesk::begin(Context& ctx, float deltaSeconds) {
    gui_.begin(ctx.window, deltaSeconds);
    // A filter being typed into is a box with the keyboard: Escape closes it, not the studio.
    ctx.window.setTyping(gui_.wantsKeys());
    cooker_.poll();
}

std::string StudioDesk::targetOf(const game::ModelBench& bench) {
    if (!bench.browsing()) return "";
    const game::BrowseCategory& tab = bench.category(bench.categoryIndex());
    if (tab.at >= tab.entries.size()) return "";
    const game::BrowseEntry& entry = tab.entries[tab.at];
    if (entry.body) return entry.body->name;
    // A cooked world object, .../cooked/<world>/meshes/<Name>.mum: its recipe is
    // source/world/<world>/<Name>.json, which `mu make <world>/<Name>` finds.
    const std::string& path = entry.path;
    const size_t meshes = path.rfind("/meshes/");
    if (meshes == std::string::npos) return "";
    const size_t world = path.rfind('/', meshes - 1);
    const size_t dot = path.rfind('.');
    if (world == std::string::npos || dot == std::string::npos || dot < meshes) return "";
    return path.substr(world + 1, meshes - world - 1) + "/" +
           path.substr(meshes + 8, dot - meshes - 8);
}

void StudioDesk::panels(Context& ctx, game::ModelBench& bench) {
    // The names are read once a tab, not once a frame: the world objects run to hundreds.
    if (int(bench.categoryIndex()) != shelfCategory_ || shelf_.categories.size() != bench.categoryCount()) {
        shelfCategory_ = int(bench.categoryIndex());
        shelf_.categories.clear();
        for (size_t i = 0; i < bench.categoryCount(); ++i) {
            shelf_.categories.push_back(bench.category(i).label);
        }
        shelf_.names.clear();
        for (size_t i = 0; i < bench.browseCount(); ++i) shelf_.names.push_back(bench.browseName(i));
    }
    shelf_.category = shelfCategory_;
    shelf_.selected = int(bench.browseIndex());
    shelf_.target = targetOf(bench);
    shelf_.clip = bench.hasFigure() ? bench.clipLine() : std::string();
    shelf_.time = std::string(ctx.time.name()) + "    T changes the time of day";

    const double now = std::chrono::duration<double>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const studio::Asked asked = panels_.draw(shelf_, cooker_, float(ctx.window.width()),
                                             float(ctx.window.height()), now);
    if (asked.category >= 0) {
        bench.setCategory(size_t(asked.category), ctx.textures);
        core::logf("browser: %s", bench.browseLine().c_str());
    }
    if (asked.pick >= 0) {
        bench.step(asked.pick - int(bench.browseIndex()), ctx.textures);
        core::logf("browser: %s", bench.browseLine().c_str());
    }
}

void StudioDesk::end() { gui_.end(); }

void StudioDesk::shutdown() {
    cooker_.close();
    gui_.close();
}

}  // namespace mu::app
