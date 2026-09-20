// The command line. Every switch a review run needs is here, and nothing that belongs in a
// sheet: a number that is tuned belongs in sheets/, a number that describes the run belongs
// here.
#pragma once

#include <string>
#include <vector>

namespace mu::core {

struct BudgetOverride {
    std::string account;
    double ms;
};

struct Args {
    int width = 1920;
    int height = 1080;
    bool vsync = false;  // off for every measurement; see docs/budget.md

    // The review loop.
    int frames = 0;              // 0 plays until the window closes
    int shotEvery = 0;           // a PNG every N frames, and one on the last frame
    std::string shotPath;        // absolute; the directory shots land in
    std::string logPath;         // absolute; mu2.log beside the executable by default
    std::string statsPath;       // absolute; a csv row a frame

    // The gate. Empty means every account is checked at its documented allowance.
    bool budget = false;
    std::vector<BudgetOverride> budgetOverrides;

    // The bench. A model is a path to a .glb; empty raises the ground alone.
    std::string model;
    std::string sheet;       // absolute; sheets/lighting.json by default
    float distance = 0.0f;   // camera distance in world units; 0 frames on the model
    bool still = false;      // hold the camera instead of turning it

    bool valid = true;
};

// Parses argv. Complains into the log and clears `valid` on anything it does not know,
// rather than carrying on with a switch the caller thinks took effect.
Args parseArgs(int argc, char** argv);

void printUsage();

}  // namespace mu::core
