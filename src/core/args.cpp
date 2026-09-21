#include "core/args.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace mu::core {
namespace {

// A path on the command line is absolute or it is nothing: a shot written relative to a
// working directory nobody set is a shot nobody finds. MU2's bench lost pictures this way.
bool wantsAbsolute(const char* what, const std::string& path, bool* valid) {
    if (!path.empty() && path[0] == '/') return true;
    logError("%s must be an absolute path, got '%s'", what, path.c_str());
    *valid = false;
    return false;
}

}  // namespace

void printUsage() {
    logf(
        "mu2 [options]\n"
        "  --width N --height N      backbuffer size (default 1920x1080)\n"
        "  --vsync                   cap to the display; off by default so a number is a number\n"
        "  --frames N                quit after N frames\n"
        "  --repeat N                measure N segments of --frames, loading the world once\n"
        "  --shot N                  write a PNG every N frames, and on the last\n"
        "  --shot-path DIR           absolute directory for the PNGs\n"
        "  --log PATH                absolute path for the log\n"
        "  --stats PATH              absolute path for a csv, a row a frame\n"
        "  --budget                  fail the run when an account is overdrawn\n"
        "  --budget NAME=MS          and replace one account's allowance\n"
        "  --model PATH              a .glb under assets/, or an absolute path\n"
        "  --sheet PATH              the lighting sheet (default sheets/lighting.json)\n"
        "  --dist N                  camera distance in world units\n"
        "  --still                   hold the camera instead of turning it\n"
        "  --spin                    turn it after all, undoing an earlier --still\n"
        "  --shadow-log PATH         a csv row a frame: the sun's split against a world grid\n"
        "  --shadow-slide MM         move the split alone MM millimetres a frame\n"
        "  --shadow-points PATH      a csv row a frame: a ground grid's pixels, for tools/pan.py\n"
        "  --shadow-view             draw the sun's visibility alone, as grey\n"
        "  --shadow-noise WHERE      the penumbra's turn: screen, world or none\n"
        "  --shadow-size N           the sun's map, N texels square (default 4096)\n"
        "  --fixed-dt MS             advance every frame by MS, so two runs draw the same frames\n"
        "  --no-cull                 submit every placement, not only the visible chunks\n"
        "  --msaa N                  1, 2, 4 or 8 samples (default 4)\n"
        "  --world NAME              raise a world instead of the model bench\n"
        "  --at COLUMN,ROW           which tile the world camera looks at; on the map or the "
        "run fails\n"
        "  --crowd N                 monsters standing in the town (default 30, -1 for all "
        "that spawn)\n"
        "  --no-lamps                no point lights and no flames, to price the lamps\n"
        "  --no-figures              no figures at all, which is what the crowd is priced "
        "against\n"
        "  --browse                  step through every cooked .mum; arrows walk the list\n"
        "  --studio                  the browser in the real world, beside one of its bonfires\n"
        "  --turns N                 with --studio and --shot E: N angles a time of day\n"
        "  --no-list                 the browser's list off the screen, for clean shots\n"
        "  --category WORD           world|monsters|people|armour|weapons|parts\n"
        "  --windows LIST            open these from the first frame: inventory,character; off: no HUD\n"
        "  --ui-click F:X:Y[:X2:Y2]  press the windows at screen fraction X,Y on frame F\n"
        "  --give LIST               put NAME[:COUNT],... in the bag at the start\n"
        "  --ui-key F:K              press potion key K (1-4) on frame F\n"
        "  --zen N                   start with N Zen\n"
        "  --loot                    --click-every picks up drops before it fights\n"
        "  --talk NAME               walk to the townsperson whose name holds NAME\n"
        "  --pick NAME               and on the first entry whose name holds NAME\n"
        "  --effects N               N sprites through the transparent pass, to price it\n"
        "  --effect-size M           each sprite's half-extent in metres (default 0.5); large "
        "is the fill-rate worst case\n"
        "  --effect-sheet NAME       which cooked effect sheet they wear\n"
        "  --figure NAME             the monster bench: one figure, by index.json's name\n"
        "  --clip N                  which clip it plays, as MU's own action number\n"
        "  --play                    raise the realm behind the window: click to walk, click "
        "to fight\n"
        "  --click-every N           a scripted click every N frames, through the real pick\n"
        "  --headless                run the sim with no window at all\n"
        "  --seed N                  the sim's seed; the same seed is the same run\n"
        "  --ticks N                 how many 20 Hz ticks to run (default 10000)\n"
        "  --sim-log PATH            absolute path for the event log\n"
        "  --sim-steps               log every tile crossing too\n"
        "  --no-hand                 no scripted player: the nests on their own\n"
        "  --class N                 0 Dark Wizard, 1 Fairy Elf, 2 Dark Knight\n"
        "  --weapon NAME             what he holds, by index.json's name (Sword01, Axe03...)\n"
        "  --shield NAME             and in his other hand (Shield10...)\n"
        "  --level N                 the character's level (default 1)\n"
        "  --spend STAT              where a levelled character's points go "
        "(strength by default)");
}

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const char* s = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                logError("%s wants a value", what);
                a.valid = false;
                return nullptr;
            }
            return argv[++i];
        };

        if (!std::strcmp(s, "--width")) {
            if (const char* v = next(s)) a.width = std::atoi(v);
        } else if (!std::strcmp(s, "--height")) {
            if (const char* v = next(s)) a.height = std::atoi(v);
        } else if (!std::strcmp(s, "--no-figures")) {
            a.figuresOn = false;
        } else if (!std::strcmp(s, "--no-lamps")) {
            a.lampsOn = false;
        } else if (!std::strcmp(s, "--safe")) {
            a.safe = true;
        } else if (!std::strcmp(s, "--crowd")) {
            if (const char* v = next(s)) a.crowd = std::atoi(v);
        } else if (!std::strcmp(s, "--figure")) {
            if (const char* v = next(s)) a.figure = v;
        } else if (!std::strcmp(s, "--clip")) {
            if (const char* v = next(s)) a.clip = std::atoi(v);
        } else if (!std::strcmp(s, "--click-every")) {
            if (const char* v = next(s)) a.demoClicks = std::atoi(v);
        } else if (!std::strcmp(s, "--weapon")) {
            if (const char* v = next(s)) a.weapon = v;
        } else if (!std::strcmp(s, "--shield")) {
            if (const char* v = next(s)) a.shield = v;
        } else if (!std::strcmp(s, "--play")) {
            a.play = true;
        } else if (!std::strcmp(s, "--headless")) {
            a.headless = true;
        } else if (!std::strcmp(s, "--seed")) {
            if (const char* v = next(s)) a.seed = std::strtoull(v, nullptr, 10);
        } else if (!std::strcmp(s, "--ticks")) {
            if (const char* v = next(s)) a.ticks = std::atoi(v);
        } else if (!std::strcmp(s, "--sim-log")) {
            if (const char* v = next(s)) {
                a.simLog = v;
                wantsAbsolute("--sim-log", a.simLog, &a.valid);
            }
        } else if (!std::strcmp(s, "--sim-steps")) {
            a.simSteps = true;
        } else if (!std::strcmp(s, "--no-hand")) {
            a.noHand = true;
        } else if (!std::strcmp(s, "--class")) {
            if (const char* v = next(s)) {
                a.kin = std::atoi(v);
                if (a.kin < 0 || a.kin > 2) {
                    logError("--class is 0 Dark Wizard, 1 Fairy Elf or 2 Dark Knight, got %d",
                             a.kin);
                    a.valid = false;
                }
            }
        } else if (!std::strcmp(s, "--level")) {
            if (const char* v = next(s)) a.level = std::atoi(v);
        } else if (!std::strcmp(s, "--spend")) {
            if (const char* v = next(s)) {
                a.spend = v;
                if (a.spend != "strength" && a.spend != "agility" && a.spend != "vitality" &&
                    a.spend != "energy" && a.spend != "none") {
                    logError("--spend is strength, agility, vitality, energy or none, got '%s'",
                             v);
                    a.valid = false;
                }
            }
        } else if (!std::strcmp(s, "--vsync")) {
            a.vsync = true;
        } else if (!std::strcmp(s, "--frames")) {
            if (const char* v = next(s)) a.frames = std::atoi(v);
        } else if (!std::strcmp(s, "--repeat")) {
            if (const char* v = next(s)) a.repeat = std::atoi(v);
            if (a.repeat < 1) {
                logError("--repeat wants at least 1, got %d", a.repeat);
                a.valid = false;
            }
        } else if (!std::strcmp(s, "--shot")) {
            if (const char* v = next(s)) a.shotEvery = std::atoi(v);
        } else if (!std::strcmp(s, "--shot-path")) {
            if (const char* v = next(s)) {
                a.shotPath = v;
                wantsAbsolute("--shot-path", a.shotPath, &a.valid);
            }
        } else if (!std::strcmp(s, "--log")) {
            if (const char* v = next(s)) {
                a.logPath = v;
                wantsAbsolute("--log", a.logPath, &a.valid);
            }
        } else if (!std::strcmp(s, "--stats")) {
            if (const char* v = next(s)) {
                a.statsPath = v;
                wantsAbsolute("--stats", a.statsPath, &a.valid);
            }
        } else if (!std::strcmp(s, "--budget")) {
            a.budget = true;
            // An optional NAME=MS follows. A bare --budget is the documented allowances.
            if (i + 1 < argc && std::strchr(argv[i + 1], '=') && argv[i + 1][0] != '-') {
                const char* v = argv[++i];
                const char* eq = std::strchr(v, '=');
                BudgetOverride o;
                o.account.assign(v, size_t(eq - v));
                o.ms = std::atof(eq + 1);
                a.budgetOverrides.push_back(o);
            }
        } else if (!std::strcmp(s, "--model")) {
            // Not required to be absolute: a bare path is under assets/, which is where
            // tools/sync.sh puts MU2's content. Nothing is read from MU2 at run time.
            if (const char* v = next(s)) a.model = v;
        } else if (!std::strcmp(s, "--sheet")) {
            if (const char* v = next(s)) {
                a.sheet = v;
                wantsAbsolute("--sheet", a.sheet, &a.valid);
            }
        } else if (!std::strcmp(s, "--dist")) {
            if (const char* v = next(s)) a.distance = float(std::atof(v));
        } else if (!std::strcmp(s, "--msaa")) {
            if (const char* v = next(s)) {
                a.msaa = std::atoi(v);
                if (a.msaa != 1 && a.msaa != 2 && a.msaa != 4 && a.msaa != 8) {
                    logError("--msaa takes 1, 2, 4 or 8, not %d", a.msaa);
                    a.valid = false;
                }
            }
        } else if (!std::strcmp(s, "--world")) {
            if (const char* v = next(s)) a.world = v;
        } else if (!std::strcmp(s, "--at")) {
            if (const char* v = next(s)) {
                const char* comma = std::strchr(v, ',');
                if (!comma) {
                    logError("--at wants column,row, got '%s'", v);
                    a.valid = false;
                } else {
                    // strtod, not atof: atof turns "abc" into 0 and reports nothing, so
                    // `--at abc,def` ran a full 600 frames looking at tile 0,0 and answered
                    // a question nobody asked.
                    char* afterColumn = nullptr;
                    char* afterRow = nullptr;
                    const double column = std::strtod(v, &afterColumn);
                    const double row = std::strtod(comma + 1, &afterRow);
                    // Four ways this can be junk, not two. An empty column leaves
                    // afterColumn AT the comma, which the first test alone reads as success
                    // ("--at ,5" ran at tile 0,5); and trailing junk on the row was ignored
                    // because only its start was checked ("--at 12,5x" ran at 12,5).
                    const bool columnEmpty = afterColumn == v;
                    const bool rowEmpty = afterRow == comma + 1;
                    const bool rowTrailing = afterRow == nullptr || *afterRow != '\0';
                    if (columnEmpty || rowEmpty || rowTrailing || afterColumn != comma) {
                        logError("--at wants two numbers as column,row, got '%s'", v);
                        a.valid = false;
                    } else {
                        a.atColumn = float(column);
                        a.atRow = float(row);
                    }
                    // A tile off the map is refused rather than framed. There is no tile at
                    // a negative column, and a camera pointed at one used to fall back to the
                    // town without a word -- so the run answered a question nobody asked. The
                    // other end of the map is checked once its size is known, in World::open.
                    if (a.atColumn < 0.0f || a.atRow < 0.0f) {
                        logError("--at takes tiles, and there is no tile at %s: both "
                                 "column and row must be zero or more",
                                 v);
                        a.valid = false;
                    }
                    a.atSet = true;
                }
            }
        } else if (!std::strcmp(s, "--spin")) {
            // The other way round from --still, so a script can pass --still by default and
            // the person running it can take it back on the command line. The viewer does
            // exactly that: a turntable is motion you did not ask for when you are trying to
            // look at one face of a thing.
            a.still = false;
        } else if (!std::strcmp(s, "--browse")) {
            a.browse = true;
        } else if (!std::strcmp(s, "--stage")) {
            a.browse = true;
            a.stage = true;
        } else if (!std::strcmp(s, "--studio")) {
            a.browse = true;
            a.studio = true;
        } else if (!std::strcmp(s, "--no-list")) {
            a.list = false;
        } else if (!std::strcmp(s, "--turns")) {
            if (const char* v = next(s)) a.turns = std::atoi(v);
        } else if (!std::strcmp(s, "--effects")) {
            if (const char* v = next(s)) a.effects = std::atoi(v);
        } else if (!std::strcmp(s, "--effect-size")) {
            if (const char* v = next(s)) a.effectSize = float(std::atof(v));
        } else if (!std::strcmp(s, "--effect-sheet")) {
            if (const char* v = next(s)) a.effectSheet = v;
        } else if (!std::strcmp(s, "--ui-click")) {
            if (const char* v = next(s)) {
                Args::UiClick c;
                const int got = std::sscanf(v, "%d:%f:%f:%f:%f", &c.frame, &c.x, &c.y, &c.x2, &c.y2);
                if (got == 3 && std::strchr(v, 'R')) c.right = true;
                if (got == 3 || got == 5) {
                    if (got == 3) {
                        c.x2 = c.x;
                        c.y2 = c.y;
                    }
                    a.uiClicks.push_back(c);
                } else {
                    logError("--ui-click is FRAME:X:Y, got '%s'", v);
                    a.valid = false;
                }
            }
        } else if (!std::strcmp(s, "--loot")) {
            a.loot = true;
        } else if (!std::strcmp(s, "--zen")) {
            if (const char* v = next(s)) a.zen = std::atoll(v);
        } else if (!std::strcmp(s, "--talk")) {
            if (const char* v = next(s)) a.talk = v;
        } else if (!std::strcmp(s, "--ui-key")) {
            int f = 0, k = 0;
            const char* v = next(s);
            if (v && std::sscanf(v, "%d:%d", &f, &k) == 2 && k >= 1 && k <= 4) {
                a.uiKeys.push_back({f, k});
            } else {
                logError("--ui-key is FRAME:KEY with KEY 1 to 4");
                a.valid = false;
            }
        } else if (!std::strcmp(s, "--give")) {
            if (const char* v = next(s)) a.give = v;
        } else if (!std::strcmp(s, "--windows")) {
            if (const char* v = next(s)) a.windows = v;
        } else if (!std::strcmp(s, "--category")) {
            if (const char* v = next(s)) a.category = v;
        } else if (!std::strcmp(s, "--pick")) {
            if (const char* v = next(s)) a.pick = v;
        } else if (!std::strcmp(s, "--time")) {
            if (const char* v = next(s)) {
                a.time = v;
                if (a.time != "noon" && a.time != "dusk" && a.time != "night") {
                    logError("--time takes noon, dusk or night, not '%s'", v);
                    a.valid = false;
                }
            }
        } else if (!std::strcmp(s, "--shadow-log")) {
            if (const char* v = next(s)) {
                a.shadowLog = v;
                wantsAbsolute("--shadow-log", a.shadowLog, &a.valid);
            }
        } else if (!std::strcmp(s, "--shadow-points")) {
            if (const char* v = next(s)) {
                a.shadowPoints = v;
                wantsAbsolute("--shadow-points", a.shadowPoints, &a.valid);
            }
        } else if (!std::strcmp(s, "--shadow-view")) {
            a.shadowView = true;
        } else if (!std::strcmp(s, "--shadow-noise")) {
            if (const char* v = next(s)) {
                if (!std::strcmp(v, "screen")) a.shadowNoise = 0;
                else if (!std::strcmp(v, "world")) a.shadowNoise = 1;
                else if (!std::strcmp(v, "none")) a.shadowNoise = 2;
                else {
                    logError("--shadow-noise is screen, world or none, got '%s'", v);
                    a.valid = false;
                }
            }
        } else if (!std::strcmp(s, "--shadow-size")) {
            if (const char* v = next(s)) {
                a.shadowSize = std::atoi(v);
                if (a.shadowSize < 256 || a.shadowSize > 8192 ||
                    (a.shadowSize & (a.shadowSize - 1))) {
                    logError("--shadow-size is a power of two from 256 to 8192, got %d",
                             a.shadowSize);
                    a.valid = false;
                }
            }
        } else if (!std::strcmp(s, "--fixed-dt")) {
            if (const char* v = next(s)) a.fixedDtMs = float(std::atof(v));
        } else if (!std::strcmp(s, "--shadow-slide")) {
            if (const char* v = next(s)) a.shadowSlideMm = float(std::atof(v));
        } else if (!std::strcmp(s, "--still")) {
            a.still = true;
        } else if (!std::strcmp(s, "--no-cull")) {
            a.cullChunks = false;
        } else if (!std::strcmp(s, "--help") || !std::strcmp(s, "-h")) {
            printUsage();
            a.valid = false;
        } else {
            logError("unknown option '%s'", s);
            a.valid = false;
        }
    }
    if (a.width <= 0 || a.height <= 0) {
        logError("a backbuffer of %dx%d is not a backbuffer", a.width, a.height);
        a.valid = false;
    }
    return a;
}

}  // namespace mu::core
