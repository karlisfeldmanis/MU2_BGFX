#include "core/args.h"

#include <cstdlib>
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
        "  --msaa N                  1, 2, 4 or 8 samples (default 4)\n"
        "  --world NAME              raise a world instead of the model bench\n"
        "  --at COLUMN,ROW           which tile the world camera looks at");
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
        } else if (!std::strcmp(s, "--vsync")) {
            a.vsync = true;
        } else if (!std::strcmp(s, "--frames")) {
            if (const char* v = next(s)) a.frames = std::atoi(v);
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
                    a.atColumn = float(std::atof(v));
                    a.atRow = float(std::atof(comma + 1));
                }
            }
        } else if (!std::strcmp(s, "--still")) {
            a.still = true;
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
