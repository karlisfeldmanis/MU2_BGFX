#include "app/application.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <filesystem>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#ifndef MU2_SHIP
#include "app/modes/bench_mode.h"
#endif
#include "app/modes/lobby_mode.h"
#include "app/modes/play_mode.h"
#include "app/sweep.h"
#include "game/sound.h"
#include "content/showing.h"
#include "core/log.h"
#include "core/pack.h"
#include "core/paths.h"
#include "app/alert.h"
#include "core/watch.h"
#ifdef MU2_CRASH_UPLOAD
#include "launcher/crash_upload.h"
#endif
#include "build_version.h"
#include "game/headless.h"
#include "game/world/world_show.h"
#include "gfx/stats.h"
#include "gfx/views.h"
#include "gfx/window.h"

namespace mu::app {

namespace {

// --tape: the mix as a WAV of 32-bit float stereo at 48 kHz, its two lengths written when it
// closes. The engine is opened with no device (Sound::open's offline), so its clock -- every
// fade, duck and ease reads it -- moves only as fast as this pulls it, which is game time.
class Tape {
public:
    static constexpr uint32_t kRate = 48000;

    explicit Tape(const std::string& path) {
        if (path.empty()) return;
        file_ = std::fopen(path.c_str(), "wb");
        if (!file_) {
            core::logError("tape: cannot write %s", path.c_str());
            return;
        }
        header(0);
        core::logf("tape: the mix to %s", path.c_str());
    }
    ~Tape() {
        if (!file_) return;
        std::fseek(file_, 0, SEEK_SET);
        header(uint32_t(written_ * 8));
        std::fclose(file_);
    }
    Tape(const Tape&) = delete;
    Tape& operator=(const Tape&) = delete;

    // Up to `elapsed` seconds of game time, from whatever is heard, or silence where nothing is
    // (a mode with no sound, or the gap while one hands the run to the next) so the tape stays
    // in step with the frames.
    void pull(game::Sound* from, double elapsed) {
        if (!file_) return;
        const uint64_t until = uint64_t(elapsed * kRate + 0.5);
        while (written_ < until) {
            const uint64_t want = std::min<uint64_t>(until - written_, kChunk);
            uint64_t got = from ? from->render(buffer_, want) : 0;
            for (uint64_t i = got * 2; i < want * 2; ++i) buffer_[i] = 0.0f;
            std::fwrite(buffer_, sizeof(float) * 2, size_t(want), file_);
            written_ += want;
        }
    }

private:
    static constexpr uint64_t kChunk = 4096;

    void put32(uint32_t v) { std::fwrite(&v, 4, 1, file_); }
    void put16(uint16_t v) { std::fwrite(&v, 2, 1, file_); }
    void header(uint32_t bytes) {
        std::fwrite("RIFF", 1, 4, file_);
        put32(36 + bytes);
        std::fwrite("WAVEfmt ", 1, 8, file_);
        put32(16);
        put16(3);  // IEEE float
        put16(2);
        put32(kRate);
        put32(kRate * 8);
        put16(8);
        put16(32);
        std::fwrite("data", 1, 4, file_);
        put32(bytes);
    }

