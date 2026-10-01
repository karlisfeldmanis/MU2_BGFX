// The performance sweep: one launch, the world loaded once, the hero put down on a list of
// tiles in turn, and each tile's frames measured and written as a JSON row.
//
// tools/perfsweep.py writes the list and reads the rows; docs/budget.md ("The sweep") says how
// to read them. It exists because the Lost Tower was found over budget on 2026-10-01 only
// because somebody happened to measure it by hand: a world now gets walked for its bad places
// rather than sampled at the one spot somebody chose.
//
// What it touches in the game is the hero's tile (Play::setDown, the realm's setHeroDown with
// no warp drawn) and, with "fight" on, the same Attack request a click raises. It draws no die
// and runs only in a windowed play run, so no seeded headless log can move for it.
//
// A frame is the Application's: the sweep is told before the mode's frame and after the
// statistics have taken it, and what was logged in between -- a sound, a texture read, a
// warning -- is that frame's, with the realm's happenings of any tick that ran in it. A frame
// over twice its tile's mean is a hitch and is written with those, so a cause comes with the
// number.
#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace mu::game {
class Play;
}

namespace mu::app {

struct Context;
class Mode;

class Sweep {
public:
    // Reads `ctx.args.sweepPath`, finds the realm behind `mode`, adds the grid's tiles when the
    // list asks for them, and writes the header row. False is a run that cannot sweep: no list,
    // no realm (--play is required), or nowhere to write.
    bool open(Context& ctx, Mode& mode);
    bool active() const { return play_ != nullptr && !done_; }
    bool done() const { return done_; }

    // Before the mode's frame: the hero put down at a tile's first settling frame, and the
    // fight hand. After the frame was sampled: its time and what happened in it.
    void before();
    void after(double frameMs);
    // A handoff (a gate, Switch Character) ends the sweep where it stands: the rows written
    // are kept, and the run says it was cut short.
    void abort(const char* why);
    void close();

private:
    struct Spot {
        std::string name;
        int column = 0, row = 0;
        bool hot = false;  // from the hand list, not the grid
    };
    struct Sample {
        float ms = 0.0f, gpu = 0.0f;
        uint32_t draws = 0, tris = 0;
        int cause = -1;  // into causes_, or -1 when nothing was logged or happened
    };

    void putDown(const Spot& spot);
    void fightHand();
    std::string happeningsNow();
    void writeSpot();

    game::Play* play_ = nullptr;
    FILE* out_ = nullptr;
    std::vector<Spot> spots_;
    int settle_ = 90, frames_ = 400, passes_ = 1;
    bool fight_ = false;
    float fightReach_ = 6.0f;

    int pass_ = 0;
    size_t spot_ = 0;
    int frame_ = 0;  // within this tile's settle + measure
    bool done_ = false;

    // This tile's frames and what the hitches among them were about.
    std::vector<Sample> samples_;
    std::vector<std::string> causes_;
    float settleWorst_ = 0.0f;
    int settleWorstCause_ = -1;
    int landedColumn_ = 0, landedRow_ = 0;
    int64_t lastTick_ = -1;
    double tickMsSum_ = 0.0;
    int ticks_ = 0;
    int awakeMost_ = 0;
    double nearSum_ = 0.0;
    int blows_ = 0, kills_ = 0, deaths_ = 0;
    uint32_t target_ = 0;

    // What the log said since before(): filled by core::logTap, from any thread.
    std::mutex lock_;
    std::vector<std::string> said_;
    static void tap(const char* line, void* self);
};

}  // namespace mu::app
