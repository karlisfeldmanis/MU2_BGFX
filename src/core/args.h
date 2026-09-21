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
    // The shadow probe. `--shadow-log` writes a csv row a frame of where the sun's split
    // stood against a grid fixed to the world, and how far the camera trails the character;
    // `--shadow-slide` moves the split alone this many millimetres a frame, so that with the
    // camera held any pixel that changes is the shadow's. docs/shadow-probe.md.
    std::string shadowLog;   // absolute
    float shadowSlideMm = 0.0f;
    // `--shadow-points` writes, a row a frame, where a fixed grid of ground points lands on
    // screen, so tools/pan.py can read the picture at the SAME world spots while the camera
    // moves. `--shadow-view` draws the sun's visibility alone; `--shadow-noise` is where the
    // penumbra's turn is anchored; `--shadow-size` the map's side in texels.
    std::string shadowPoints;  // absolute
    bool shadowView = false;
    int shadowNoise = -1;      // -1 leaves the renderer's own choice
    int shadowSize = 4096;
    // Every frame advances the world by exactly this many milliseconds instead of by the last
    // frame's wall time, so that two runs draw the same frames and can be compared picture by
    // picture. 0 is the wall clock.
    float fixedDtMs = 0.0f;
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
    // --no-lamps: no point lights and no flames, the baseline the lamps are priced against.
    // The glows still draw; they are the town's own meshes.
    bool lampsOn = true;

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
    // What the character holds, by index.json's own name: `--weapon Sword01 --shield Shield10`.
    // Empty is bare hands, which is a state worth running rather than a missing one.
    std::string weapon;
    std::string shield;
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
    // The cooked browser: step through every .mum the cook wrote, with the arrow keys. Not
    // the same thing as --model, which reads one glb: this reads what the game loads.
    bool browse = false;

    // The transparent pass's probe: N effect sprites in front of the camera, so that a pass
    // which has no account yet can be priced against the same scene with it empty.
    //
    // It is a probe and not `--bench effect`, and the difference matters. The bench judges
    // how one effect LOOKS; this measures what the pass COSTS, and the two want opposite
    // scenes -- the bench wants one sprite on a plain ground and the account is defined over
    // Lorencia's town. `--effect-size` drives the worst case the sprint file names: the cost
    // is fill rate, so one sprite filling the view is worse than thirty over a spider, and
    // ordinary play will not hand that over often enough to show up in a median.
    int effects = 0;
    float effectSize = 0.5f;        // half-extent in metres
    std::string effectSheet;        // an index.json effect name; empty takes the first cooked
    // Which of the browser's categories to open on, by a word out of its label -- "world",
    // "monsters", "people", "parts". Empty opens the first that has anything in it. This is
    // what makes a review run of the viewer possible at all: a run with --frames and --shot
    // has nobody at the keyboard to press tab.
    std::string category;
    // And which entry in it: the first whose name holds this, ignoring case. "budge" lands on
    // the Budge Dragon wherever it sits in the list.
    std::string pick;
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
