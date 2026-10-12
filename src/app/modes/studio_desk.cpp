#include "app/modes/studio_desk.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#include <bgfx/bgfx.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include "app/context.h"
#include "core/log.h"
#include "game/bench.h"
#include "game/figures.h"
#include "imgui.h"

extern "C" char** environ;

namespace mu::app {

bool StudioDesk::open(Context& ctx) {
    // Open Sans, the face the game's warnings are set in; ImGui's own if it is not there.
    if (!gui_.open(ctx.window, ctx.paths.root + "/extern/OpenSans-SemiBold.ttf", 15.0f)) {
        core::logError("studio: the panels did not open; the bench runs without them");
        return false;
    }
    shelfRoot_ = ctx.paths.root;
    openControl(ctx);
    // The scenes: the Stage, Home, and every world with a cooked town to stand in.
    shelf_.scenes = {"Stage", "Home"};
    std::error_code ignored;
    std::vector<std::string> worlds;
    for (const auto& entry : std::filesystem::directory_iterator(ctx.paths.assets + "/cooked", ignored)) {
        const std::string name = entry.path().filename().string();
        if (std::filesystem::exists(entry.path() / (name + ".mut"), ignored)) worlds.push_back(name);
    }
    std::sort(worlds.begin(), worlds.end());
    shelf_.scenes.insert(shelf_.scenes.end(), worlds.begin(), worlds.end());
    if (const char* script = std::getenv("MU_STUDIO_DO")) {
        std::string all = script;
        for (size_t start = 0; start < all.size();) {
            size_t end = all.find(';', start);
            if (end == std::string::npos) end = all.size();
            const std::string one = all.substr(start, end - start);
            const size_t colon = one.find(':');
            if (colon != std::string::npos) script_.push_back({std::atoi(one.c_str()), one.substr(colon + 1)});
            start = end + 1;
        }
    }
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
        // Or what Place's card names: the world's placements, its ground. Those were never the
        // subject's, and a placements cook went unanswered until sprint G compared this too.
        const std::string here = targetOf(bench);
        const bool placed = placing_ && job.target == shelf_.target;
        if (!placed && job.target != here && "Arm" + job.target != here) continue;
        if (job.target.rfind("placements ", 0) == 0 || job.target.rfind("ground ", 0) == 0) {
            // The world's placements or its land cooked: read the world again, so the lamps and
            // the fires stand where their objects now do, and the land is the painted one.
            core::logf("studio: job %d cooked %s; reading the world again", job.number, job.target.c_str());
            sceneAsked_ = "reload";
            place_.drop();
            if (job.target.rfind("ground ", 0) == 0) paint_.cooked();
            continue;
        }
        core::logf("studio: job %d cooked %s; swapping it in", job.number, job.target.c_str());
        startSwap(bench, job.target);
        // And the fight, if one is up: it reads the cooked tables and shows at its start.
        if (arenaUp()) openArena(arenaBreed_, arenaWorld_);
    }
    seenAll_ = true;
}

bool StudioDesk::arenaUp() {
    if (arenaPid_ > 0 && ::waitpid(arenaPid_, nullptr, WNOHANG) == arenaPid_) {
        core::logf("studio: the arena window was closed");
        arenaPid_ = 0;
    }
    return arenaPid_ > 0;
}

void StudioDesk::closeArena() {
    if (!arenaUp()) return;
    ::kill(arenaPid_, SIGTERM);
    ::waitpid(arenaPid_, nullptr, 0);
    arenaPid_ = 0;
}

void StudioDesk::openArena(const std::string& breed, const std::string& world) {
    closeArena();
    // The game binary itself, as the studio is: what is fought there is what players fight.
    const std::string self = shelfRoot_ + "/build/mu2";
    const std::string log = shelfRoot_ + "/workshop/logs/mu/arena.log";
    char* argv[] = {const_cast<char*>(self.c_str()), const_cast<char*>("--arena"),
                    const_cast<char*>(breed.c_str()), const_cast<char*>("--world"),
                    const_cast<char*>(world.c_str()), const_cast<char*>("--log"),
                    const_cast<char*>(log.c_str()), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, self.c_str(), nullptr, nullptr, argv, environ) != 0) {
        core::logError("studio: the arena would not start");
        return;
    }
    arenaPid_ = pid;
    arenaBreed_ = breed;
    arenaWorld_ = world;
    core::logf("studio: the arena for %s in %s (pid %d), its log in workshop/logs/mu/arena.log",
               breed.c_str(), world.c_str(), pid);
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

