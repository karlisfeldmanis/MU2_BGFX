// The Graphics rows found on this PC, at its own screen, on the first character screen (the
// user, 2026-10-17: "first time run on loading screen where we find optimal setting for
// fullscreen experience", "at least 60fps", and "we can even do this before character creation
// screen because most of data we already have there").
//
// The launcher's preset is a guess from the card's name and memory (launcher/gpu.h presetFor)
// and never sees the screen; core::easeGraphics only steps down, and only after eight slow
// seconds the player has sat through. So the character screen comes up from its black a few
// seconds later, "Setting up graphics for your PC", while it is drawn at each preset in turn --
// uncapped, vsync held off -- and the heaviest whose GPU frame keeps inside kBudgetMs is kept,
// with the frame rate capped at the display's refresh.
//
// The GPU's own time, not the frame's: the character screen's frame is the CPU's (Gordichan's
// GTX 1650, 2026-10-17: 7 ms a frame there and in Noria alike), and a preset moves the GPU. And
// a stricter budget than a town's: the same card drew the character screen in 2.0 ms of GPU and
// Noria in 3.4, 181 draws against 550. 6.5 ms here is about 11 in a town, a third under 60's
// 16.7 kept for crowds, rain and a warm laptop; easeGraphics still steps down in the world if
// a card does worse there. A card with no GPU timer is measured by its whole frame, at 12.5.
//
// It climbs from the launcher's preset and steps down from it, one preset at a time, never
// jumping: a weak card asked for a frame far past it loses its device (Salvi's Iris Plus,
// 2026-10-15), so a frame over kSpikeMs ends a climb at once. Textures and the renderer are
// left alone (a change of either is a restart).
//
// Once per card: options.txt keeps `tuned 1` and `tunedgpu` (vendor and device), and a run on
// another card tunes again. Never once a Graphics row was set by hand (`hand 1`).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/args.h"

namespace mu::gfx {
class Window;
}

namespace mu::app {

class Tuner {
public:
    static constexpr double kBudgetMs = 6.5;         // the 90th percentile GPU frame here
    static constexpr double kFrameBudgetMs = 12.5;   // the whole frame, with no GPU timer
    static constexpr double kSpikeMs = 250.0;  // one frame this long ends a climb

    // A player's run on a card it has not tuned for, whose rows nobody set by hand.
    static bool wanted(const core::Args& args, uint32_t gpu);

    // From the preset the rows sit in; the cap off and vsync held off until done.
    void start(core::Args& args, gfx::Window& window, uint32_t gpu);
    // One frame: the GPU's time (negative where the renderer keeps none) and the wall's; the
    // rows move between steps. False once done (or not started). `counts` false for a frame that
    // says nothing of the card: minimised, out of focus.
    bool frame(core::Args& args, gfx::Window& window, double gpuMs, double frameMs, bool counts);
    // Put back as it was, nothing kept: the run left the screen before the tuning ended.
    void abort(core::Args& args, gfx::Window& window);

    bool active() const { return active_; }
    float progress() const;
    // The preset kept, once done; null before.
    const char* chosen() const { return chosen_; }

private:
    void finish(core::Args& args, gfx::Window& window, int preset);

    bool active_ = false;
    int at_ = 0;           // the preset being measured
    int best_ = -1;        // the heaviest that held, so far
    int first_ = 0;        // the preset it started from
    int direction_ = 0;    // 0 the first step, +1 climbing, -1 stepping down
    int steps_ = 0;
    double stepMs_ = 0.0;  // into the step, warm-up and measuring
    double totalMs_ = 0.0;
    bool spiked_ = false;
    bool byFrame_ = false;  // no GPU timer: the whole frame against kFrameBudgetMs
    std::vector<float> samples_;
    int cap_ = 0;
    uint32_t gpu_ = 0;
    const char* chosen_ = nullptr;
    std::string said_;  // each step's 90th percentile, for the log line
};

}  // namespace mu::app
