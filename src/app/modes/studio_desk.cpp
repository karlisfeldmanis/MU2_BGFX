#include "app/modes/studio_desk.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#include <bgfx/bgfx.h>
#include <spawn.h>
#include <sys/wait.h>

#include "app/context.h"
#include "core/log.h"
#include "game/bench.h"
#include "game/figures.h"

extern "C" char** environ;

namespace mu::app {

bool StudioDesk::open(Context& ctx) {
    // Open Sans, the face the game's warnings are set in; ImGui's own if it is not there.
    if (!gui_.open(ctx.window, ctx.paths.root + "/extern/OpenSans-SemiBold.ttf", 15.0f)) {
        core::logError("studio: the panels did not open; the bench runs without them");
        return false;
    }
    shelfRoot_ = ctx.paths.root;
    if (const char* at = std::getenv("MU_STUDIO_SWAP_AT")) swapAt_ = std::atoi(at);
    cooker_.open(ctx.paths.root);
    cooker_.poll();
    // No server answering: start one. It outlives the studio, so the next studio finds it.
    if (!cooker_.connected()) cooker_.startServer();
    return true;
}

void StudioDesk::begin(Context& ctx, float deltaSeconds, double elapsed) {
    elapsed_ = elapsed;
    if (comparePid_ > 0 && ::waitpid(comparePid_, nullptr, WNOHANG) == comparePid_) comparePid_ = 0;
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

void StudioDesk::watchJobs(game::ModelBench& bench) {
    // The jobs there when the studio connected are not this studio's to answer.
    if (!cooker_.connected()) return;
    for (const studio::JobView& job : cooker_.jobs()) {
        if (!job.finished() || std::find(seen_.begin(), seen_.end(), job.number) != seen_.end()) {
            continue;
        }
        seen_.push_back(job.number);
        if (!seenAll_ || job.state != "done" || swap_ != Swap::Idle) continue;
        // The Cook button's name, or the recipe's own from a `./mu make` by hand.
        const std::string here = targetOf(bench);
        if (job.target != here && "Arm" + job.target != here) continue;
        core::logf("studio: job %d cooked %s; swapping it in", job.number, job.target.c_str());
        startSwap(bench, job.target);
    }
    seenAll_ = true;
}

void StudioDesk::startSwap(game::ModelBench& bench, const std::string& name) {
    // Hold it still and take the first shot.
    {
        char when[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(when, sizeof(when), "%Y%m%d-%H%M%S", std::localtime(&now));
        std::string safe = name;
        for (char& c : safe) {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }
        swapDir_ = shelfRoot_ + "/shots/studio/swaps/" + safe + "_" + when;
        std::error_code ignored;
        std::filesystem::create_directories(swapDir_, ignored);
        swap_ = Swap::Before;
        swapFrames_ = 0;
        heldSeconds_ = elapsed_;
        bench.replayClip();
    }
}

void StudioDesk::stepSwap(Context& ctx, game::ModelBench& bench, const std::string& world) {
    if (swap_ == Swap::Idle) return;
    // Three frames held before each shot, so what is shot is the held pose and not the frame
    // the hold began on; the shot is of the frame being drawn now, the panels left off it.
    if (++swapFrames_ < 3) return;
    if (swap_ == Swap::Before) {
        if (swapFrames_ == 3) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, (swapDir_ + "/before.png").c_str());
            return;
        }
        if (!bench.reopen(ctx.paths.assets, world, ctx.textures)) {
            core::logError("studio: the swap could not put the subject back");
        }
        bench.replayClip();
        shelfCategory_ = -1;   // the lists were read again
        swap_ = Swap::After;
        swapFrames_ = 0;
        return;
    }
    if (swapFrames_ == 3) {
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, (swapDir_ + "/after.png").c_str());
        return;   // the panels stay off until this frame is drawn
    }
    swap_ = Swap::Idle;
    lastSwap_ = swapDir_;
    // The sheet, a moment later: the after shot is written when this frame is.
    const std::string mu = shelfRoot_ + "/mu";
    const std::string before = swapDir_ + "/before.png", after = swapDir_ + "/after.png";
    const std::string sheet = swapDir_ + "/compare.png";
    const std::string script = "sleep 1; exec \"$0\" compare \"$1\" \"$2\" --out \"$3\" > /dev/null 2>&1";
    char* argv[] = {const_cast<char*>("/bin/sh"), const_cast<char*>("-c"),
                    const_cast<char*>(script.c_str()), const_cast<char*>(mu.c_str()),
                    const_cast<char*>(before.c_str()), const_cast<char*>(after.c_str()),
                    const_cast<char*>(sheet.c_str()), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, "/bin/sh", nullptr, nullptr, argv, environ) == 0) comparePid_ = pid;
}

void StudioDesk::panels(Context& ctx, game::ModelBench& bench, const std::string& world) {
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
    shelf_.swapped = lastSwap_.empty() ? std::string() : lastSwap_ + "/compare.png";
    watchJobs(bench);
    if (frame_++ == swapAt_ && swap_ == Swap::Idle) {
        startSwap(bench, bench.browseName(bench.browseIndex()));
    }
    stepSwap(ctx, bench, world);

    const double now = std::chrono::duration<double>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const studio::Asked asked = panels_.draw(shelf_, cooker_, float(ctx.window.width()),
                                             float(ctx.window.height()), now);
    if (holding()) return;   // nothing moves the subject while it is being shot
    if (asked.openSheet) {
        char* argv[] = {const_cast<char*>("/usr/bin/open"), const_cast<char*>(shelf_.swapped.c_str()),
                        nullptr};
        pid_t pid = 0;
        if (posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv, environ) == 0) {
            ::waitpid(pid, nullptr, 0);   // open(1) hands it to Preview and returns at once
        }
    }
    if (asked.reread) startSwap(bench, bench.browseName(bench.browseIndex()));
    if (asked.pick >= 0 || asked.category >= 0) lastSwap_.clear();   // another subject
    if (asked.category >= 0) {
        bench.setCategory(size_t(asked.category), ctx.textures);
        core::logf("browser: %s", bench.browseLine().c_str());
    }
    if (asked.pick >= 0) {
        bench.step(asked.pick - int(bench.browseIndex()), ctx.textures);
        core::logf("browser: %s", bench.browseLine().c_str());
    }
}

void StudioDesk::end() {
    // Built every frame, as ImGui wants, and left undrawn while a swap is being shot.
    gui_.end(swap_ == Swap::Idle);
}

void StudioDesk::shutdown() {
    cooker_.close();
    gui_.close();
}

}  // namespace mu::app
