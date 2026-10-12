// The studio's arena (docs/order-plan.md §9c): a monster fought inside the studio's own window.
//
// The user, 2026-10-12: "if we cook new monster on aida, we see it on aida map where it will be
// placed and character fighting monster" -- and the fight in the studio, not in a second window.
// It is the game's own arena, PlayMode raised with `--arena <breed> --world <w>` as the command
// line raises it (a level-80 knight with Kris against the breed, the sim deciding every blow),
// hosted by the studio's BenchMode: while it is up, the frame is the arena's and the studio's
// panels are drawn over it. It stands on the monster's own spawn tile (`--at`), not on the
// patch Play::Arena keeps for Lorencia.
//
// The arena runs on arguments of its own, swapped into the context around each of its calls and
// back out, so the studio's --frames, Escape and shots stay the studio's. While it runs, every
// line logged but the studio's own is copied to workshop/logs/mu/arena.log (core::logTap), the
// file the window arena wrote and the cooker MCP's `arena_log` reads.
#pragma once

#include <cstdio>
#include <memory>
#include <string>

#include "app/mode.h"
#include "core/args.h"

namespace mu::app {

class PlayMode;

class StudioArena {
public:
    StudioArena();
    ~StudioArena();
    // At the start of a frame, before the panels begin theirs: the preloader draws its own.
    bool open(Context& ctx, const std::string& breed, const std::string& world, int column, int row,
              const std::string& logPath);
    void frame(Context& ctx, const Frame& at);
    void close(Context& ctx);
    bool up() const { return play_ != nullptr; }
    const gfx::Camera& camera() const;
    const std::string& breed() const { return breed_; }
    const std::string& world() const { return world_; }

private:
    std::unique_ptr<PlayMode> play_;
    core::Args args_;   // the arena's own
    std::string breed_, world_;
    FILE* log_ = nullptr;
};

}  // namespace mu::app
