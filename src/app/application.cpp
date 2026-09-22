#include "app/application.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <sys/stat.h>

#include <cstdio>
#include <memory>
#include <string>

#include "app/modes/bench_mode.h"
#include "app/modes/play_mode.h"
#include "content/showing.h"
#include "core/log.h"
#include "game/headless.h"
#include "gfx/stats.h"
#include "gfx/views.h"

namespace mu::app {

bool Application::boot() {
    gfx::WindowDesc desc;
    desc.width = args_.width;
    desc.height = args_.height;
    desc.vsync = args_.vsync;
    if (!window_.open(desc)) return false;

    textures_.createDefaults();

    if (!renderer_.init(window_.width(), window_.height(), paths_.shaders.c_str(), args_.msaa,
                        uint16_t(args_.shadowSize))) {
        core::logError("the renderer did not start");
        textures_.shutdown();
        window_.close();
        return false;
    }
    // The viewer announces its time of day on every change; a play run does not.
    const int daytime = args_.time == "dusk" ? 1 : (args_.time == "night" ? 2 : 0);
    time_.open(&paths_, &lighting_, daytime, args_.browse);
    return true;
}

void Application::openProbe() {
    // The transparent pass's probe. The sheet comes out of the cooked showing table rather
    // than off a path, so what is measured is exactly what the game will draw: a BC7 .ktx
    // with its mip chain, not a PNG decoded at load.
    //
    // A probe that cannot find its sheet draws nothing and says so once. It is never fatal:
    // this is a measurement aid, and a run that refuses to start because an effect is
    // missing would be a worse tool than one that says the effects are missing.
    probeSprites_ = args_.effects;
    if (probeSprites_ <= 0) return;
    content::Showing showing;
    std::string error;
    const std::string showingPath = paths_.assets + "/cooked/showing/showing.mus";
    if (!content::loadShowing(showingPath, showing, error)) {
        core::logError("the showing table did not open: %s (tools/cook.py --only showing)",
                       error.c_str());
        probeSprites_ = 0;
        return;
    }
    const content::EffectSheet* chosen =
        args_.effectSheet.empty()
            ? (showing.effects.empty() ? nullptr : &showing.effects.front())
            : showing.effect(args_.effectSheet);
    if (chosen == nullptr) {
        core::logError("no cooked effect named '%s'; %zu are in the table",
                       args_.effectSheet.c_str(), showing.effects.size());
        probeSprites_ = 0;
        return;
    }
    probeSheet_ = textures_.load(paths_.assets + "/" + chosen->path, content::TextureRole::Albedo);
    core::logf("effects probe: %d sprites of '%s' at %.2f m, %zu sheets and "
               "%zu sound events cooked",
               probeSprites_, chosen->name.c_str(), args_.effectSize, showing.effects.size(),
               showing.events.size());
}

void Application::submitProbe(Mode& mode) {
    if (probeSprites_ <= 0 || !bgfx::isValid(probeSheet_)) return;
    const gfx::Camera& shot = mode.camera();
    // Strung along the line from the camera to what it looks at, so that the sprites
    // are in front of the subject rather than inside it, and so that the sort has
    // something to sort: the near one must come out over the far one.
    for (int i = 0; i < probeSprites_; ++i) {
        const float along = 0.35f + 0.5f * float(i) / float(probeSprites_);
        gfx::Sprite sprite;
        for (int c = 0; c < 3; ++c) {
            sprite.position[c] = shot.position[c] + (shot.target[c] - shot.position[c]) * along;
        }
        sprite.halfWidth = sprite.halfHeight = args_.effectSize;
        // A turn per sprite, which is what MU does so that four blows on one spider
        // do not stamp the same picture four times.
        sprite.spin = float(i) * 0.7f;
        sprite.colour[3] = 0.6f;
        sprite.sheet = probeSheet_;
        // The far half alpha and the near half additive, rather than alternating
        // every sprite. Alternating is the batcher's worst case -- the sort puts the
        // two modes in each other's way and every sprite becomes its own draw -- and
        // it is not what a fight looks like: a burst of particles shares one sheet
        // and one mode. Two blocks exercise both modes AND show the runs batching,
        // which is what the draw count in the log is there to report.
        sprite.blend = (i * 2 >= probeSprites_) ? gfx::Blend::Additive : gfx::Blend::Alpha;
        renderer_.effects().add(sprite);
    }
}

void Application::teardown() {
    overlay_.shutdown();
    curtain_.shutdown();
    renderer_.shutdown();
    textures_.shutdown();
    window_.close();
}

int Application::run(int argc, char** argv) {
    args_ = core::parseArgs(argc, argv);

    paths_.assets = MU2_ASSET_DIR;
    paths_.shaders = MU2_SHADER_DIR;
    paths_.sheets = MU2_SHEET_DIR;
    paths_.root = MU2_ROOT_DIR;
    paths_.log = args_.logPath.empty() ? paths_.under(paths_.root, "mu2.log") : args_.logPath;
    paths_.shots = args_.shotPath.empty() ? paths_.under(paths_.root, "shots") : args_.shotPath;
    paths_.sheet =
        args_.sheet.empty() ? paths_.under(paths_.sheets, "lighting.json") : args_.sheet;

    core::logOpen(paths_.log.c_str());
    if (!args_.valid) {
        core::logf("nothing run");
        core::logClose();
        return 1;
    }

    // The sim with no window, and it returns before anything graphical is touched: no GLFW, no
    // device, no textures. That is what makes the headless run a measurement of the tick.
    if (args_.headless) {
        const int code = game::runHeadless(args_, paths_.assets.c_str());
        core::logClose();
        return code;
    }

    // Made rather than assumed: a --shot-path that does not exist turned every shot into a
    // logged failure and the run's exit code into 1, long after the run was worth repeating.
    if (args_.shotEvery) ::mkdir(paths_.shots.c_str(), 0755);

    if (!boot()) {
        core::logClose();
        return 1;
    }

    Context ctx{args_,     paths_,    window_,   renderer_, textures_,
                lighting_, time_,     overlay_,  readout_,  curtain_};

    // Either a world or the bench, never both: they are two different things to look at and
    // the camera belongs to whichever it is.
    std::unique_ptr<Mode> mode;
    if (!args_.world.empty()) mode = std::make_unique<PlayMode>();
    else mode = std::make_unique<BenchMode>();

    if (!mode->open(ctx)) {
        teardown();
        core::logClose();
        return 1;
    }

    openProbe();

    // A shot and a measurement do not belong in the same run, and saying so is cheaper than
    // discovering it twice. The stalled frame itself is kept out of the statistics, but the
    // readback's cost does not land wholly inside that one frame: measured over six
    // alternating pairs, a run with --shot still comes out about 0.15 ms of mean dearer, and
    // it was dearer in six pairs out of six. A single pair is not enough to see it -- the
    // spread between runs is larger than the effect -- which is exactly how this file came to
    // publish the difference with the sign reversed.
    if (args_.shotEvery && (args_.budget || !args_.statsPath.empty())) {
        core::logf("NOTE: --shot is on, so these timings are about 0.15 ms a frame dearer "
                   "than the same run without it. Take numbers from a run with no shots.");
    }

    gfx::Stats stats;
    stats.begin(args_.statsPath, args_.budgetOverrides);

    core::logf("running%s%s", args_.frames ? " for " : " until the window closes",
               args_.frames ? std::to_string(args_.frames).c_str() : "");

    Frame at;
    int segment = 0;
    int64_t last = bx::getHPCounter();
    const double toMs = 1000.0 / double(bx::getHPFrequency());
    double sinceLine = 0.0;
    double sinceSheetCheck = 0.0;

    const bool quitEarly = mode->quitEarly();
    while (!quitEarly && window_.pump() && !window_.escapePressed()) {
        renderer_.resize(window_.width(), window_.height());

        // Four times a second, counted in milliseconds rather than frames: the point is to
        // tune with the window open, and at 470 fps a count of frames was stat'ing the file
        // a hundred times a second.
        if (sinceSheetCheck >= 250.0) {
            sinceSheetCheck = 0.0;
            time_.reloadIfChanged();
        }

        // The clock the scrolling materials read. Seconds of play, not of process: see
        // Renderer::setClock for what reading the wall clock there cost.
        renderer_.setClock(float(at.elapsed));

        // The frame's poses start empty: a row is taken by whoever is posed this frame, and
        // a row left over from the last one belongs to nobody.
        renderer_.resetPalettes();

        // The transparent pass's list starts empty for the same reason, and is filled below
        // from whatever the probe asks for and then by the mode's own effects.
        renderer_.effects().begin();
        submitProbe(*mode);

        mode->frame(ctx, at);

        const bool lastFrame = args_.frames && at.index + 1 >= args_.frames;
        const bool shotThisFrame =
            args_.shotEvery && (at.index % args_.shotEvery == 0 || lastFrame);
        if (shotThisFrame) {
            char path[1024];
            // The name is ours whole: bgfx hands the path to the callback unchanged and
            // appends nothing, and a shot called 00100 with no suffix is a file nothing opens.
            std::snprintf(path, sizeof(path), "%s/%05d.png", paths_.shots.c_str(), at.index);
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path);
        }

        bgfx::frame();

        const int64_t now = bx::getHPCounter();
        const double cpuMs = double(now - last) * toMs;
        last = now;
        at.deltaSeconds = cpuMs / 1000.0;
        // A screenshot stalls its frame to about 250 ms, and that quarter-second used to be
        // handed to the clips: every shot after the first showed a pose a quarter-second
        // ahead of where a shotless run stands, and any transient shorter than the stall --
        // the 0.18 s crossfade first among them -- could not be photographed at all. The
        // statistics already dropped this frame; the animation clock did not. Capped rather
        // than dropped, because a genuinely slow frame should still advance the world.
        constexpr double kLongestStep = 0.05;  // 50 ms, which is one tick of MU's own 20 Hz
        if (shotThisFrame || at.deltaSeconds > kLongestStep) at.deltaSeconds = kLongestStep;
        if (args_.fixedDtMs > 0.0f) at.deltaSeconds = args_.fixedDtMs * 0.001;
        at.elapsed += at.deltaSeconds;
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
            core::logf("frame %d: %.1f fps, cpu %.2f ms, gpu %.2f ms, %u draws", at.index,
                       1000.0 / cpuMs, cpuMs,
                       double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0 / double(s->gpuTimerFreq),
                       s->numDraw);
            // The transparent pass's pool, for the same reason the culling counts are in the
            // mode's own report: "no allocation per frame in the pools" is sprint 6's proving
            // sentence, and a claim nothing reports is not proved. The high-water mark against
            // the reserve is the evidence -- it cannot exceed it, because add() refuses instead
            // of growing -- and a refusal count above zero means the reserve is too small and
            // the picture is already missing something.
            if (renderer_.effects().highWater() > 0 || renderer_.effects().refused() > 0) {
                core::logf("  effects: %u sprites in %u draws; high water %u of %u reserved, "
                           "%u refused",
                           renderer_.effects().lastSpriteCount(),
                           renderer_.effects().lastDrawCount(), renderer_.effects().highWater(),
                           renderer_.effects().capacity(), renderer_.effects().refused());
            }
            mode->report(ctx);
            sinceLine = 0.0;
        }

        ++at.index;
        if (args_.frames && at.index >= args_.frames) {
            // One segment done. With --repeat the world stays loaded and the next segment
            // starts from a fresh warmup: what separates them is then the machine's own
            // drift, which is the thing worth measuring, rather than the twenty seconds of
            // texture decoding a second launch would spend first.
            if (segment + 1 < args_.repeat) {
                stats.endSegment(segment, args_.repeat);
                ++segment;
                at.index = 0;
                last = bx::getHPCounter();
                continue;
            }
            // The last segment is NOT ended here: its frames stay in hand so finish() can
            // still print the per-account table and the two humps. finish() counts it as a
            // segment itself.
            break;
        }
    }

    const bool withinBudget = stats.finish(args_.budget);

    mode->shutdown(ctx);
    mode.reset();
    teardown();

    const int errors = core::logErrorCount();
    core::logf("%d frames in %d segment(s), %d errors, budget %s", at.index, args_.repeat,
               errors, !args_.budget ? "not enforced" : (withinBudget ? "kept" : "overdrawn"));
    core::logClose();

    if (!withinBudget) return 2;
    return errors ? 1 : 0;
}

}  // namespace mu::app
