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

    // Sprint 5's sim. `--headless` runs the tick with no window at all; the seed and the
    // tick count are the whole of a reproducible run, and `--sim-log` is where its bytes go.
    bool headless = false;
    uint64_t seed = 1;
    int ticks = 10000;
    std::string simLog;     // absolute; build/hunt.log by default
    bool simSteps = false;  // put every tile crossing in the log too, which is most of it
    bool noHand = false;    // no scripted player: the nests alone, which is the AI's own cost
    int kin = 2;            // mu.db's enumeration: 0 Dark Wizard, 1 Fairy Elf, 2 Dark Knight
    int level = 1;
    // Which stat the scripted hand puts a levelled character's points into. A character made
    // at level 20 has 95 points in hand and, unspent, he is a level-1 character with more
    // health -- which is a fair thing to be able to measure and a poor hunt to watch.
    std::string spend = "strength";
    // `--play` raises the realm behind the window: the sim ticks, the figures are where it says
    // they are, and a click is a request. Without it a world is the still crowd sprint 4 drew.
    bool play = false;
    // A review harness and not a feature: every N frames it puts the pointer on a pixel from a
    // short fixed list and clicks it, through the same unprojection a hand would. It is how a
    // run with nobody at the mouse can show that a click walks and a click on a monster fights.
    int demoClicks = 0;

    // The monster bench: one figure on the bench ground, by the name index.json gives it,
    // playing one clip. `--clip` is MU's own action number, in the right table of the two --
    // a monster's 4 is its second swing and a player's is "Stop sword".
    std::string figure;
    int clip = -1;
    // Stands the bench's figure as a safe zone does: weapon on the back, unarmed idle. It is
    // how every figure in Lorencia's town square stands, and the only way to judge the slung
    // arrangement without walking the camera into the square.
    bool safe = false;

    bool valid = true;
};

// Parses argv. Complains into the log and clears `valid` on anything it does not know,
// rather than carrying on with a switch the caller thinks took effect.
Args parseArgs(int argc, char** argv);

void printUsage();

}  // namespace mu::core
