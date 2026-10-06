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

// The Graphics page's presets, which --graphics names too. What each row costs at 2K is
// docs/budget.md's table: the scale is most of it, then the shadows' taps and map, MSAA,
// bloom, reflections and grass. High is the game as it was drawn before there was a choice.
struct GraphicsPreset {
    const char* name;
    int scale, msaa, shadows;
    bool ssao, bloom, reflections;
    int grass;
};
inline constexpr GraphicsPreset kGraphicsPresets[] = {
    {"Low", 67, 1, 0, false, false, false, 1},
    {"Medium", 85, 2, 1, true, true, false, 2},
    {"High", 100, 4, 2, true, true, true, 2},
};
inline constexpr int kGraphicsPresetCount = 3;

struct Args {
    int width = 1920;
    int height = 1080;
    // The whole display, at its own mode, and --width/--height ignored. A measurement is
    // taken windowed at 1080p; this is for playing it.
    bool fullscreen = false;
    bool vsync = false;  // off for every measurement; see docs/budget.md
    // --remember: the game menu's Options kept across runs, in optionsPath(). Read where the
    // switch stands on the line, so main.sh's own --fullscreen and --vsync before it give way to
    // what the player chose and a switch after it still wins; written back whenever Options
    // changes something. Only main.sh asks for it, so no measurement run reads a player's file.
    bool remember = false;
    int volume = 100;  // percent: the Options page's Volume row
    // How much of the backbuffer the WORLD is drawn at, 1 for all of it. The present pass
    // magnifies it; the hover ring and the HUD are drawn at the screen's own size whatever
    // this is, so the plate and its text stay as sharp as the display. Held to 0.5 at the
    // bottom. The frame costs about 1.2 ms plus 1.55 ms a megapixel on this Mac, so 0.9 at
    // 2560x1440 is worth about 0.6 ms.
    float scale = 1.0f;
    // At 0.9 and under, the world is upscaled by Apple's MetalFX spatial scaler before the present,
    // which reconstructs edges a stretch would smear. --no-metalfx has the present stretch it
    // itself, sharply (fs_present's Catmull-Rom): 238 fps against 186 in a 2K Meteorite fight,
    // and the default for a few hours on 2026-10-05, until the user played it: "i think its was
    // pretties before". MetalFX is the look.
    bool metalfx = true;
    // Frames a second to hold the picture to, 0 for as fast as it will go. A pace, not a
    // limit on the work: it waits after the present and the wait is kept out of the
    // statistics. Every measurement is taken at 0. See the remark in application.cpp.
    int cap = 0;

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
    // bgfx's per-view GPU timers. OFF unless asked for, because on Metal they are not free:
    // with them on, bgfx opens a render pass for EVERY view rather than one per target, so
    // the transparent pass reloads the 4x MSAA colour and depth the shade pass just stored,
    // and every small pass pays a pass of its own. Measured 2026-09-24 at 2560x1273: 6.32 ms
    // with them, 5.50 and 5.59 without, and not a pixel different. --views turns them on, and
    // so do --stats (its csv has a column a view) and a named --budget claim (it is checked
    // against an account's share). A bare --budget enforces the wall frame and needs none.
    bool views = false;

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
    // The Options page's Graphics rows (app/options.h), kept in options.txt with `scale`,
    // `msaa` and `cap`. `shadows` is the map's side and the penumbra's taps together: 0 a
    // 2048 map with the turned taps, 1 a 4096 with the turned, 2 a 4096 with the still -- the
    // two costliest knobs on the 2K table in docs/budget.md. --shadow-size and --shadow-noise
    // name their own and outrank it.
    int shadows = 2;
    bool shadowAsked = false;
    bool ssao = true;
    bool bloom = true;
    bool reflections = true;  // the sky probe
    int grass = 2;            // 0 none, 1 thinned and nearer, 2 as the world's sheet has it

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
    // --birds-now: the first flock arrives at once instead of 20 to 90 seconds in. A review
    // run is a few seconds long and would otherwise never see one; nothing about how they fly
    // changes, only how long the sky stays empty before the first pass. See boids.h.
    bool birdsNow = false;
    // --weather rain|dry: hold the weather rather than take the world's own spells, so a review
    // run sees the rain without waiting out a dry spell; cycle: the world's own spells, short,
    // so a whole dry-wet-dry turn is watched in under two minutes; storm: raining, with a clap
    // and its lightning every 8 to 16 seconds. See game/world/weather.h.
    std::string weather;
    // --no-air: no birds and no leaves, the baseline they are reviewed and priced against.
    // A leaf is three pixels and a bird is half a metre, so "is it drawn?" is answered by
    // differencing two runs rather than by looking.
    bool airOn = true;

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
    bool weaponAsked = false;  // --weapon was named, so the arena's own default does not decide
    bool levelAsked = false;   // likewise for --level
    // `--play` raises the realm behind the window: the sim ticks, the figures are where it says
    // they are, and a click is a request. Without it a world is the still crowd sprint 4 drew.
    bool play = false;
    // The arena: ONE breed, `arenaCount` of them, on a clear patch of the map with the hero,
    // fighting from the first tick, and none of the map's own spawns anywhere. It is a played
    // run and not a bench -- the sim spawns the bodies, the rules decide every blow -- so it
    // implies --play, and --world lorencia when no world is named.
    //
    // The name is the cook's own, and either of the two it writes: the figure
    // (`SkeletonWarrior`, `BudgeDragon01`, and a prefix of one is enough -- `BudgeDragon`) or
    // the label (`Skeleton Warrior`), which is what mu2.log prints for a breed. Case and
    // spaces are ignored. A name that matches nothing lists what the map has and fails the run.
    std::string arena;
    int arenaCount = 1;
    int arenaLearn = 0;  // `--arena-learn N`: the arena's hero is taught skill N
    bool arenaUndying = false;  // `--arena-undying`: the arena's hero is never felled
    bool castleOpen = false;    // `--castle-open`: the Messenger's door open at any hour (a test)
    bool invasion = false;      // `--invasion`: the map's Golden Invasion begun at once (a test)
    // `--raid N`: the Golden Dragon's raid (docs/golden-dragon-raid.md) -- the party of
    // source/raid/party.json in one of WebZen's Lorencia Dragon Event boxes, the invasion begun at
    // once, the dragon tough for N; the hero wears the first kit. 0 for none.
    int raid = 0;
    char raidBox = 'A';         // `--raid-box A|B|C`: which box (DragonEvent.cpp:103-109)
    int raidStage = 0;          // `--raid-stage S`: the dragon laid at stage S's health once it stands
    bool raidNow = false;       // `--raid-now`: it lands on the next tick, its entrance skipped
    bool castleFree = false;    // `--castle-free`: Blood Castle with no run and every gate open (a test)
    int castleBridge = -1;      // `--castle-bridge S`: the run on, the drawbridge down in S seconds (a test)
    int castle = 0;             // `--castle N`: Blood Castle N's garrison and statue, as the Messenger sets (a test)
    bool peaceful = false;      // `--peaceful`: no nests on the map (Play::Arena::peaceful)
    // The character's file. Empty means the default (game/save.cpp) for a played run and no
    // file at all for a review run (--frames): a scripted fight must not overwrite the
    // player's hero, nor start from wherever he last stood. `--fresh` ignores what is there
    // and starts a new character, whose first save then replaces it.
    std::string savePath;
    bool fresh = false;
    // --lobby: the character screen first (app/modes/lobby_mode.h), and the world after it on
    // the character picked. `--roster DIR` reads and writes the characters there instead of the
    // account's own folder, which is what a review run wants: a scripted create or delete must
    // not touch the player's characters. The rest put the screen in a state for a still:
    // `--lobby-pick N` picks slot N, `--lobby-create K` opens the create window on class K
    // (0 wizard, 1 knight, 2 elf, the window's order), `--lobby-name S` types S into it, and
    // `--lobby-delete` raises the deletion's question over the pick.
    bool lobby = false;
    std::string rosterPath;
    int lobbyPick = -1;
    int lobbyCreate = -1;
    std::string lobbyName;
    bool lobbyDelete = false;
    // A review harness for the handoff, both frame numbers counted across it: `--lobby-enter F`
    // enters the pick on frame F, and `--lobby-back F` has the world go back to the screen on F,
    // as the menu's Switch Character does.
    int lobbyEnter = -1;
    int lobbyBack = -1;
    // And the map change's: `--travel-at F` travels on frame F, once -- on to the next world
    // (app/modes/play_mode.cpp, game/world/maps.h).
    int travelAt = -1;
    // A review harness and not a feature: every N frames it puts the pointer on a pixel from a
    // short fixed list and clicks it, through the same unprojection a hand would. It is how a
    // run with nobody at the mouse can show that a click walks and a click on a monster fights.
    int demoClicks = 0;
    // `--bolt-every N [--bolt-tiles T]`: the bolt bench -- an Energy Ball thrown every N frames
    // from where he stands at a point T tiles east (default 6), drawing only. Tuning, not play.
    // `--arena-left`: the arena's hand attacks with the left button, the weapon alone, rather
    // than with the right button's quick slot.
    bool arenaLeft = false;
    int boltEvery = 0;
    float boltTiles = 6.0f;
    // `--bolt-skill N`: which spell the bench throws, by MU's number -- 17 Energy Ball, 4 Fire Ball.
    int boltSkill = 17;
    // `--point X,Y`: the pointer held on one spot, as fractions of the window, and never
    // clicked -- so a shot can show what hovering there does (the ring, the cursor).
    float pointX = -1.0f, pointY = -1.0f;

