// MU2 on bgfx. Sprint 1: the whole six-view frame, with the model bench in front of it.
// See PLAN.md and docs/sprints/.
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <sys/stat.h>

#include <cstdio>
#include <string>
#include <vector>

#include "content/texture.h"
#include "core/args.h"
#include "core/log.h"
#include "game/bench.h"
#include "game/headless.h"
#include "sim/realm.h"
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

    // The sim with no window, and it returns before anything graphical is touched: no GLFW, no
    // device, no textures. That is what makes the headless run a measurement of the tick.
    if (args.headless) {
        const int code = game::runHeadless(args, MU2_ASSET_DIR);
        core::logClose();
        return code;
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
        // No crowd when the realm is going to be raised: the crowd stands monsters where it
        // chooses and the sim stands them where they are, and raising both means loading, posing
        // and then throwing away 45 figures a run.
        if (!world.open(MU2_ASSET_DIR, args.world, textures, args.play ? 0 : args.crowd,
                        args.figuresOn)) {
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
        // And the realm behind it, when there is somebody playing. A world that cannot raise
        // one -- no cooked tables yet -- says so and is still a world to look at, which is
        // what every run before this sprint was.
        if (args.play) {
            world.play(MU2_ASSET_DIR, args.world, args.seed, args.kin, args.level, args.weapon,
                       args.shield);
        }
    }

    game::ModelBench bench;
    if (args.distance > 0.0f) bench.setDistance(args.distance);
    const std::string modelPath =
        (args.model.empty() || args.model[0] == '/')
            ? args.model
            : defaultPath(MU2_ASSET_DIR, args.model.c_str());
    // --figure is the monster bench and --model the model bench: one figure out of the cook
    // with its clips, or one .glb as it sits on disk. Never both, and the figure wins.
    const bool figureBench = !inWorld && !args.figure.empty();
    if (figureBench && !bench.openFigure(MU2_ASSET_DIR, args.world.empty() ? "lorencia"
                                                                          : args.world,
                                         args.figure, args.clip, args.safe, textures)) {
        core::logError("the bench did not open");
        renderer.shutdown();
        textures.shutdown();
        window.close();
        core::logClose();
        return 1;
    }
    if (!inWorld && !figureBench && !bench.open(modelPath, textures)) {
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
    // The previous frame's own length, which is what this frame advances a clip by. The
    // frame's own is not known until it has been drawn, and a clip advanced by a delta
    // measured after the draw is a clip one frame behind what is on screen.
    double deltaSeconds = 0.0;
    std::vector<gfx::Drawable> townDrawables;
    std::vector<gfx::Drawable> townCasters;

    while (window.pump() && !window.escapePressed()) {
        renderer.resize(window.width(), window.height());

        // Four times a second, counted in milliseconds rather than frames: the point is to
        // tune with the window open, and at 470 fps a count of frames was stat'ing the file
        // a hundred times a second.
        if (sinceSheetCheck >= 250.0) {
            sinceSheetCheck = 0.0;
            lighting.reloadIfChanged(sheetPath);
        }

        // The frame's poses start empty: a row is taken by whoever is posed this frame, and
        // a row left over from the last one belongs to nobody.
        renderer.resetPalettes();

        if (inWorld) {
            world.update(elapsed, args.still);
            // The town's drawables are gathered fresh each frame into one vector that keeps
            // its capacity: a frame appends to a flat array, as foundation 7 says, and
            // allocates nothing after the first.
            townDrawables.clear();
            townCasters.clear();
            const std::vector<gfx::Drawable>* casters = nullptr;
            if (world.town().isOpen()) {
                if (args.cullChunks) {
                    float view[16];
                    float proj[16];
                    renderer.cameraMatrices(world.camera(), view, proj);
                    float viewProj[16];
                    bx::mtxMul(viewProj, view, proj);
                    world.town().gatherVisible(viewProj, townDrawables);
                    // The sun gets its own list, and for now it is all of them. A chunk
                    // behind the camera still casts into the frame, so the camera's frustum
                    // is the wrong test for the split -- foundation 7's named bug. Culling
                    // the split against its own box is the next step and it is measured
                    // separately; drawing every caster is the honest baseline to measure it
                    // against.
                    world.town().gatherAll(townCasters, true);
                    casters = &townCasters;
                } else {
                    world.town().gatherAll(townDrawables);
                }
            }
            // The pointer and what it is over, before the sim is stepped: a click is answered
            // on the tick after it is made, which is MU's own latency and not ours to shave.
            if (world.played().isOpen()) {
                float view[16];
                float proj[16];
                renderer.cameraMatrices(world.camera(), view, proj);
                float pointerX = 0.0f, pointerY = 0.0f;
                window.pointer(&pointerX, &pointerY);
                // The scripted pointer, for a run with nobody at the mouse. It goes through
                // the same unprojection, the same tile, the same request: what it skips is the
                // hand and nothing else.
                bool clickNow = false;
                if (args.demoClicks > 0 && frame % args.demoClicks == 0) {
                    static const float kSpots[6][2] = {{0.50f, 0.50f}, {0.62f, 0.38f},
                                                       {0.38f, 0.60f}, {0.70f, 0.55f},
                                                       {0.44f, 0.34f}, {0.56f, 0.66f}};
                    const int spot = (frame / args.demoClicks) % 6;
                    pointerX = kSpots[spot][0] * float(window.width());
                    pointerY = kSpots[spot][1] * float(window.height());
                    clickNow = true;
                }
                world.played().point(world.camera(), view, proj, pointerX, pointerY,
                                     window.width(), window.height());
                if (window.clicked(0) || clickNow) world.played().leftClick();
                if (window.clicked(1)) world.played().rightClick();
                world.played().update(deltaSeconds);
                float viewProj[16];
                bx::mtxMul(viewProj, view, proj);
                world.played().gather(renderer, viewProj, townDrawables,
                                      casters ? &townCasters : nullptr);
            }

            // The crowd goes into the same two lists as the town, and through the same two
            // passes. A figure is not a special case of a drawable: it is a drawable whose
            // mesh carries a skin and whose instance names a palette row.
            world.crowd().update(float(deltaSeconds));
            if (world.crowd().figureCount() > 0) {
                float view[16];
                float proj[16];
                renderer.cameraMatrices(world.camera(), view, proj);
                float viewProj[16];
                bx::mtxMul(viewProj, view, proj);
                world.crowd().gather(renderer, args.cullChunks ? viewProj : nullptr,
                                     townDrawables, casters ? &townCasters : nullptr);
            }
            renderer.draw(world.camera(), lighting, townDrawables, &world.ground(), casters);
        } else {
            bench.update(elapsed, deltaSeconds, !args.still);
            renderer.draw(bench.camera(), lighting, bench.gather(renderer), nullptr);
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
        deltaSeconds = cpuMs / 1000.0;
        // A screenshot stalls its frame to about 250 ms, and that quarter-second used to be
        // handed to the clips: every shot after the first showed a pose a quarter-second
        // ahead of where a shotless run stands, and any transient shorter than the stall --
        // the 0.18 s crossfade first among them -- could not be photographed at all. The
        // statistics already dropped this frame; the animation clock did not. Capped rather
        // than dropped, because a genuinely slow frame should still advance the world.
        constexpr double kLongestStep = 0.05;  // 50 ms, which is one tick of MU's own 20 Hz
        if (shotThisFrame || deltaSeconds > kLongestStep) deltaSeconds = kLongestStep;
        elapsed += deltaSeconds;
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
            // Foundation 7: the drawn and the culled go in the log, for the camera and for
            // the sun separately, or a culling change cannot be seen to have happened. The
            // sun's line says "all" while its casters are not culled at all, which is the
            // honest way to say that half of this is not built yet.
            if (inWorld && world.town().isOpen()) {
                const game::TownCounts& counts = world.town().counts();
                const game::TownCounts& sun = world.town().casterCounts();
                core::logf("  town: camera %u of %u chunks and %u of %zu placements; "
                           "sun %u chunks and %u placements, unculled",
                           counts.chunksDrawn, counts.chunksDrawn + counts.chunksCulled,
                           counts.instancesDrawn, world.town().instanceCount(),
                           sun.chunksDrawn, sun.instancesDrawn);
            }
            if (inWorld && world.played().isOpen()) {
                const game::Play& play = world.played();
                const sim::Body& hero = play.realm().hero();
                const sim::RealmCounts counts = play.realm().counts();
                core::logf("  play: tick %lld, %.3f ms a tick, hero level %d at %.1f,%.1f with "
                           "%d of %d health; %u monsters, %u alive, %u awake",
                           (long long)play.ticks(), play.tickMs(), hero.level, hero.x, hero.y,
                           hero.health, hero.maxHealth, counts.monsters, counts.alive,
                           counts.roused);
                core::logf("  pointer: tile %d,%d%s | %s", play.pointedColumn(),
                           play.pointedRow(),
                           play.pointedAt() ? " (on a monster)" : "",
                           play.lastLine().c_str());
                if (play.findings().total() > 0) {
                    core::logError("  play: %llu invariants broken",
                                   (unsigned long long)play.findings().total());
                }
            }
            if (inWorld && world.crowd().figureCount() > 0) {
                const game::Crowd& crowd = world.crowd();
                core::logf("  crowd: %u of %zu figures drawn, %u culled, %zu bones, "
                           "pose %.3f ms, %d palette rows of %d%s",
                           crowd.drawn(), crowd.figureCount(), crowd.culled(),
                           crowd.boneCount(), crowd.poseMs(), renderer.paletteRowsUsed(),
                           gfx::Renderer::kMaxPaletteRows,
                           renderer.paletteRowsRefused()
                               ? " -- FULL, the rest stand in bind pose" : "");
                if (renderer.paletteRowsRefused()) {
                    core::logError("%d figures found no palette row this frame and stood in "
                                   "bind pose; the palette holds %d",
                                   renderer.paletteRowsRefused(),
                                   gfx::Renderer::kMaxPaletteRows);
                }
            }
            // The bench says where the clock is, not only which clip: position AND length.
            if (bench.hasFigure()) core::logf("  %s", bench.clipLine().c_str());
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
