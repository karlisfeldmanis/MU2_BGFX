// MU2 on bgfx. Sprint 0: the review loop and the budget gate, around a window that clears
// itself. The frame's six views exist from here on; sprints fill them. See PLAN.md.
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <cstdio>
#include <string>

#include "core/args.h"
#include "core/log.h"
#include "gfx/stats.h"
#include "gfx/views.h"
#include "gfx/window.h"

using namespace mu;

namespace {

// Where a run writes when it is not told. Absolute, from the build.
std::string defaultPath(const char* dir, const char* name) {
    return std::string(dir) + "/" + name;
}

// Lays out the frame's views. Sprint 0 has no targets yet, so every one of them is the
// backbuffer and only the first clears; what this proves is that the view ids, their order
// and their accounts are wired from end to end.
void setupViews(int width, int height) {
    for (uint16_t v = 0; v < gfx::ViewCount; ++v) {
        bgfx::setViewName(v, gfx::viewName(gfx::View(v)));
        bgfx::setViewRect(v, 0, 0, uint16_t(width), uint16_t(height));
        bgfx::setViewClear(v, v == gfx::ViewShadow ? (BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH) : 0,
                           0x14181eff, 1.0f, 0);
    }
}

}  // namespace

int main(int argc, char** argv) {
    core::Args args = core::parseArgs(argc, argv);

    const std::string logPath =
        args.logPath.empty() ? defaultPath(MU2_ROOT_DIR, "mu2.log") : args.logPath;
    core::logOpen(logPath.c_str());
    if (!args.valid) {
        core::logf("nothing run");
        core::logClose();
        return 1;
    }

    const std::string shotDir = args.shotPath.empty() ? defaultPath(MU2_ROOT_DIR, "shots") : args.shotPath;

    gfx::Window window;
    gfx::WindowDesc desc;
    desc.width = args.width;
    desc.height = args.height;
    desc.vsync = args.vsync;
    if (!window.open(desc)) {
        core::logClose();
        return 1;
    }

    gfx::Stats stats;
    stats.begin(args.statsPath, args.budgetOverrides);

    core::logf("running%s%s", args.frames ? " for " : " until the window closes",
               args.frames ? std::to_string(args.frames).c_str() : "");

    int frame = 0;
    int64_t last = bx::getHPCounter();
    const double toMs = 1000.0 / double(bx::getHPFrequency());
    double sinceLine = 0.0;

    while (window.pump() && !window.escapePressed()) {
        setupViews(window.width(), window.height());

        // Every view is submitted, empty or not: a view bgfx sees nothing in is dropped from
        // the frame and its timer reports nothing, and an account with no rows reads as free
        // rather than as unbuilt.
        for (uint16_t v = 0; v < gfx::ViewCount; ++v) bgfx::touch(v);

        const bool lastFrame = args.frames && frame + 1 >= args.frames;
        if (args.shotEvery && (frame % args.shotEvery == 0 || lastFrame)) {
            char path[1024];
            // The name is ours whole: bgfx hands the path to the callback unchanged and
            // appends nothing, and a shot called 00100 with no suffix is a file nothing opens.
            std::snprintf(path, sizeof(path), "%s/%05d.png", shotDir.c_str(), frame);
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path);
        }

        bgfx::frame();

        const int64_t now = bx::getHPCounter();
        const double cpuMs = double(now - last) * toMs;
        last = now;
        stats.sample(cpuMs);

        sinceLine += cpuMs;
        if (sinceLine >= 1000.0) {
            const bgfx::Stats* s = bgfx::getStats();
            core::logf("frame %d: %.1f fps, cpu %.2f ms, gpu %.2f ms, %u draws", frame,
                       1000.0 / cpuMs, cpuMs,
                       double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0 / double(s->gpuTimerFreq),
                       s->numDraw);
            sinceLine = 0.0;
        }

        ++frame;
        if (args.frames && frame >= args.frames) break;
    }

    const bool withinBudget = stats.finish(args.budget);
    window.close();

    const int errors = core::logErrorCount();
    core::logf("%d frames, %d errors, budget %s", frame, errors,
               !args.budget ? "not enforced" : (withinBudget ? "kept" : "overdrawn"));
    core::logClose();

    if (!withinBudget) return 2;
    return errors ? 1 : 0;
}