void StudioDesk::panels(Context& ctx, game::ModelBench& bench, const std::string& world,
                        game::World& town, const gfx::Camera& camera, bool onStage) {
    // Place: only over a town, and before the panels so a pick is this frame's.
    if (onStage) placing_ = false;
    tookPointer_ = false;
    if (placing_) {
        place_.world(ctx, world);
        marks_.world(ctx, world);
        marks_.kind(ctx, placeMode_ == 1 ? StudioMarks::Kind::Gates : StudioMarks::Kind::Townsfolk);
        paint_.world(ctx, world);
        const bool idle = swap_ == Swap::Idle, over = gui_.wantsPointer();
        tookPointer_ = idle && (placeMode_ == 0   ? place_.pointer(ctx, town, camera, over)
                                : placeMode_ == 3 ? paint_.pointer(ctx, town, camera, over)
                                                  : marks_.pointer(ctx, town, camera, over));
    }
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
    shelf_.target = !placing_          ? targetOf(bench)
                    : placeMode_ == 0  ? "placements " + world
                    : placeMode_ == 3  ? paint_.target()
                                       : marks_.target();
    shelf_.placing = placing_;
    shelf_.clip = bench.hasFigure() ? bench.clipLine() : std::string();
    shelf_.time = std::string(ctx.time.name()) + "    T changes the time of day";
    shelf_.swapped = lastSwap_.empty() ? std::string() : lastSwap_ + "/compare.png";
    shelf_.plus = bench.plus();
    const game::FigureBody* body = bench.subjectBody();
    shelf_.monster = body && body->kind == game::BodyKind::Monster;
    shelf_.arena = arenaUp();
    watchJobs(bench);
    for (const auto& [at, what] : script_) {
        if (at != frame_) continue;
        const std::string answer = act(ctx, bench, world, town, what);
        core::logf("studio: frame %d does '%s' (MU_STUDIO_DO): %s", at, what.c_str(), answer.c_str());
    }
    serveControl(ctx, bench, world, town);
    ++frame_;
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
    if (asked.plus >= 0) plusAsked_ = asked.plus;
    if (asked.place >= 0) {
        placing_ = asked.place == 1;
        if (!placing_) place_.drop();
    }
    if (placing_) {
        float side[4];
        panels_.sideRect(side);
        const int mode = placeModeBar(side[0], side[1], side[2], placeMode_);
        if (mode != placeMode_) {
            placeMode_ = mode;
            place_.drop();
            marks_.drop();
        }
        const float bar = ImGui::GetFrameHeightWithSpacing() + 2.0f * ImGui::GetStyle().WindowPadding.y + 4.0f;
        if (placeMode_ == 0) {
            place_.panel(ctx, town, cooker_, camera, side[0], side[1] + bar, side[2], side[3] - bar);
        } else if (placeMode_ == 3) {
            paint_.panel(ctx, town, cooker_, camera, side[0], side[1] + bar, side[2], side[3] - bar);
        } else {
            marks_.panel(ctx, town, cooker_, camera, side[0], side[1] + bar, side[2], side[3] - bar);
        }
    }
    if (asked.arena == 1 && body) openArena(body->name, world);
    if (asked.arena == 0) closeArena();
    if (asked.scene >= 0 && asked.scene < int(shelf_.scenes.size())) {
        shelf_.scene = asked.scene;
        sceneAsked_ = shelf_.scenes[size_t(asked.scene)];
        shelfCategory_ = -1;   // a world's lists differ
    }
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

std::string StudioDesk::takeScene() {
    std::string scene;
    scene.swap(sceneAsked_);
    return scene;
}

void StudioDesk::end() {
    // Built every frame, as ImGui wants, and left undrawn while a swap is being shot or the
    // control socket asked for the panels hidden.
    gui_.end(swap_ == Swap::Idle && !hidden_);
}

void StudioDesk::shutdown() {
    closeControl();
    closeArena();   // the arena is the studio's window; the cook (mu serve) is not
    cooker_.close();
    gui_.close();
}

}  // namespace mu::app
