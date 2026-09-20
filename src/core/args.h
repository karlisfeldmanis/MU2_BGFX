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
    int repeat = 1;              // measure this many segments of `frames`, loading once
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
    int msaa = 4;            // samples on the prepass, depth and shade targets

    // The world. A name under assets/world/; empty runs the model bench instead.
    std::string world;
    // Chunk culling on the town, on by default. --no-cull is how the two are compared, and
    // the answer to whether chunking earns its keep is the difference between them.
    bool cullChunks = true;
    // Which tile the world camera looks at. `atSet` rather than a negative sentinel: a
    // negative column used to mean "not given", so `--at -5,3` was silently the default and
    // the run reported a frame from somewhere the caller never asked for.
    bool atSet = false;
    float atColumn = 0.0f, atRow = 0.0f;

    // How many monsters stand in the town's crowd. The sprint's sentence is thirty; -1 is
    // "every breed's whole spawn count", which is Lorencia's real 290 and is what the
    // crowd's cost is measured against when it is asked for.
    int crowd = 30;
    // --no-figures leaves the cooked figures unopened altogether, which is the baseline the
    // crowd's own cost is measured against: --crowd 0 still stands the Dark Knight and the
    // town's own fourteen.
    bool figuresOn = true;

    // The monster bench: one figure on the bench ground, by the name index.json gives it,
    // playing one clip. `--clip` is MU's own action number, in the right table of the two --
    // a monster's 4 is its second swing and a player's is "Stop sword".
    std::string figure;
    int clip = -1;

    bool valid = true;
};

// Parses argv. Complains into the log and clears `valid` on anything it does not know,
// rather than carrying on with a switch the caller thinks took effect.
Args parseArgs(int argc, char** argv);

void printUsage();

}  // namespace mu::core