    std::FILE* file_ = nullptr;
    uint64_t written_ = 0;
    float buffer_[kChunk * 2];
};

}  // namespace

bool Application::boot() {
    gfx::WindowDesc desc;
    desc.width = args_.width;
    desc.height = args_.height;
    desc.fullscreen = args_.fullscreen;
    desc.vsync = args_.vsync;
    desc.profileViews =
        args_.views || !args_.statsPath.empty() || !args_.budgetOverrides.empty();
    desc.renderThread = args_.renderThread;
    desc.renderer = args_.renderer;
    desc.offscreen = args_.offscreen;
    args_.rendererLoaded = args_.renderer;
    if (!window_.open(desc)) return false;
    // A player's first look at a big screen is not a 4K frame his card cannot draw.
    if (args_.remember && args_.frames == 0 && !args_.graphicsByHand &&
        core::fitGraphics(args_, window_.width(), window_.height())) {
        core::saveOptions(args_);
    }
    if (!args_.rawFrames.empty() && !gfx::offscreenRaw(args_.rawFrames.c_str())) return false;
    // The build names the SPIR-V; a run that came up on Direct3D 11 reads the DXBC beside it.
    paths_.shaders = gfx::shadersFor(paths_.shaders);

    textures_.createDefaults();
    // The Graphics page's Textures, read once: a change waits for the next start.
    textures_.setHalf(args_.textures == 0);
    args_.texturesLoaded = args_.textures;

    if (!renderer_.init(window_.width(), window_.height(), paths_.shaders.c_str(), args_.msaa,
                        uint16_t(args_.shadowSize), args_.scale, args_.metalfx)) {
        core::logError("the renderer did not start");
        textures_.shutdown();
        window_.close();
        return false;
    }
    renderer_.setGpuSceneryOn(args_.gpuScenery);
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
        core::logError("the showing table did not open: %s (pipeline/cook.py --only showing)",
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
    // An --offscreen run's last shots are still being read back; they land before anything goes.
    gfx::offscreenFlush();
    overlay_.shutdown();
    curtain_.shutdown();
    renderer_.shutdown();
    textures_.shutdown();
    window_.close();
}

int Application::run(int argc, char** argv) {
    args_ = core::parseArgs(argc, argv);

    const core::Folders& folders = core::folders();
    paths_.assets = folders.assets;
    // Every world's manifest is read from there on first asking (game/world/world_show.h).
    game::openWorldShows(paths_.assets);
    paths_.shaders = folders.shaders;
    // A copy of another build's shaders, for an A/B of two binaries in one afternoon: the
    // directory is compiled in, so a baseline binary would otherwise read the new shaders.
    if (const char* dir = std::getenv("MU2_SHADER_DIR")) paths_.shaders = dir;
    paths_.sheets = folders.sheets;
    paths_.root = folders.root;
    paths_.log = args_.logPath.empty() ? folders.log : args_.logPath;
    paths_.shots = args_.shotPath.empty() ? folders.shots : args_.shotPath;
    paths_.sheet =
        args_.sheet.empty() ? paths_.under(paths_.sheets, "lighting.json") : args_.sheet;

    core::logOpen(paths_.log.c_str());
#if defined(MU2_SHIP) && defined(MU2_CRASH_UPLOAD)
    // The player's game plays only through the launcher, which signs him in and keeps the game up
    // to date; the server turns away any other way in (server --launcher-only). Said here in words
    // rather than as a connection that fails at the character screen.
    if (!launcher::startedByLauncher() && !args_.headless) {
        core::logError("started without the launcher: nothing run");
        systemAlert("MU2", "Open MU2 Launcher to play.\n\nThe launcher signs you in and keeps the "
                           "game up to date. Sign in there and press Play.");
        core::logClose();
        return 1;
    }
#endif
    // The player's build reads its data from the packs in the bundle (core/pack.h), and has
    // nothing to play without them.
    if (!folders.data.empty() && core::mountPacks(folders.data) == 0) {
        core::logError("no packs in %s: the game's data is missing", folders.data.c_str());
#ifdef MU2_SHIP
        systemAlert("MU2", "The game's files are missing or damaged.\n\nOpen MU2 Launcher; it puts them back.");
#endif
        core::logClose();
        return 1;
    }
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
    if (args_.shotEvery) {
        std::error_code ignored;
        std::filesystem::create_directories(paths_.shots, ignored);
    }

    // A crash or a freeze leaves a report in crashes/ (core/watch.h).
    core::watchStart(folders.crashes.c_str());
    core::watchNote("MU2 " MU2_BUILD_VERSION);
#ifdef MU2_CRASH_UPLOAD
    // What earlier runs left there goes to the website (launcher/crash_upload.h): the player's
    // build always, a developer's only when MU2_CRASH_SITE names a site to try it against.
    {
        const char* asked = std::getenv("MU2_CRASH_SITE");
#ifdef MU2_SHIP
        const std::string site = asked && *asked ? asked : launcher::kCrashSite;
#else
        const std::string site = asked ? asked : "";
#endif
        if (!site.empty()) {
#ifdef _WIN32
            const char* platform = "windows";
#elif defined(__linux__)
            const char* platform = "linux";
#else
            const char* platform = "macos";
#endif
            launcher::CrashFrom from{site, platform, "game", launcher::ticketFromEnvironment(), {}, {}};
            std::vector<std::filesystem::path> where = {std::filesystem::path(folders.crashes)};
            std::thread([where, from] { launcher::sendCrashes(where, from); }).detach();
        }
    }
#endif

    if (!boot()) {
#ifdef MU2_SHIP
        // Said in the system's own box: the launcher has minimised itself by now, and a game that
        // only logs leaves the player looking at nothing (the Windows audit, 2026-10-15).
#ifdef _WIN32
        systemAlert("MU2", "MU2 could not start its graphics.\n\nUpdate your graphics card's driver "
                           "(NVIDIA, AMD or Intel, from their site) and try again. MU2 needs Vulkan "
                           "or DirectX 11 on Windows.");
#elif defined(__linux__)
        systemAlert("MU2", "MU2 could not start its graphics.\n\nMU2 needs Vulkan. Install your "
                           "distribution's Vulkan driver (Mesa's, or NVIDIA's own) and its loader "
                           "(libvulkan1 or vulkan-loader), then try again.");
#else
        systemAlert("MU2", "MU2 could not start its graphics.\n\nUpdate macOS and try again.");
#endif
#endif
        core::logClose();
        return 1;
    }

    Tape tape(args_.tape);

    Context ctx{args_,     paths_,    window_,   renderer_, textures_,
                lighting_, time_,     overlay_,  readout_,  curtain_, goBack_};

    // Either a world or the bench, never both: they are two different things to look at and
    // the camera belongs to whichever it is.
    // Or the character screen, which hands the run to a world when a character is picked.
    std::unique_ptr<Mode> mode;
#ifdef MU2_SHIP
    // The player's build is the game and nothing else: always the character screen.
    mode = std::make_unique<LobbyMode>();
#else
    if (args_.lobby) mode = std::make_unique<LobbyMode>();
    else if (!args_.world.empty()) mode = std::make_unique<PlayMode>();
    else mode = std::make_unique<BenchMode>();
#endif

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

    // The performance sweep, when asked: the hero put down on each tile of a list in turn and
    // each measured, with the world loaded once (app/sweep.h). It ends the run itself.
    Sweep sweep;
    if (!args_.sweepPath.empty() && !sweep.open(ctx, *mode)) {
        mode->shutdown(ctx);
        mode.reset();
        teardown();
        core::logClose();
        return 1;
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
    // The GPU's watch, for a player's run whose Graphics rows nobody set by hand: eight seconds
    // in a row below 40 frames a second with the GPU over 25 ms of each frame, and the rows go one
    // preset lighter (core::easeGraphics), as a lost device does for the next start
    // (gfx/callback.cpp). Garisons' GTX 1070 drew High at 4K at 15 fps, 2026-10-16. A slow frame
    // the GPU is not behind -- a load, a dragged window -- is not the settings' to answer.
    double gpuSum = 0.0, wallSum = 0.0;
    int gpuFrames = 0, behind = 0;
    bool lightest = false;

    bool quitEarly = mode->quitEarly();
    bool handoffFailed = false;
    // The mode the OS's hold reaches, while it is running frames: not while one opens behind
    // the preloader, nor once it is shut down. The freeze watch is told the frames stopped
    // on purpose, so a held title bar files no report (gfx::Window::whileHeld).
    Mode* framing = mode.get();
    window_.whileHeld([&framing] {
        core::watchPause();
        if (framing) framing->held();
    });
    while (!quitEarly && !mode->quitting() && !args_.restart && window_.pump() &&
           !window_.escapePressed()) {
        core::watchBeat();
        renderer_.resize(window_.width(), window_.height());
        renderer_.setDisplayScale(window_.contentScale());

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

        sweep.before();
        mode->frame(ctx, at);

        const bool lastFrame = args_.frames && at.index + 1 >= args_.frames;
        const bool shotThisFrame =
            args_.shotEvery && (at.index % args_.shotEvery == 0 || lastFrame);
        if (shotThisFrame) {
            char path[1024];
            // The name is ours whole: bgfx hands the path to the callback unchanged and
            // appends nothing, and a shot called 00100 with no suffix is a file nothing opens.
            std::snprintf(path, sizeof(path), "%s/%05d.png", paths_.shots.c_str(), at.index);
            if (!gfx::offscreenShot(path)) bgfx::requestScreenShot(gfx::screenTarget(), path);
        }

        gfx::beforeFrame();
        gfx::afterFrame(bgfx::frame());

        // The work: from the end of the last frame's wait to the end of this present. The
        // pacing wait below is deliberately outside it, so `cpu` stays the frame's cost and
        // a paced run's budget table reads the same as a free one's.
        const double cpuMs = double(bx::getHPCounter() - last) * toMs;

        // The pace. Vsync alone is not a steady picture on a fast display: this frame costs
        // 7.6 ms in the middle and 12.3 ms at the 99th, and 180 Hz refreshes every 5.56 ms,
        // so the present lands on the second refresh or the third as the cost crosses 11.1 ms
        // and the picture steps between 90 and 60 several times a second. Held to a period
        // the frame fits inside every time, it lands on the same refresh every time. A steady
        // 60 reads smoother than an average of 130 that keeps changing its mind.
        //
        // Waited out here rather than asked of bgfx, which has vsync on or off and nothing
        // in between. 0 leaves it free, and every measurement is taken at 0.
        //
        // Minimised, at most kMinimisedCap: a present to an icon returns at once on Direct3D 11
        // (DXGI_STATUS_OCCLUDED), so a free run drew whole frames flat out for nobody, the card
        // as hot as in play -- Gordichan's GTX 1650 laptop lost its device every few minutes,
        // 2026-10-17, uncapped at 150 fps. The line and the world still step each frame.
        constexpr int kMinimisedCap = 30;
        const int cap = window_.minimised() && (args_.cap == 0 || args_.cap > kMinimisedCap)
                            ? kMinimisedCap
                            : args_.cap;
        if (cap > 0) {
            const double periodMs = 1000.0 / double(cap);
            for (;;) {
                const double aheadMs = periodMs - double(bx::getHPCounter() - last) * toMs;
                if (aheadMs <= 0.0) break;
                // The last stretch is spun, not slept: the OS wakes when it wakes, and a
                // millisecond late is a missed refresh, which is the judder this is for.
                if (aheadMs > 1.5) {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(int64_t((aheadMs - 1.0) * 1000.0)));
                }
            }
        }

        const int64_t now = bx::getHPCounter();
        // The world's clock is wall time, the wait included, or a paced run would play in
        // slow motion: the hero would walk 60 frames' worth of ground in a second and the
        // clips would run at the ratio of the work to the period.
        const double frameMs = double(now - last) * toMs;
        last = now;
        at.deltaSeconds = frameMs / 1000.0;
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
        // The frame just drawn sounds for the length the next one advances by.
        tape.pull(mode->heard(), at.elapsed);
        // A frame that writes a screenshot is not a frame of the game, and it does not go in
        // the statistics. The readback stalls this one frame to about 253 ms, and a mean over
        // 570 frames carries that as about 1.6 ms of pure measurement apparatus -- which is
        // how sprint 2 published 3.075 ms still and 4.085 moving under a command line that
        // said `--frames 600 --world lorencia` and had actually been run with `--shot 200`.
        // Six runs of the command as written measured 2.16 ms for both. The cost of taking a
        // picture belongs to the reviewer, not to the frame being reviewed.
        if (!shotThisFrame) stats.sample(cpuMs);
        sweep.after(cpuMs);

        if (const bgfx::Stats* s = bgfx::getStats(); s->gpuTimerFreq > 0) {
            gpuSum += double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0 / double(s->gpuTimerFreq);
            wallSum += frameMs;
            ++gpuFrames;
        }

        // Wall time, so the line stays a line a second when the run is paced.
        sinceLine += frameMs;
        sinceSheetCheck += frameMs;
        if (sinceLine >= 1000.0) {
            const bgfx::Stats* s = bgfx::getStats();
            core::logf("frame %d: %.1f fps, cpu %.2f ms, gpu %.2f ms, %u draws", at.index,
                       1000.0 / frameMs, cpuMs,
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

            if (args_.remember && args_.frames == 0 && !args_.graphicsByHand && !lightest &&
                gpuFrames > 0) {
                constexpr double kBehindMs = 25.0;
                constexpr int kSeconds = 8;
                const bool slow = gpuSum / gpuFrames > kBehindMs && wallSum / gpuFrames > kBehindMs;
                behind = slow ? behind + 1 : 0;
                if (behind >= kSeconds) {
                    behind = 0;
                    if (const char* to = core::easeGraphics(args_)) {
                        core::logf("graphics: the GPU took over %.0f ms a frame for %d s; eased to %s",
                                   kBehindMs, kSeconds, to);
                        core::saveOptions(args_);
                        mode->graphicsEased(ctx, to);
                    } else {
                        lightest = true;
                    }
                }
            }
            gpuSum = wallSum = 0.0;
            gpuFrames = 0;
        }

        // A mode that is done hands the run on: the screen to the world, the world back to the
        // screen. The one going shuts down first, so the world's save is kept and its meshes go
        // back before the next one loads, and what it left in the renderer that belongs to a
        // world -- the lamps' grid, a moving light, the grey of a death -- is cleared, since
        // the next world sets its own and may not set all of them.
        if (const Mode::Next next = mode->next(); next != Mode::Next::None) {
            sweep.abort("the world handed the run on (a gate, or the lobby)");
            core::watchPause();
            framing = nullptr;
            mode->shutdown(ctx);
            mode.reset();
            renderer_.setPointLights(nullptr, 0, 0.0f, 0.0f, 0.0f);
            renderer_.setTransientLights(nullptr, 0);
            renderer_.setDrain(0.0f);
            window_.holdEscape(false);
            window_.setTyping(false);
            if (next == Mode::Next::Lobby) mode = std::make_unique<LobbyMode>();
            else mode = std::make_unique<PlayMode>();
            core::logf("handing the run to the %s", next == Mode::Next::Lobby
                                                        ? "character screen"
                                                        : "world");
            if (!mode->open(ctx)) {
                handoffFailed = true;
                break;
            }
            if (mode->quitEarly()) {
                quitEarly = true;
                break;
            }
            // The load is not a frame: the next one is timed from here.
            last = bx::getHPCounter();
            framing = mode.get();
        }

        ++at.index;
        if (sweep.done()) break;
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

    core::watchStop();
    const bool withinBudget = stats.finish(args_.budget);
    if (sweep.active()) sweep.abort("the window closed or the frames ran out");
    sweep.close();

    framing = nullptr;
    window_.whileHeld(nullptr);
    if (!handoffFailed) mode->shutdown(ctx);
    mode.reset();
    teardown();
    if (handoffFailed) core::logError("the mode handed to did not open");

    const int errors = core::logErrorCount();
    core::logf("%d frames in %d segment(s), %d errors, budget %s", at.index, args_.repeat,
               errors, !args_.budget ? "not enforced" : (withinBudget ? "kept" : "overdrawn"));
    core::logClose();

    if (!withinBudget) return 2;
    return errors ? 1 : 0;
}

}  // namespace mu::app
