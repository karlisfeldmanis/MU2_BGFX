#include "app/tuner.h"

#include <algorithm>
#include <cstdio>

#include "core/log.h"
#include "gfx/window.h"

namespace mu::app {

namespace {

constexpr double kWarmMs = 600.0;      // a step's first frames: its targets and pipelines made
constexpr double kMeasureMs = 1250.0;  // then measured
constexpr double kMostMs = 20000.0;    // the whole of it, whatever happens
constexpr size_t kFewest = 10;         // a step with fewer frames than this measured nothing
constexpr int kTop = core::kGraphicsPresetCount - 1;

int percent(float scale) { return int(scale * 100.0f + 0.5f); }

// The rows of preset `i`, the Textures and the renderer left as they are.
void wear(core::Args& args, int i) {
    const core::GraphicsPreset& p = core::kGraphicsPresets[i];
    args.scale = float(p.scale) / 100.0f;
    args.msaa = p.msaa;
    args.shadows = p.shadows;
    args.ssao = p.ssao;
    args.bloom = p.bloom;
    args.reflections = p.reflections;
    args.grass = p.grass;
    args.lights = p.lights;
}

// The heaviest preset the rows are all at least: High's rows are High, the launcher's low-end
// set (whole scale, no MSAA, the effects off) is Low, not High for its scale alone.
int presetOf(const core::Args& args) {
    int at = 0;
    for (int i = 0; i < core::kGraphicsPresetCount; ++i) {
        const core::GraphicsPreset& p = core::kGraphicsPresets[i];
        if (p.scale <= percent(args.scale) && p.msaa <= args.msaa && p.shadows <= args.shadows &&
            (!p.ssao || args.ssao) && (!p.bloom || args.bloom) &&
            (!p.reflections || args.reflections) && p.grass <= args.grass &&
            (!p.lights || args.lights)) {
            at = i;
        }
    }
    return at;
}

}  // namespace

bool Tuner::wanted(const core::Args& args, uint32_t gpu) {
    return args.remember && args.frames == 0 && !args.offscreen && !args.graphicsByHand &&
           !(args.tuned && args.tunedGpu == gpu);
}

void Tuner::start(core::Args& args, gfx::Window& window, uint32_t gpu) {
    active_ = true;
    gpu_ = gpu;
    first_ = at_ = presetOf(args);
    best_ = -1;
    direction_ = 0;
    steps_ = 0;
    stepMs_ = totalMs_ = 0.0;
    spiked_ = false;
    byFrame_ = false;
    samples_.clear();
    said_.clear();
    cap_ = args.cap;
    args.cap = 0;
    window.holdVsync(true);
    wear(args, at_);
    core::logf("graphics: tuning for this PC at %dx%d, from %s", window.width(), window.height(),
               core::kGraphicsPresets[at_].name);
}

float Tuner::progress() const {
    if (!active_) return chosen_ ? 1.0f : 0.0f;
    // Three steps is the usual: the launcher's preset, one past it, and back.
    const double done = double(steps_) + std::min(1.0, stepMs_ / (kWarmMs + kMeasureMs));
    return float(std::min(0.95, done / 3.0));
}

bool Tuner::frame(core::Args& args, gfx::Window& window, double gpuMs, double frameMs,
                  bool counts) {
    if (!active_) return false;
    totalMs_ += frameMs;
    if (!counts) {
        // Minimised or behind another window: the step starts over once he is back.
        stepMs_ = 0.0;
        samples_.clear();
        spiked_ = false;
    } else {
        stepMs_ += frameMs;
        // A climb ends on one frame the card could not draw in time, warm or not: the next
        // could be the one Windows resets the driver over.
        if (direction_ > 0 && frameMs > kSpikeMs) spiked_ = true;
        if (gpuMs < 0.0) byFrame_ = true;
        if (stepMs_ > kWarmMs) samples_.push_back(float(byFrame_ ? frameMs : gpuMs));
    }
    if (totalMs_ > kMostMs) {
        // Nothing measured -- the window never in front, never shown -- is nothing to keep: the
        // rows go back and the next start measures. Part of it is the steps that were taken.
        if (steps_ == 0) {
            abort(args, window);
            return false;
        }
        core::logf("graphics: tuning took over %.0f s; kept what held", kMostMs / 1000.0);
        finish(args, window, best_ >= 0 ? best_ : std::min(first_, at_));
        return false;
    }
    if (!spiked_ && stepMs_ < kWarmMs + kMeasureMs) return true;
    if (!spiked_ && samples_.size() < kFewest) return true;

    float p90 = float(kSpikeMs);
    if (!samples_.empty()) {
        std::vector<float> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        p90 = sorted[size_t(0.9 * double(sorted.size() - 1))];
    }
    const bool held = !spiked_ && p90 <= (byFrame_ ? kFrameBudgetMs : kBudgetMs);
    char one[64];
    std::snprintf(one, sizeof one, "%s%s %.1f ms%s", said_.empty() ? "" : ", ",
                  core::kGraphicsPresets[at_].name, double(p90), spiked_ ? " (a spike)" : "");
    said_ += one;
    ++steps_;
    stepMs_ = 0.0;
    samples_.clear();
    spiked_ = false;

    if (held) best_ = at_;
    if (direction_ == 0) direction_ = held ? +1 : -1;
    int next = -1;
    if (direction_ > 0 && held && at_ < kTop) next = at_ + 1;
    if (direction_ < 0 && !held && at_ > 0) next = at_ - 1;
    if (next < 0) {
        // Climbing, the last that held; stepping down, the first that held, or Lowest.
        finish(args, window, best_ >= 0 ? best_ : 0);
        return false;
    }
    at_ = next;
    wear(args, at_);
    return true;
}

void Tuner::finish(core::Args& args, gfx::Window& window, int preset) {
    active_ = false;
    wear(args, preset);
    chosen_ = core::kGraphicsPresets[preset].name;
    // At least 60, and no more than the display shows: uncapped, a laptop drew 150 frames a
    // second for nobody and lost its device to the heat (Gordichan, 2026-10-17). The display's
    // and not the cap there was: nobody set the Graphics by hand (wanted), so a cap there is the
    // launcher's guess -- its low-end set's 60, on a card the guess got wrong -- or the start's.
    args.cap = std::clamp(window.refreshHz(), 60, 240);
    window.holdVsync(false);
    // And the launcher's other guess the measure can answer (the user, 2026-10-17: "i think
    // launcher could be false positive", "good enough textures"): Half textures, which no preset
    // step lifts. Every card draws Full: a texture's size is its memory, not its frame (the user,
    // 2026-10-18: Half on a low-end PC "but i was able to play with 60fps on low-end with 100%
    // texture quality"), and a card short of memory steps down by lightenOptions' lost device.
    // It takes a new start, so the game starts again here, on the character screen, measured.
    const bool textures = args.textures == 0;
    if (textures) {
        args.textures = 1;
        if (args.remember) args.restart = true;
    }
    args.tuned = true;
    args.tunedGpu = gpu_;
    core::logf("graphics: tuned to %s from the launcher's %s, capped at %d fps%s (%s; the 90th "
               "percentile %s, %.1f ms or under holds)",
               chosen_, core::kGraphicsPresets[first_].name, args.cap,
               textures ? ", textures Full from a new start" : "", said_.c_str(),
               byFrame_ ? "frame" : "GPU frame", byFrame_ ? kFrameBudgetMs : kBudgetMs);
}

void Tuner::abort(core::Args& args, gfx::Window& window) {
    if (!active_) return;
    active_ = false;
    wear(args, first_);
    args.cap = cap_;
    window.holdVsync(false);
    core::logf("graphics: tuning left unfinished (%s); the next start tries again",
               said_.empty() ? "nothing measured" : said_.c_str());
}

}  // namespace mu::app
