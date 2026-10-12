#include "app/modes/studio_arena.h"

#include <cstring>
#include <utility>

#include "app/modes/play_mode.h"
#include "core/log.h"

namespace mu::app {
namespace {

// The arena's arguments live in the context only while it runs.
struct Swap {
    Swap(Context& ctx, core::Args& theirs) : ctx_(ctx), theirs_(theirs) { std::swap(ctx_.args, theirs_); }
    ~Swap() { std::swap(ctx_.args, theirs_); }
    Context& ctx_;
    core::Args& theirs_;
};

void tap(const char* line, void* user) {
    if (std::strncmp(line, "studio:", 7) == 0) return;
    std::fputs(line, static_cast<FILE*>(user));
    std::fputc('\n', static_cast<FILE*>(user));
}

}  // namespace

StudioArena::StudioArena() = default;
StudioArena::~StudioArena() = default;

bool StudioArena::open(Context& ctx, const std::string& breed, const std::string& world, int column, int row,
                       const std::string& logPath) {
    close(ctx);
    log_ = std::fopen(logPath.c_str(), "w");
    if (log_) {
        std::setvbuf(log_, nullptr, _IOLBF, 0);
        core::logTap(&tap, log_);
    }
    // core/args.cpp's arena, decided as the command line decides it: played, a level-80 hero
    // with Kris. The rest is the studio's own (the window, the sheets, mute).
    args_ = ctx.args;
    args_.arena = breed;
    args_.world = world;
    args_.play = true;
    if (!args_.levelAsked) args_.level = 80;
    if (!args_.weaponAsked) args_.weapon = "Sword01";
    args_.atSet = column >= 0;
    args_.atColumn = float(column), args_.atRow = float(row);
    // Not zero: no entrance, Escape left the studio's, and never a save (play_mode.cpp).
    args_.frames = ctx.args.frames != 0 ? ctx.args.frames : 1;
    args_.entrance = false;
    play_ = std::make_unique<PlayMode>();
    bool ok;
    {
        Swap swap(ctx, args_);
        ok = play_->open(ctx);
    }
    if (!ok) {
        core::logError("studio: the arena for %s in %s did not open", breed.c_str(), world.c_str());
        {
            Swap swap(ctx, args_);
            play_->shutdown(ctx);
        }
        play_.reset();
        if (log_) {
            core::logTap(nullptr, nullptr);
            std::fclose(log_);
            log_ = nullptr;
        }
        return false;
    }
    play_->watch(true);   // the fight's readings, not the hero's orbs and belt under the panels
    breed_ = breed;
    world_ = world;
    core::logf("studio: the arena for %s in %s at %d,%d, in the studio", breed.c_str(), world.c_str(), column, row);
    return true;
}

void StudioArena::frame(Context& ctx, const Frame& at) {
    if (!play_) return;
    Swap swap(ctx, args_);
    play_->frame(ctx, at);
}

void StudioArena::close(Context& ctx) {
    if (!play_) return;
    {
        Swap swap(ctx, args_);
        play_->shutdown(ctx);
    }
    play_.reset();
    if (log_) {
        core::logTap(nullptr, nullptr);
        std::fclose(log_);
        log_ = nullptr;
    }
    core::logf("studio: the arena for %s closed", breed_.c_str());
    breed_.clear();
}

const gfx::Camera& StudioArena::camera() const { return play_->camera(); }

}  // namespace mu::app