    // The monster bench: one figure on the bench ground, by the name index.json gives it,
    // playing one clip. `--clip` is MU's own action number, in the right table of the two --
    // a monster's 4 is its second swing and a player's is "Stop sword".
    // The cooked browser: step through every .mum the cook wrote, with the arrow keys. Not
    // the same thing as --model, which reads one glb: this reads what the game loads.
    bool browse = false;
    // The browser on its stage: a bonfire, a lamp and a wall of the world's own stood round the
    // subject, with the world's lamps built from them. viewer.sh passes it.
    bool stage = false;
    // The studio: the browser in the world the game draws, its subject beside one of the map's
    // own bonfires. With --turns N and --shot E it sweeps: N angles round the subject,
    // one a block of E frames, at noon, then dusk, then night. tools/studio.py passes both.
    bool studio = false;
    int turns = 0;
    // The browser's list and its clip line off the screen, for shots of the subject alone.
    bool list = true;

    // The frame rate in the top right, while a world is played. On for a game and off for
    // every run whose picture or whose number is being read: a studio sheet must not carry a
    // counter baked into the plate, and a measured run must not pay for a draw the budget
    // knows nothing about. So --still, --budget and --stats turn it off by themselves, and
    // --fps or --no-fps on the command line beats all three -- the same shape as --still and
    // --spin, where the script sets a default and the person running it takes it back.
    bool fps = true;
    bool fpsAsked = false;  // --fps or --no-fps was named, so the run's own kind does not decide

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
    // Windows open from the first frame, for a scripted run that has to photograph them:
    // any of "inventory", "character", comma separated.
    std::string windows;
    // Scripted presses on the windows, for a run with nobody at the mouse: FRAME:X:Y, X and Y
    // as fractions of the screen, pressed on FRAME and released on the next -- or
    // FRAME:X:Y:X2:Y2, a drag, pressed at the first point, carried to the second over two
    // frames and released there on the third. A right-click is FRAME:X:Y:R. Repeatable.
    struct UiClick {
        int frame = 0;
        float x = 0.0f, y = 0.0f;
        float x2 = -1.0f, y2 = -1.0f;
        bool right = false;
    };
    std::vector<UiClick> uiClicks;
    // Scripted typing: FRAME:TEXT, the text typed into whatever box is open on FRAME -- digits,
    // or the words enter and escape for those keys. Repeatable.
    std::vector<std::pair<int, std::string>> uiTyped;
    // A scripted pointer parked at X,Y (fractions of the screen) on every frame, pressing
    // nothing: what a review run needs to photograph a tooltip, since a click on an item in
    // the bag picks it up instead of describing it. Negative is "nobody is pointing".
    float hoverX = -1.0f, hoverY = -1.0f;
    // Scripted potion keys: FRAME:KEY, KEY 1 to 4, pressed on FRAME. Repeatable.
    std::vector<std::pair<int, int>> uiKeys;
    // Frame and skill key, 1 to 4 for Q W E R: what --ui-key is for a potion. The cast path's
    // review loop -- a shot of a cooldown running needs the key pressed on a known frame.
    std::vector<std::pair<int, int>> uiSkills;
    // The level-up's flares thrown on the hero on FRAME, for a review run that cannot wait for
    // a real level. Only the drawing: the realm's level is untouched. Repeatable.
    std::vector<int> rises;
    // The same for the orb's aura and its swoosh, and the same warning: only the drawing. The
    // realm learns nothing, so this shows the picture on a character who cannot afford it.
    std::vector<int> learns;
    // And the guard's cage -- the knight's Defense and the wizard's Soul Barrier share it -- on
    // FRAME, for a review run. The same warning: only the drawing, and no boon is raised.
    std::vector<int> guards;
    // Everything sounds and is logged, at no volume: a review run should not play into the room.
    bool mute = false;
    // Things put in the bag at the start: NAME or NAME:COUNT, comma separated.
    std::string give;
    // The bench's, not a rule: on frame `layFrame`, lay NAME,... on the ground beside him as a
    // kill's drop lies -- the fall, the landing sound and the label -- for looking at a drop
    // nothing in Lorencia leaves (the Bless and the Soul). FRAME:LIST.
    int layFrame = -1;
    std::string lay;
    long long zen = 0;     // Zen in hand at the start
    bool loot = false;     // --click-every aims at what lies on the ground before a monster
    bool entrance = false; // --entrance: the fade-up and the character's dissolve in a --frames run
    std::string talk;      // walk to this townsperson (by a piece of his name) at the start
    bool questReady = false;  // --quest-ready: every quest Ready, the hand-in's demo; no save
    // --quests-done 2,6: those quests (sim/quests.cpp's table order) handed in once, so what waits
    // on them is offered -- the Lost Tower's chain after Devin's; no save.
    std::string questsDone;
    bool questDemo = false;  // --quest-demo: Peia gives the two demo quests too (sim::kDemoQuests)
    int walkColumn = -1, walkRow = -1;  // --walk-to: one walk to this tile at the start
    int perch = -1;       // walk to this perch (an index into the tables' perches) and take it
    // And which entry in it: the first whose name holds this, ignoring case. "budge" lands on
    // the Budge Dragon wherever it sits in the list.
    std::string pick;
    // --bearer: a weapon on the Weapons tab is shown in the hands of whoever holds it, rather
    // than alone -- how the grip is judged.
    bool bearer = false;
    // The plus every item on the viewer's subject is shown at, 0 to 15: the refinement shine,
    // judged on the bench (docs/sprints/14-the-shine.md). 0 is the item as it is.
    int plus = 0;
    // The viewer's time of day to open on: noon, dusk or night. sheets/time/<name>.json laid
    // over the lighting sheet; noon is the sheet alone. T walks them with the window open.
    std::string time = "noon";
    std::string figure;
    int clip = -1;
    // Stands the bench's figure as a safe zone does: weapon on the back, unarmed idle. It is
    // how every figure in Lorencia's town square stands, and the only way to judge the slung
    // arrangement without walking the camera into the square.
    bool safe = false;

    // The performance sweep (app/sweep.h, tools/perfsweep.py): the tiles to stand on, as JSON,
    // and where each one's row is written. Both absolute. A played world and --frames are still
    // asked for; the sweep ends the run when its last tile is measured.
    std::string sweepPath;
    std::string sweepOut;

    bool valid = true;
};

// Parses argv. Complains into the log and clears `valid` on anything it does not know,
// rather than carrying on with a switch the caller thinks took effect.
Args parseArgs(int argc, char** argv);

void printUsage();

// saves/ in the client folder (MU2_ROOT_DIR), made on the first ask: the options, the
// characters, the vault and the old hero.json. Nothing the game keeps lives outside it --
// not in ~/Library/Application Support, where it used to. Git ignores it.
std::string userFolder();

// saves/options.txt, beside the characters: what --remember reads and what the game menu
// writes back. `key value` a line; a key it does not know is passed by.
std::string optionsPath();
void saveOptions(const Args& args);

}  // namespace mu::core
