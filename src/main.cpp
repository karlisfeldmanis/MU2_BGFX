// MU2 on bgfx. Sprint 1: the whole six-view frame, with the model bench in front of it.
// See PLAN.md and docs/sprints/.
#include <bgfx/bgfx.h>
#include <bx/timer.h>

#include <sys/stat.h>

#include <cstdio>
#include <string>

#include "content/texture.h"
#include "core/args.h"
#include "core/log.h"
#include "game/bench.h"
#include "game/world.h"
#include "gfx/lighting.h"
#include "gfx/renderer.h"
#include "gfx/stats.h"
#include "gfx/views.h"
#include "gfx/window.h"

using namespace mu;

namespace {

std::string defaultPath(const char* dir, const char* name) {
    return std::string(dir) + "/" + name;
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

    const std::string shotDir =
        args.shotPath.empty() ? defaultPath(MU2_ROOT_DIR, "shots") : args.shotPath;
    // Made rather than assumed: a --shot-path that does not exist turned every shot into a
    // logged failure and the run's exit code into 1, long after the run was worth repeating.
    if (args.shotEvery) ::mkdir(shotDir.c_str(), 0755);
    const std::string sheetPath = args.sheet.empty()
                                      ? defaultPath(MU2_SHEET_DIR, "lighting.json")
                                      : args.sheet;

    gfx::Window window;
    gfx::WindowDesc desc;
    desc.width = args.width;
    desc.height = args.height;
    desc.vsync = args.vsync;
    if (!window.open(desc)) {
        core::logClose();
        return 1;
    }

    content::Textures textures;
    textures.createDefaults();

    gfx::Renderer renderer;
    if (!renderer.init(window.width(), window.height(), MU2_SHADER_DIR, args.msaa)) {
        core::logError("the renderer did not start");
        textures.shutdown();
        window.close();
        core::logClose();
        return 1;
    }

    gfx::Lighting lighting;
    lighting.reloadIfChanged(sheetPath);

    // Either a world or the model bench, never both: they are two different things to look
    // at and the camera belongs to whichever it is.
    game::World world;
    const bool inWorld = !args.world.empty();
    if (inWorld) {
        if (args.atSet) world.setFocusTile(args.atColumn, args.atRow);
        if (!world.open(MU2_ASSET_DIR, args.world, textures)) {
            core::logError("the world did not open");
            // The world may have failed half-open -- `--at` off the map is refused after the
            // ground's buffers are already made -- and a failure path that skips the world's
            // own shutdown leaks a vertex and an index buffer past bgfx's own shutdown.
            world.shutdown();
            renderer.shutdown();
            textures.shutdown();
            window.close();
            core::logClose();
            return 1;
        }
    }

    game::ModelBench bench;
    if (args.distance > 0.0f) bench.setDistance(args.distance);
    const std::string modelPath =
        (args.model.empty() || args.model[0] == '/')
            ? args.model
            : defaultPath(MU2_ASSET_DIR, args.model.c_str());
    if (!inWorld && !bench.open(modelPath, textures)) {
        core::logError("the bench did not open");
        renderer.shutdown();
        textures.shutdown();
        window.close();
        core::logClose();
        return 1;
    }

    // A shot and a measurement do not belong in the same run, and saying so is cheaper than
    // discovering it twice. The stalled frame itself is kept out of the statistics, but the
    // readback's cost does not land wholly inside that one frame: measured over six
    // alternating pairs, a run with --shot still comes out about 0.15 ms of mean dearer, and
    // it was dearer in six pairs out of six. A single pair is not enough to see it -- the
    // spread between runs is larger than the effect -- which is exactly how this file came to
    // publish the difference with the sign reversed.
    if (args.shotEvery && (args.budget || !args.statsPath.empty())) {
        core::logf("NOTE: --shot is on, so these timings are about 0.15 ms a frame dearer "
                   "than the same run without it. Take numbers from a run with no shots.");
    }

    gfx::Stats stats;
    stats.begin(args.statsPath, args.budgetOverrides);

    core::logf("running%s%s", args.frames ? " for " : " until the window closes",
               args.frames ? std::to_string(args.frames).c_str() : "");

    int frame = 0;
    int segment = 0;
    int64_t last = bx::getHPCounter();
    const double toMs = 1000.0 / double(bx::getHPFrequency());
    double elapsed = 0.0;
    double sinceLine = 0.0;
    double sinceSheetCheck = 0.0;

    while (window.pump() && !window.escapePressed()) {
        renderer.resize(window.width(), window.height());

        // Four times a second, counted in milliseconds rather than frames: the point is to
        // tune with the window open, and at 470 fps a count of frames was stat'ing the file
        // a hundred times a second.
        if (sinceSheetCheck >= 250.0) {
            sinceSheetCheck = 0.0;
            lighting.reloadIfChanged(sheetPath);
        }

        if (inWorld) {
            world.update(elapsed, args.still);
            renderer.draw(world.camera(), lighting, {}, &world.ground());
        } else {
            bench.update(elapsed, !args.still);
            renderer.draw(bench.camera(), lighting, bench.drawables(), nullptr);
        }

        const bool lastFrame = args.frames && frame + 1 >= args.frames;
        const bool shotThisFrame = args.shotEvery && (frame % args.shotEvery == 0 || lastFrame);
        if (shotThisFrame) {
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
        elapsed += cpuMs / 1000.0;
        // A frame that writes a screenshot is not a frame of the game, and it does not go in
        // the statistics. The readback stalls this one frame to about 253 ms, and a mean over
        // 570 frames carries that as about 1.6 ms of pure measurement apparatus -- which is
        // how sprint 2 published 3.075 ms still and 4.085 moving under a command line that
        // said `--frames 600 --world lorencia` and had actually been run with `--shot 200`.
        // Six runs of the command as written measured 2.16 ms for both. The cost of taking a
        // picture belongs to the reviewer, not to the frame being reviewed.
        if (!shotThisFrame) stats.sample(cpuMs);

        sinceLine += cpuMs;
        sinceSheetCheck += cpuMs;
        if (sinceLine >= 1000.0) {
            const bgfx::Stats* s = bgfx::getStats();
            core::logf("frame %d: %.1f fps, cpu %.2f ms, gpu %.2f ms, %u draws", frame,
                       1000.0 / cpuMs, cpuMs,
                       double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0 / double(s->gpuTimerFreq),
                       s->numDraw);
            sinceLine = 0.0;
        }

        ++frame;
        if (args.frames && frame >= args.frames) {
            // One segment done. With --repeat the world stays loaded and the next segment
            // starts from a fresh warmup: what separates them is then the machine's own
            // drift, which is the thing worth measuring, rather than the twenty seconds of
            // texture decoding a second launch would spend first.
            if (segment + 1 < args.repeat) {
                stats.endSegment(segment, args.repeat);
                ++segment;
                frame = 0;
                last = bx::getHPCounter();
                continue;
            }
            // The last segment is NOT ended here: its frames stay in hand so finish() can
            // still print the per-account table and the two humps. finish() counts it as a
            // segment itself.
            break;
        }
    }

    const bool withinBudget = stats.finish(args.budget);

    bench.shutdown();
    world.shutdown();
    renderer.shutdown();
    textures.shutdown();
    window.close();

    const int errors = core::logErrorCount();
    core::logf("%d frames in %d segment(s), %d errors, budget %s", frame, args.repeat, errors,
               !args.budget ? "not enforced" : (withinBudget ? "kept" : "overdrawn"));
    core::logClose();

    if (!withinBudget) return 2;
    return errors ? 1 : 0;
}
