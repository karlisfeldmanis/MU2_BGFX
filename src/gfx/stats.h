// What a run measured. A row a frame while it runs, and a table of medians and 99th
// percentiles per account when it ends; the gate reads the medians. See docs/budget.md.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "core/args.h"
#include "gfx/views.h"

namespace mu::gfx {

struct Frame {
    double cpuMs = 0.0;
    double gpuMs = 0.0;
    double viewMs[ViewCount] = {};
    uint32_t draws = 0;
};

class Stats {
public:
    // `csvPath` may be empty; the summary is written either way.
    void begin(const std::string& csvPath, const std::vector<core::BudgetOverride>& overrides);

    // Reads bgfx's own counters for the frame just submitted. `cpuMs` is ours: the wall
    // time of the game's own work, measured around the frame.
    //
    // The caller does not call this on a frame it asked for a screenshot on: the readback
    // stalls that one frame to a quarter of a second, and averaging it in is what made
    // sprint 2's published frame times a measurement of the reviewer's camera. See app/application.cpp.
    void sample(double cpuMs);

    // Writes the table into the log. Returns false when an account's median is over its
    // allowance, which is what `--budget` turns into an exit code.
    // Ends one measured segment and reports it, keeping only its mean frame time. Used by
    // --repeat: the point of several segments in one process is that the assets are loaded
    // once, so what is left between them is the machine's own drift rather than 20 seconds
    // of texture decoding each time.
    void endSegment(int index, int count);

    bool finish(bool enforce);

private:
    void reportSegments();

public:

private:
    double allowance(Account a) const;

    std::vector<Frame> frames_;
    std::vector<double> segmentMeans_;
    std::vector<core::BudgetOverride> overrides_;
    FILE* csv_ = nullptr;
    // The first frames hold the pipeline compiles and the first upload of everything, and
    // are dropped from every summary.
    static constexpr size_t kWarmup = 30;
};

}  // namespace mu::gfx
