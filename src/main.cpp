// MU2 on bgfx. Sprint 1: the whole six-view frame, with the model bench in front of it.
// See PLAN.md and docs/sprints/.
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <cstdio>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "core/files.h"
#include "core/args.h"
#include "core/log.h"
#include "game/bench.h"
#include "game/headless.h"
#include "sim/realm.h"
#include "game/world.h"
#include "gfx/lighting.h"
#include "gfx/overlay.h"
#include "game/desk.h"
#include "gfx/renderer.h"
#include "gfx/stats.h"
#include "gfx/views.h"
#include "gfx/window.h"

using namespace mu;

namespace {

std::string defaultPath(const char* dir, const char* name) {
    return std::string(dir) + "/" + name;
}

// What the day gives an unlit puff of smoke: the ambient and the sun on a flat surface, over
// what the default sheet's noon gives it. The transparent pass lights nothing, so the lamps'
// smoke is lit by this. Shared by the world and the viewer's stage.
float daylightOf(const gfx::Lighting& lighting) {
    const float sky = lighting.ambientStrength +
                      lighting.sunStrength * std::sin(lighting.elevation * 3.14159265f / 180.0f) /
                          3.14159265f;
    return std::clamp(sky / 1.45f, 0.08f, 1.0f);
}

// The viewer's list, down the left, and the hit test that goes with it.
//
// A window onto the list rather than the whole of it: 156 names do not fit at a size anybody
// can read, and a list that scrolls past what is selected is no use for choosing. The
// selection is held in the middle of the window where it can be, so the eye stays in one
// place while the names move past it.
//
// Drawing and picking are one function on purpose. They share the same arithmetic -- where a
// row starts, how tall it is, which slice of the list is on screen -- and two copies of that
// drift the moment either changes, which is a list that highlights one name and selects
// another. `pointer` is where the mouse is; the row under it comes back in `hovered`, and
// what the panel covers in `bounds`, so the caller can tell a click on the list from a drag
// on the model.
struct ListHit {
    long long hovered = -1;   // the row the pointer is over, or -1
    long long tab = -1;       // the category tab the pointer is over, or -1
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool over = false;        // the pointer is somewhere on the panel
};

ListHit drawBrowserList(gfx::Overlay& overlay, const game::ModelBench& bench, int width,
                        int height, float pointerX, float pointerY) {
    // Every frame starts empty and hands the overlay the backbuffer's size. Forgetting this
    // is not a small bug: the quads pile up run-long, and the size the vertex shader divides
    // by stays zero, so the whole list is one NaN off the screen and nothing draws at all.
    overlay.begin(width, height);
    ListHit hit;

    // The face is Open Sans at a 8*scale line box, so this is the list's text size in the
    // only units the overlay has. 2.0 was right for the 5x7 bitmap, whose letters filled
    // their box; a proportional face at the same box reads a size smaller, and this is the
    // value that puts it back where a name is legible in a 1080p shot.
    constexpr float kScale = 2.8f;
    constexpr float kPad = 10.0f;
    constexpr uint32_t kBack = 0xD8140d0au;    // abgr: a dark wash, so names read over grass
    constexpr uint32_t kInk = 0xFFc8c8c8u;
    constexpr uint32_t kChosen = 0xFFffffffu;
    constexpr uint32_t kDim = 0xFF8a8a8au;
    constexpr uint32_t kChosenBar = 0xB0705030u;
    constexpr uint32_t kHoverBar = 0x60606060u;
    constexpr uint32_t kRule = 0x40ffffffu;

    // A quarter of a line of leading. The font's own cell is one pixel taller than its
    // glyphs, which is enough to keep two lines from touching and not enough to read a
    // hundred names down: set solid, the list is a grey block and the eye slides off it.
    constexpr float kLeading = 1.25f;
    const float line = gfx::Overlay::lineHeight(kScale) * kLeading;
    const size_t count = bench.browseCount();
    if (count == 0) return hit;

    // The categories, one row each above the names. Vertical rather than a strip across the
    // top: the panel is already as wide as the widest name and no wider, and four labels laid
    // side by side either overflow that or have to be shortened until they stop being the
    // words the log uses for the same thing.
    const float tabScale = kScale * 0.9f;
    const float tabLine = gfx::Overlay::lineHeight(tabScale) * kLeading * 1.15f;
    const size_t tabs = bench.categoryCount() > 8 ? 8 : bench.categoryCount();
    const float tabBlock = float(tabs) * tabLine + kPad;

    const float header = line * 1.6f;
    const float footer = line * 1.6f;
    // As many as fit in the top three quarters, so the list never runs into the frame line
    // the log prints at the bottom of a review shot.
    size_t rows =
        size_t((float(height) * 0.75f - kPad * 2.0f - tabBlock - header - footer) / line);
    if (rows < 1) rows = 1;
    if (rows > count) rows = count;
    const size_t half = rows / 2;
    size_t first = bench.browseIndex() > half ? bench.browseIndex() - half : 0;
    if (first + rows > count) first = count > rows ? count - rows : 0;
    const size_t last = first + rows;

    float widest = overlay.measure(kScale, "COOKED MODELS  999/999");
    for (size_t i = first; i < last; ++i) {
        const float w = overlay.measure(kScale, bench.browseName(i));
        if (w > widest) widest = w;
    }
    char tabLabels[8][96];
    for (size_t i = 0; i < tabs && i < 8; ++i) {
        std::snprintf(tabLabels[i], sizeof(tabLabels[i]), "%s  %zu",
                      bench.category(i).label.c_str(), bench.category(i).entries.size());
        const float w = overlay.measure(tabScale, tabLabels[i]);
        if (w > widest) widest = w;
    }

    hit.x = kPad;
    hit.y = kPad;
    hit.w = widest + kPad * 3.0f;
    hit.h = tabBlock + header + float(rows) * line + footer + kPad;
    overlay.panel(hit.x, hit.y, hit.w, hit.h, kBack);
    hit.over = pointerX >= hit.x && pointerX < hit.x + hit.w && pointerY >= hit.y &&
               pointerY < hit.y + hit.h;

    // The tabs. An empty category is drawn dim and cannot be chosen -- a world cooked with
    // no figures still shows that the monsters are a thing the viewer has, and that there
    // are none of them here, which is a different statement from the tab not existing.
    for (size_t i = 0; i < tabs && i < 8; ++i) {
        const float y = hit.y + kPad * 0.3f + float(i) * tabLine;
        const bool empty = bench.category(i).entries.empty();
        const bool open = i == bench.categoryIndex();
        const bool over = !empty && hit.over && pointerY >= y && pointerY < y + tabLine;
        if (over) hit.tab = (long long)i;
        if (open || over) {
            overlay.panel(hit.x + 2.0f, y - 1.0f, hit.w - 4.0f, tabLine,
                          open ? kChosenBar : kHoverBar);
        }
        overlay.text(hit.x + kPad, y, tabScale, empty ? kDim : (open ? kChosen : kInk),
                     tabLabels[i]);
    }
    overlay.panel(hit.x + kPad, hit.y + tabBlock - 3.0f, hit.w - kPad * 2.0f, 1.0f, kRule);

    char label[96];
    std::snprintf(label, sizeof(label), "%s  %zu/%zu",
                  bench.category(bench.categoryIndex()).label.c_str(), bench.browseIndex() + 1,
                  count);
    overlay.text(hit.x + kPad, hit.y + tabBlock + kPad * 0.6f, kScale, kDim, label);
    // A rule under the heading and above the footer, which is the whole of the chrome: a
    // panel with a line at each end reads as a list, and costs two quads.
    overlay.panel(hit.x + kPad, hit.y + tabBlock + header - 3.0f, hit.w - kPad * 2.0f, 1.0f,
                  kRule);

    const float top = hit.y + tabBlock + header;
    for (size_t i = first; i < last; ++i) {
        const float y = top + float(i - first) * line;
        const bool chosen = i == bench.browseIndex();
        const bool over = hit.over && pointerY >= y && pointerY < y + line;
        if (over) hit.hovered = (long long)i;
        if (chosen || over) {
            overlay.panel(hit.x + 2.0f, y - 1.0f, hit.w - 4.0f, line, chosen ? kChosenBar
                                                                             : kHoverBar);
        }
        overlay.text(hit.x + kPad, y, kScale, chosen ? kChosen : kInk, bench.browseName(i));
    }

    const float footTop = top + float(rows) * line;
    overlay.panel(hit.x + kPad, footTop + 2.0f, hit.w - kPad * 2.0f, 1.0f, kRule);
    // What the footer says depends on what is standing there: with a figure the useful thing
    // is which of its clips is running, because that is the one fact a still cannot show.
    overlay.text(hit.x + kPad, footTop + 6.0f, kScale * 0.75f, kDim,
                 // Brackets are drawn again now the face is a real one. They were spelled
                 // out as the word for a while, because the 5x7 fallback has no bracket in
                 // it and dropped the two keys this line exists to name.
                 bench.hasFigure() ? "Tab: category   [ ]: clip   Drag turns   Wheel zooms"
                                   : "Tab: category   Arrows walk   Drag turns   Wheel zooms");
    return hit;
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
    if (!renderer.init(window.width(), window.height(), MU2_SHADER_DIR, args.msaa,
                       uint16_t(args.shadowSize))) {
        core::logError("the renderer did not start");
        textures.shutdown();
        window.close();
        core::logClose();
        return 1;
    }

    gfx::Lighting lighting;
    // The viewer's times of day: the sheet alone at noon, and sheets/time/<name>.json laid
    // over it otherwise. Rebuilt from the sheet on every change rather than patched, so going
    // from night back to noon cannot leave a night value behind.
    static const char* kTimes[] = {"noon", "dusk", "night"};
    int daytime = args.time == "dusk" ? 1 : (args.time == "night" ? 2 : 0);
    int64_t overlayStamp = 0;
    auto overlayPath = [&](int which) {
        return defaultPath(MU2_SHEET_DIR, (std::string("time/") + kTimes[which] + ".json").c_str());
    };
    auto applyTime = [&]() {
        lighting = gfx::Lighting();
        lighting.reloadIfChanged(sheetPath);
        overlayStamp = 0;
        if (daytime > 0) {
            const std::string overlay = overlayPath(daytime);
            overlayStamp = core::fileModified(overlay);
            lighting.readOverlay(overlay);
        }
        if (daytime > 0 || args.browse) core::logf("time of day: %s", kTimes[daytime]);
    };
    applyTime();

    // Either a world or the model bench, never both: they are two different things to look
    // at and the camera belongs to whichever it is.
    game::World world;
    // The windows, over a played world and nowhere else: a bench has nobody to show them for.
    // Opened by the preloader's worker with the rest of the world.
    game::Desk desk;
    const bool inWorld = !args.world.empty();
    // The game's own entrance, only when somebody is playing: the frame fades up from black
    // and the character dissolves in (Play::appear). A review run (--frames) is left alone,
    // because a shot of a black frame measures nothing.
    const bool entrance = inWorld && args.play && (args.frames == 0 || args.entrance);
    // The spinner, and after it the black the frame fades up from. Its own overlay: the
    // viewer's list below is a different thing and is only built for the browser.
    gfx::Overlay curtain;
    bool quitEarly = false;
    if (inWorld) {
        if (args.atSet) world.setFocusTile(args.atColumn, args.atRow);

        // ---- The preloader ------------------------------------------------------------------
        //
        // The window is open and the renderer is up; everything the world needs is loaded on
        // a worker thread while this one does nothing but draw a spinner and present it. The
        // spinner therefore cannot hitch on a load, however long one takes: this thread never
        // waits on a file, a decode or a build, only on the display's vsync.
        //
        // What makes it legal is bgfx's resource lock (CMakeLists.txt, BGFX_CONFIG_MULTITHREADED):
        // the loaders create textures, buffers, shaders and uniforms and call nothing else in
        // bgfx, and those calls are locked against this thread's bgfx::frame(). The uploads
        // themselves still happen here, inside frame(), a frame's worth of creations at a
        // time. Nothing else is shared: this thread touches neither the world nor `textures`
        // until the worker has been joined.
        //
        // The light grid is NOT the worker's: it is the renderer's own state, and it is laid
        // below, after the join, in two milliseconds.
        std::atomic<int> loaded{0};  // 0 loading, 1 ready, -1 the world did not open
        std::thread loader([&]() {
            // No crowd when the realm is going to be raised: the crowd stands monsters where
            // it chooses and the sim stands them where they are, and raising both means
            // loading, posing and then throwing away 45 figures a run.
            const bool ok = world.open(MU2_ASSET_DIR, args.world, textures,
                                       args.play ? 0 : args.crowd, args.figuresOn);
            // And the realm behind it, when there is somebody playing. A world that cannot
            // raise one -- no cooked tables yet -- says so and is still a world to look at.
            if (ok && args.play) {
                world.play(MU2_ASSET_DIR, args.world, args.seed, args.kin, args.level,
                           args.weapon, args.shield);
                // What a blow looks like and where a click sent him. Not fatal: open() has
                // said why in the log.
                world.played().showing().open(MU2_ASSET_DIR, textures);
                world.played().marker().open(MU2_ASSET_DIR, textures);
                // Not fatal either: a game with no HUD is still a game.
                if (world.played().isOpen() && args.windows != "off" &&
                    !desk.open(MU2_SHADER_DIR, MU2_ASSET_DIR, &textures)) {
                    core::logError("the windows did not open; playing without a HUD");
                }
            }
            loaded.store(ok ? 1 : -1);
        });

        curtain.init(MU2_SHADER_DIR);
        // The spinner's dot: a soft disc, made here in a few lines rather than loaded, because
        // nothing of the cook is in hand yet. Drawn through the effects pass, in screen space,
        // so a dot is a round antialiased sprite -- the overlay can only lay axis-aligned
        // rectangles, and a ring built out of those reads as a staircase of squares.
        bgfx::TextureHandle dot = BGFX_INVALID_HANDLE;
        {
            constexpr int kSide = 64;
            const bgfx::Memory* pixels = bgfx::alloc(kSide * kSide * 4);
            for (int y = 0; y < kSide; ++y) {
                for (int x = 0; x < kSide; ++x) {
                    const float dx = (float(x) + 0.5f) / kSide * 2.0f - 1.0f;
                    const float dy = (float(y) + 0.5f) / kSide * 2.0f - 1.0f;
                    const float r = std::sqrt(dx * dx + dy * dy);
                    // Solid to 0.55 of the radius and out to nothing by 1.0: a soft edge rather
                    // than a hard circle, which at eight pixels across is what reads as round.
                    const float a = std::max(0.0f, std::min(1.0f, (1.0f - r) / 0.45f));
                    uint8_t* at = pixels->data + (size_t(y) * kSide + size_t(x)) * 4;
                    at[0] = at[1] = at[2] = 255;
                    at[3] = uint8_t(a * a * 255.0f);
                }
            }
            dot = bgfx::createTexture2D(kSide, kSide, false, 1, bgfx::TextureFormat::RGBA8,
                                        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, pixels);
        }
        // A comet arc chasing round: its head is the brightest and largest dot, at a continuous
        // angle, so it moves by fractions of a dot a frame rather than stepping between them.
        // MU's marker gold.
        const auto spinner = [&](float alpha, double seconds) {
            const int w = window.width(), h = window.height();
            bgfx::setViewFrameBuffer(gfx::ViewHud, BGFX_INVALID_HANDLE);
            bgfx::setViewRect(gfx::ViewHud, 0, 0, uint16_t(w), uint16_t(h));
            bgfx::setViewClear(gfx::ViewHud, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff,
                               1.0f, 0);
            bgfx::touch(gfx::ViewHud);
            const float unit = float(h) / 1080.0f;
            const float cx = float(w) * 0.5f, cy = float(h) * 0.5f;
            const float radius = 30.0f * unit;
            if (alpha > 0.0f && bgfx::isValid(dot)) {
                // Screen space through the effects pass: an orthographic projection in pixels
                // with y running down, and a view that is the identity, which leaves its
                // billboard axes as x right and y down -- so a sprite's position is a pixel and
                // its half-extents are pixels too.
                float screen[16];
                bx::mtxOrtho(screen, 0.0f, float(w), float(h), 0.0f, -1.0f, 1.0f, 0.0f,
                             bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
                float eye[16];
                bx::mtxIdentity(eye);
                renderer.effects().begin();
                const auto put = [&](float x, float y, float half, float a, bool add) {
                    gfx::Sprite s;
                    s.position[0] = x;
                    s.position[1] = y;
                    s.position[2] = 0.0f;
                    s.halfWidth = s.halfHeight = half;
                    s.sheet = dot;
                    s.blend = add ? gfx::Blend::Additive : gfx::Blend::Alpha;
                    // MU's marker gold, warmer at the head than at the tail.
                    s.colour[0] = 1.0f;
                    s.colour[1] = 0.74f;
                    s.colour[2] = 0.38f;
                    s.colour[3] = a * alpha;
                    renderer.effects().add(s);
                };
                // The track the ribbon runs on: a faint closed ring, so the arc is read as
                // something travelling round a circle rather than as a shape adrift.
                // Enough of them, and wide enough, that they overlap into a line: at 72 dots
                // of 3 px the ring read as a dotted circle, which is the opposite of smooth.
                for (int i = 0; i < 160; ++i) {
                    const float angle = float(i) / 160.0f * 6.2831853f;
                    put(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius,
                        2.2f * unit, 0.055f, false);
                }
                // The ribbon: enough overlapping dots that it is a continuous stroke and not a
                // string of beads, thickening and brightening towards the head. Additive, so
                // where the dots overlap it glows rather than banding.
                const float head = float(seconds) * 4.6f;  // radians a second
                constexpr int kAlong = 120;
                constexpr float kSweep = 3.5f;  // radians of arc behind the head
                for (int i = 0; i < kAlong; ++i) {
                    const float along = float(i) / float(kAlong - 1);  // 0 tail .. 1 head
                    const float angle = head - (1.0f - along) * kSweep;
                    // Eased at both ends: the tail dies away instead of stopping, and the head
                    // is a rounded cap rather than a cut.
                    const float taper = along * along * (3.0f - 2.0f * along);
                    const float cap = 1.0f - std::pow(std::max(0.0f, along - 0.94f) / 0.06f, 2.0f);
                    put(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius,
                        (1.1f + 2.2f * taper) * unit, 0.10f * taper * std::max(0.0f, cap), true);
                }
                // And the flare at the head: two soft haloes over the brightest dot, which is
                // what makes it read as a light being carried round rather than a painted arc.
                put(cx + std::cos(head) * radius, cy + std::sin(head) * radius, 5.0f * unit,
                    0.55f, true);
                put(cx + std::cos(head) * radius, cy + std::sin(head) * radius, 11.0f * unit,
                    0.16f, true);
                put(cx + std::cos(head) * radius, cy + std::sin(head) * radius, 22.0f * unit,
                    0.05f, true);
                const float at[3] = {0.0f, 0.0f, 1.0f};
                renderer.effects().draw(gfx::ViewHud, eye, screen, at);
            }
            if (curtain.ready() && alpha > 0.0f) {
                curtain.begin(w, h);
                const float scale = 2.2f * unit;
                const char* word = "Loading";
                const float across = curtain.measure(scale, word);
                const uint32_t ink = (uint32_t(alpha * 0.5f * 255.0f) << 24) | 0x00c8d8e6u;
                curtain.text(cx - across * 0.5f, cy + radius + 24.0f * unit, scale, ink, word);
                curtain.submit(gfx::ViewHud);
            }
            bgfx::frame();
        };
        const int64_t spun = bx::getHPCounter();
        const auto since = [&](int64_t from) {
            return double(bx::getHPCounter() - from) / double(bx::getHPFrequency());
        };
        // Up while the worker loads, and then a quarter second out, on black, so the game does
        // not arrive on top of a spinner caught mid-turn. A window closed meanwhile still
        // waits for the worker -- a load cannot be abandoned half-made -- and then leaves.
        //
        // Presented WITHOUT vsync, and paced here instead. bgfx holds its resource lock through
        // the whole of a frame -- in single-threaded rendering the frame renders, and waits for
        // the display, inside it -- so a spinner paced by vsync held the lock nearly all the
        // time and the worker crept: 3.6 s for what loads in 0.35 s alone. Unsynced, a frame
        // holds it for about a millisecond, the rest of each 8 ms is the worker's, and the
        // spinner's angle is taken from the clock at the moment it is drawn, so its motion is
        // even whatever the cadence. Tearing is possible and invisible: a small arc on black.
        window.holdVsync(true);
        // Paced to the display's own refresh by DEADLINES rather than by sleeping a slice after
        // each frame: sleeping a fixed slice after variable work makes the spacing drift with
        // whatever the frame cost, which is the judder an eye reads as a stutter. Each frame is
        // aimed at the next whole tick of the refresh, and one that overruns aims at the one
        // after rather than trying to catch up.
        //
        // At the display's own rate. A spinner frame costs 2.4 ms of this thread and the rest
        // of each period is the loader's; what used to spoil the cadence was the upload of
        // everything the loader had created since the last frame, which the valve above now
        // meters. The first frame is the exception at about 17 ms -- Metal compiles the pass's
        // pipeline there -- and it happens while the spinner is still fading up from nothing.
        const double period = 1.0 / double(window.refreshHz());
        double due = since(spun);
        double worstGap = 0.0, lastAt = -1.0, frameCost = 0.0, worstFrame = 0.0;
        int spins = 0;
        // A shot of the spinner itself, for review: the loop below is not the frame loop and
        // --shot does not reach it. MU2_SPIN_SHOT=/abs/path.png takes one a second in.
        const char* spinShot = std::getenv("MU2_SPIN_SHOT");
        bool spinShotTaken = false;
        while (loaded.load() == 0 || (spinShot != nullptr && since(spun) < 1.4)) {
            if (!quitEarly && (!window.pump() || window.escapePressed())) quitEarly = true;
            if (!quitEarly) {
                const double at = since(spun);
                // After the first three: the first frame compiles the pass's pipeline and the
                // spinner is invisible through it anyway, and a worst case dominated by it says
                // nothing about the cadence an eye sees.
                if (lastAt >= 0.0 && spins > 3) worstGap = std::max(worstGap, at - lastAt);
                lastAt = at;
                ++spins;
                const int64_t before = bx::getHPCounter();
                spinner(std::min(1.0f, float(at) / 0.15f), at);
                const double cost = since(before);
                worstFrame = std::max(worstFrame, cost);
                frameCost += cost;
            }
            if (spinShot != nullptr && !spinShotTaken && since(spun) > 1.0) {
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, spinShot);
                spinShotTaken = true;
            }
            due += period;
            if (due < since(spun)) due = since(spun);  // overran: aim at the next one, not at a catch-up
            const double left = due - since(spun);
            // Most of the wait asleep and the last of it spinning on the clock: a sleep is only
            // accurate to a millisecond or so, which at 120 Hz is an eighth of a frame.
            if (left > 0.002) {
                std::this_thread::sleep_for(std::chrono::microseconds(int((left - 0.002) * 1e6)));
            }
            while (since(spun) < due) {
            }
        }
        loader.join();
        window.holdVsync(false);
        if (bgfx::isValid(dot)) bgfx::destroy(dot);
        core::logf("preloader: the world loaded behind the spinner in %.2f s; %d spinner frames "
                   "at %d Hz, mean %.1f ms, worst gap %.1f ms, frame %.1f ms mean %.1f worst",
                   since(spun), spins, window.refreshHz(),
                   spins > 0 ? since(spun) * 1000.0 / spins : 0.0, worstGap * 1000.0,
                   spins > 0 ? frameCost * 1000.0 / spins : 0.0, worstFrame * 1000.0);
        // The HUD view goes back to drawing over the frame rather than clearing it.
        bgfx::setViewClear(gfx::ViewHud, BGFX_CLEAR_NONE);

        if (loaded.load() < 0) {
            core::logError("the world did not open");
            // The world may have failed half-open -- `--at` off the map is refused after the
            // ground's buffers are already made -- and a failure path that skips the world's
            // own shutdown leaks a vertex and an index buffer past bgfx's own shutdown.
            world.shutdown();
            curtain.shutdown();
            renderer.shutdown();
            textures.shutdown();
            window.close();
            core::logClose();
            return 1;
        }
        // The lamps' static set, once: the renderer lays its light grid over the ground here.
        if (args.lampsOn) world.lamps().light(renderer);
        // Placed once before the first frame: the loop answers the pointer against the camera
        // already on screen, and on frame zero there has to be one.
        world.update(0.0, args.still);
        // He comes in once the frame has faded most of the way up. Long enough a wait to
        // outlast the first frame, which compiles every pipeline and takes a tenth of a second.
        if (entrance && world.played().isOpen()) world.played().appear(0.3f);
    }

    if (inWorld && world.played().isOpen() && !args.give.empty()) {
        size_t from = 0;
        while (from <= args.give.size()) {
            const size_t comma = args.give.find(',', from);
            const std::string one = args.give.substr(from, comma - from);
            const size_t colon = one.find(':');
            if (!one.empty()) {
                world.played().give(one.substr(0, colon),
                                    colon == std::string::npos ? 1 : std::atoi(one.c_str() + colon + 1));
            }
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
    }
    if (inWorld && world.played().isOpen()) {
        if (args.zen > 0) world.played().earn(args.zen);
        if (!args.talk.empty()) world.played().talkTo(args.talk);
    }
    if (args.windows.find("inventory") != std::string::npos) desk.setInventoryOpen(true);
    if (args.windows.find("character") != std::string::npos) desk.setCharacterOpen(true);

    // The viewer's list. Only the browser has anything to put on it, so it is only built
    // there: an overlay nobody draws still costs a program and a texture.
    gfx::Overlay overlay;

    game::ModelBench bench;
    if (args.distance > 0.0f) bench.setDistance(args.distance);
    const std::string modelPath =
        (args.model.empty() || args.model[0] == '/')
            ? args.model
            : defaultPath(MU2_ASSET_DIR, args.model.c_str());
    // --figure is the monster bench and --model the model bench: one figure out of the cook
    // with its clips, or one .glb as it sits on disk. Never both, and the figure wins.
    const bool figureBench = !inWorld && !args.figure.empty();
    const bool browseBench = !inWorld && !figureBench && args.browse;
    const std::string benchWorld = args.world.empty() ? "lorencia" : args.world;
    if (figureBench && !bench.openFigure(MU2_ASSET_DIR, benchWorld,
                                         args.figure, args.clip, args.safe, textures)) {
        core::logError("the bench did not open");
        renderer.shutdown();
        textures.shutdown();
        window.close();
        core::logClose();
        return 1;
    }
    if (browseBench && !overlay.init(MU2_SHADER_DIR)) {
        core::logError("the viewer opened without its list; the names are in the log");
    }
    // The studio: the world the game draws, opened as a world is -- town, ground, baked light,
    // lamps and fires -- with nobody in it, and the browser's subject stood beside the map's
    // own bonfire nearest the middle of the town. It is drawn from the bench's camera, so it
    // is not `inWorld`: the world is scenery here, and the item is what is looked at.
    const bool studio = browseBench && args.studio;
    float studioStand[3] = {0.0f, 0.0f, 0.0f};
    float studioFire[3] = {0.0f, 0.0f, 0.0f};
    if (studio) {
        if (!world.open(MU2_ASSET_DIR, benchWorld, textures, 0, false)) {
            core::logError("the studio's world did not open");
            world.shutdown();
            renderer.shutdown();
            textures.shutdown();
            window.close();
            core::logClose();
            return 1;
        }
        if (args.lampsOn) world.lamps().light(renderer);
        const content::CookedTown& cooked = world.town().cooked();
        const float middle = float(world.ground().size()) * 0.5f * world.ground().metresPerTile();
        float best = 1e30f;
        for (const content::TownInstance& one : cooked.instances) {
            if (one.model >= cooked.models.size() || cooked.models[one.model].name != "Bonfire01") {
                continue;
            }
            const float dx = one.position[0] - middle, dz = one.position[2] + middle;
            if (dx * dx + dz * dz < best) {
                best = dx * dx + dz * dz;
                for (int i = 0; i < 3; ++i) studioFire[i] = one.position[i];
            }
        }
        if (best == 1e30f) {
            core::logError("the studio found no Bonfire01 in %s; standing at the middle",
                           benchWorld.c_str());
            studioFire[0] = middle + 1.6f;
            studioFire[2] = -middle - 1.6f;
        }
        // The stage's arrangement: the fire 1.6 m to the subject's +x and -z, which is the
        // right of MU's picture.
        studioStand[0] = studioFire[0] - 1.6f;
        studioStand[2] = studioFire[2] + 1.6f;
        studioStand[1] = world.ground().heightAt(studioStand[0], studioStand[2]);
        studioFire[1] = world.ground().heightAt(studioFire[0], studioFire[2]) + 0.6f;
    }
    // The stage is the browser with the world's own lamps, fire and objects stood round the
    // subject; the bare browser is the subject on its plot and nothing else.
    if (browseBench &&
        !(studio ? bench.openStudio(MU2_ASSET_DIR, benchWorld, studioStand, studioFire, textures)
          : args.stage ? bench.openStage(MU2_ASSET_DIR, benchWorld, textures)
                       : bench.openBrowser(MU2_ASSET_DIR, benchWorld, textures))) {
        core::logError("the bench did not open");
        renderer.shutdown();
        textures.shutdown();
        window.close();
        core::logClose();
        return 1;
    }
    // Where the browser opens, for a run with nobody at the keyboard. Neither is fatal: a
    // category or a name that is not there has said so, and the viewer is still a viewer.
    if (browseBench && !args.category.empty()) bench.openCategory(args.category, textures);
    if (browseBench && !args.pick.empty()) bench.pick(args.pick, textures);
    if (browseBench) core::logf("browser: %s", bench.browseLine().c_str());
    // The stage's lamps, once, as a world's are: the renderer lays its light grid here.
    if (bench.hasStage() && args.lampsOn) bench.stageLamps().light(renderer);
    if (!inWorld && !figureBench && !browseBench &&
        !bench.open(MU2_ASSET_DIR, benchWorld, modelPath, textures)) {
        core::logError("the bench did not open");
        renderer.shutdown();
        textures.shutdown();
        window.close();
        core::logClose();
        return 1;
    }

    // The transparent pass's probe. The sheet comes out of the cooked showing table rather
    // than off a path, so what is measured is exactly what the game will draw: a BC7 .ktx
    // with its mip chain, not a PNG decoded at load.
    //
    // A probe that cannot find its sheet draws nothing and says so once. It is never fatal:
    // this is a measurement aid, and a run that refuses to start because an effect is
    // missing would be a worse tool than one that says the effects are missing.
    int probeSprites = args.effects;
    bgfx::TextureHandle probeSheet = BGFX_INVALID_HANDLE;
    if (probeSprites > 0) {
        content::Showing showing;
        std::string error;
        const std::string showingPath =
            std::string(MU2_ASSET_DIR) + "/cooked/showing/showing.mus";
        if (!content::loadShowing(showingPath, showing, error)) {
            core::logError("the showing table did not open: %s (tools/cook.py --only showing)",
                           error.c_str());
            probeSprites = 0;
        } else {
            const content::EffectSheet* chosen =
                args.effectSheet.empty() ? (showing.effects.empty() ? nullptr
                                                                    : &showing.effects.front())
                                         : showing.effect(args.effectSheet);
            if (chosen == nullptr) {
                core::logError("no cooked effect named '%s'; %zu are in the table",
                               args.effectSheet.c_str(), showing.effects.size());
                probeSprites = 0;
            } else {
                probeSheet = textures.load(std::string(MU2_ASSET_DIR) + "/" + chosen->path,
                                           content::TextureRole::Albedo);
                core::logf("effects probe: %d sprites of '%s' at %.2f m, %zu sheets and "
                           "%zu sound events cooked",
                           probeSprites, chosen->name.c_str(), args.effectSize,
                           showing.effects.size(), showing.events.size());
            }
        }
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

    if (args.shadowView || args.shadowNoise >= 0) {
        renderer.setShadowDebug(args.shadowView ? 1 : 0, args.shadowNoise);
    }
    // The pan test's grid: fixed ground points around where the camera starts, and a csv
    // row a frame of the pixel each lands on. tools/pan.py samples the shots there.
    FILE* shadowPoints = nullptr;
    std::vector<float> pointGrid;  // x, y, z a point
    if (!args.shadowPoints.empty() && inWorld) {
        shadowPoints = std::fopen(args.shadowPoints.c_str(), "w");
        if (!shadowPoints) {
            core::logError("could not open --shadow-points %s", args.shadowPoints.c_str());
        } else {
            constexpr int kSide = 60;
            constexpr float kStep = 0.3f;
            const gfx::Camera& cam = world.camera();
            for (int j = 0; j < kSide; ++j) {
                for (int i = 0; i < kSide; ++i) {
                    const float x = cam.target[0] + (float(i) - kSide * 0.5f) * kStep;
                    const float z = cam.target[2] + (float(j) - kSide * 0.5f) * kStep;
                    pointGrid.insert(pointGrid.end(), {x, world.ground().heightAt(x, z), z});
                }
            }
            std::fprintf(shadowPoints, "# %zu points; frame then x,y pixel pairs, -1 when off "
                                       "screen\n", pointGrid.size() / 3);
        }
    }
    // The shadow probe's csv. docs/shadow-probe.md reads it.
    FILE* shadowLog = nullptr;
    if (!args.shadowLog.empty()) {
        shadowLog = std::fopen(args.shadowLog.c_str(), "w");
        if (shadowLog) {
            std::fprintf(shadowLog,
                         "frame,dt_ms,focus_x,focus_z,texel_mm,texel_x,texel_y,phase_x,phase_y,"
                         "depth_quanta,depth_phase,hero_x,hero_z,camera_lag_mm\n");
        } else {
            core::logError("could not open --shadow-log %s", args.shadowLog.c_str());
        }
    }

    // The browser's keys and its list, shared by the viewer on its plot and the viewer in a
    // world: the same list, the same keys, whichever ground is under the subject.
    auto steer = [&]() {
        // The browser's steering, before the frame it steers. Left and right walk one,
        // down and up walk ten, and the step is an edge rather than a state: at 400 fps a
        // key read as held walks the whole list on one tap.
        if (bench.browsing()) {
            int by = 0;
            if (window.stepped(gfx::Window::Step::Previous)) by -= 1;
            if (window.stepped(gfx::Window::Step::Next)) by += 1;
            if (window.stepped(gfx::Window::Step::PreviousTen)) by -= 10;
            if (window.stepped(gfx::Window::Step::NextTen)) by += 10;
            if (by != 0) {
                bench.step(by, textures);
                core::logf("browser: %s", bench.browseLine().c_str());
            }
            // Tab walks the categories, skipping any that is empty: a world cooked
            // without figures must not have two tab presses that appear to do nothing.
            if (window.stepped(gfx::Window::Step::Category)) {
                const size_t count = bench.categoryCount();
                for (size_t i = 1; i <= count; ++i) {
                    const size_t next = (bench.categoryIndex() + i) % count;
                    if (bench.setCategory(next, textures)) break;
                }
                core::logf("browser: %s", bench.browseLine().c_str());
            }
            if (window.stepped(gfx::Window::Step::PreviousClip)) bench.stepClip(-1);
            if (window.stepped(gfx::Window::Step::NextClip)) bench.stepClip(1);
        }
        // T walks the times of day. It is the viewer's key; the game has no day and night yet.
        if (browseBench && window.stepped(gfx::Window::Step::Time)) {
            daytime = (daytime + 1) % 3;
            applyTime();
        }
    };
    auto drawList = [&]() {
        if (bench.browsing() && overlay.ready() && args.list) {
            float px = 0.0f, py = 0.0f;
            window.pointer(&px, &py);
            const ListHit hit = drawBrowserList(overlay, bench, window.width(),
                                                window.height(), px, py);
            // Under the panel rather than in it: which clip is running, where its clock
            // stands and how long it is. A still cannot show that a clip is playing, and
            // a monster frozen on frame one looks exactly like one standing still.
            if (bench.hasFigure()) {
                overlay.text(hit.x + 4.0f, hit.y + hit.h + 10.0f, 2.4f, 0xFFc8c8c8u,
                             bench.clipLine());
            }
            overlay.text(hit.x + 4.0f, hit.y + hit.h + (bench.hasFigure() ? 34.0f : 10.0f), 2.4f,
                         0xFFc8c8c8u, std::string(kTimes[daytime]) + "    T changes the time of day");
            overlay.submit(gfx::ViewHud);
            // The list eats the pointer while it is over it, so a click on a name does
            // not also drag the camera and the wheel scrolls the list rather than zooming.
            if (hit.over) {
                if (window.clicked(0) && hit.tab >= 0) {
                    bench.setCategory(size_t(hit.tab), textures);
                    core::logf("browser: %s", bench.browseLine().c_str());
                }
                if (window.clicked(0) && hit.hovered >= 0) {
                    bench.step(int(hit.hovered - (long long)bench.browseIndex()), textures);
                    core::logf("browser: %s", bench.browseLine().c_str());
                }
                const float wheel = window.scroll();
                if (wheel != 0.0f) {
                    bench.step(wheel > 0.0f ? -1 : 1, textures);
                }
            } else {
                if (window.held(0)) {
                    float dx = 0.0f, dy = 0.0f;
                    window.pointerDelta(&dx, &dy);
                    bench.orbit(dx, dy);
                }
                bench.zoom(window.scroll());
            }
        }
    };

    // The world arrives at once: the first two frames are black -- the first compiles every
    // pipeline the frame uses and takes about a tenth of a second, and on black, straight
    // after the spinner's black, that stall cannot be seen -- and then the black lifts in a
    // tenth of a second, which is only enough not to be a cut. The character is what comes in
    // slowly (Play::appear).
    float entranceSeconds = 0.0f;
    while (!quitEarly && window.pump() && !window.escapePressed()) {
        renderer.resize(window.width(), window.height());

        // Four times a second, counted in milliseconds rather than frames: the point is to
        // tune with the window open, and at 470 fps a count of frames was stat'ing the file
        // a hundred times a second.
        if (sinceSheetCheck >= 250.0) {
            sinceSheetCheck = 0.0;
            // The overlay is watched as well as the sheet, so dusk can be tuned live too.
            if (lighting.reloadIfChanged(sheetPath) ||
                (daytime > 0 && core::fileModified(overlayPath(daytime)) != overlayStamp)) {
                applyTime();
            }
        }

        // The frame's poses start empty: a row is taken by whoever is posed this frame, and
        // a row left over from the last one belongs to nobody.
        renderer.resetPalettes();

        // The transparent pass's list starts empty for the same reason, and is filled below
        // from whatever the probe asks for. Sprint 6 has no effects with lives of their own
        // yet; when it does, this is where the game's own pool will write into it.
        renderer.effects().begin();
        if (probeSprites > 0 && bgfx::isValid(probeSheet)) {
            const gfx::Camera& shot = inWorld ? world.camera() : bench.camera();
            // Strung along the line from the camera to what it looks at, so that the sprites
            // are in front of the subject rather than inside it, and so that the sort has
            // something to sort: the near one must come out over the far one.
            for (int i = 0; i < probeSprites; ++i) {
                const float along = 0.35f + 0.5f * float(i) / float(probeSprites);
                gfx::Sprite sprite;
                for (int c = 0; c < 3; ++c) {
                    sprite.position[c] =
                        shot.position[c] + (shot.target[c] - shot.position[c]) * along;
                }
                sprite.halfWidth = sprite.halfHeight = args.effectSize;
                // A turn per sprite, which is what MU does so that four blows on one spider
                // do not stamp the same picture four times.
                sprite.spin = float(i) * 0.7f;
                sprite.colour[3] = 0.6f;
                sprite.sheet = probeSheet;
                // The far half alpha and the near half additive, rather than alternating
                // every sprite. Alternating is the batcher's worst case -- the sort puts the
                // two modes in each other's way and every sprite becomes its own draw -- and
                // it is not what a fight looks like: a burst of particles shares one sheet
                // and one mode. Two blocks exercise both modes AND show the runs batching,
                // which is what the draw count in the log is there to report.
                sprite.blend = (i * 2 >= probeSprites) ? gfx::Blend::Additive : gfx::Blend::Alpha;
                renderer.effects().add(sprite);
            }
        }

        if (inWorld) {
            const gfx::Camera& eye = world.camera();
            // The pointer and what it is over, before the sim is stepped: a click is taken at
            // the start of the next tick and walked on that same tick (Realm::accept).
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

                    // Aimed at the nearest living monster when there is one, and at the spot
                    // above when there is not.
                    //
                    // The six spots wander: the camera follows the hero, so the middle of the
                    // screen is roughly his own tile and a scripted click mostly walks a step
                    // and comes back. That is fine for proving a walk and useless for proving
                    // a fight -- over 900 frames in a spider field it produced blows landing
                    // ON the hero and not one landing on a monster, which is the half of
                    // sprint 6 worth looking at. The click still goes through the same
                    // unprojection, the same tile and the same request; only where it points
                    // is chosen.
                    const sim::Realm& realm = world.played().realm();
                    const sim::Body& hero = realm.hero();
                    const sim::Body* nearest = nullptr;
                    float best = 1e9f;
                    for (const sim::Body& body : realm.bodies()) {
                        if (body.player || !body.alive()) continue;
                        const float dx = body.x - hero.x, dy = body.y - hero.y;
                        const float away = dx * dx + dy * dy;
                        if (away < best) {
                            best = away;
                            nearest = &body;
                        }
                    }
                    // With --loot, what lies on the ground comes first: the scripted hand
                    // picks up before it fights again, which is the half of sprint 7 a run
                    // with nobody at the mouse can otherwise never show.
                    float aimX = nearest ? nearest->x : 0.0f, aimY = nearest ? nearest->y : 0.0f;
                    if (args.loot) {
                        float closest = 12.0f * 12.0f;
                        for (const sim::Lying& one : realm.lying()) {
                            const float dx = float(one.column) - hero.x;
                            const float dy = float(one.row) - hero.y;
                            if (dx * dx + dy * dy < closest) {
                                closest = dx * dx + dy * dy;
                                aimX = float(one.column);
                                aimY = float(one.row);
                                best = closest;
                                nearest = &hero;  // anything non-null: there is an aim
                            }
                        }
                    }
                    // Only when it is close enough to walk to and fight in a few ticks;
                    // anything further and the run is a march rather than a fight.
                    if (nearest != nullptr && best < 12.0f * 12.0f) {
                        const content::Ground& land = world.ground();
                        const float metresPerTile = land.metresPerTile();
                        const float wx = (aimX + 0.5f) * metresPerTile;
                        const float wz = -(aimY + 0.5f) * metresPerTile;
                        const float wy = land.heightAt(wx, wz);
                        float clip[4] = {0, 0, 0, 0};
                        const float world4[4] = {wx, wy, wz, 1.0f};
                        float viewProjNow[16];
                        bx::mtxMul(viewProjNow, view, proj);
                        bx::vec4MulMtx(clip, world4, viewProjNow);
                        if (clip[3] > 0.0f) {
                            // Clip space to pixels. y is flipped because clip space runs up
                            // the screen and the pointer runs down it, which is the same
                            // flip vs_overlay.sc makes for the same reason.
                            const float ndcX = clip[0] / clip[3];
                            const float ndcY = clip[1] / clip[3];
                            pointerX = (ndcX * 0.5f + 0.5f) * float(window.width());
                            pointerY = (0.5f - ndcY * 0.5f) * float(window.height());
                        }
                    }
                }
                // The windows first: a click that lands on one is the interface's, and the
                // world only hears the clicks that land on none. A scripted click is the
                // world's by construction -- it is aimed at a monster.
                if (desk.ready()) {
                    float viewProjNow[16];
                    bx::mtxMul(viewProjNow, view, proj);
                    desk.setView(viewProjNow);
                    for (const auto& [f, k] : args.uiKeys) {
                        if (frame == f) desk.scriptKey(k - 1);
                    }
                    const float w = float(window.width()), h = float(window.height());
                    for (const core::Args::UiClick& c : args.uiClicks) {
                        const bool drag = c.x2 != c.x || c.y2 != c.y;
                        const int last = c.frame + (drag ? 3 : 1);
                        if (frame < c.frame || frame > last) continue;
                        // Pressed at the first point, carried to the second, let go there.
                        const bool at = frame == c.frame;
                        desk.script((at ? c.x : c.x2) * w, (at ? c.y : c.y2) * h, at,
                                    frame == last, c.right);
                    }
                    desk.update(float(deltaSeconds), window, world.played());
                }
                const bool windowed = desk.ready() && desk.takesPointer();
                world.played().point(world.camera(), view, proj, pointerX, pointerY,
                                     window.width(), window.height());
                if ((window.clicked(0) && !windowed) || clickNow) world.played().leftClick();
                if (window.clicked(1) && !windowed) world.played().rightClick();
                if (!windowed) world.zoom(window.scroll());
                world.played().update(deltaSeconds);
            }
            // And only THEN the camera, onto where the character is drawn this frame. Placed
            // before the step, it followed where he stood a frame ago: the town and every
            // shadow in it slid under him by his step length times the frame time, which
            // changes every frame -- 16 mm median and 79 mm worst over a walk, on a shadow
            // texel of 29 mm. docs/shadow-probe.md.
            world.update(elapsed, args.still);
            // The lamps flicker, the fires burn, and the glows' levels go into the town before
            // it is gathered, since each rides in its instance. docs/sprints/08a-the-lamps.md.
            if (args.lampsOn) {
                world.lamps().update(float(deltaSeconds), world.town(), renderer, eye.target);
                // What the day gives an unlit puff of smoke: the ambient and the sun on a flat
                // surface, over what the default sheet's noon gives it.
                world.lamps().gather(renderer.effects(), eye.target, daylightOf(lighting));
            }
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
                    renderer.cameraMatrices(eye, view, proj);
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
            if (world.played().isOpen()) {
                float view[16];
                float proj[16];
                renderer.cameraMatrices(world.camera(), view, proj);
                float viewProj[16];
                bx::mtxMul(viewProj, view, proj);
                world.played().gather(renderer, viewProj, townDrawables,
                                      casters ? &townCasters : nullptr);
                // And what the blows have thrown, into the transparent pass. The camera's
                // own horizontal comes out of the view matrix's first column, which is the
                // same basis the pass billboards on -- a number's digits are laid along it,
                // so a second copy of that vector taken from anywhere else would tilt the
                // number away from the sprites it sits among.
                const float right[3] = {view[0], view[4], view[8]};
                world.played().showing().gather(renderer.effects(), right);
                world.played().gatherMarker(renderer.effects());
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
            // Along both of the ground's axes, and not a whole texel's worth of either in one
            // step, so the split crosses texel boundaries in x and in y at different frames.
            if (args.shadowSlideMm != 0.0f) {
                const float metres = float(frame) * args.shadowSlideMm * 0.001f;
                const float slide[3] = {metres, 0.0f, metres * 0.618f};
                renderer.slideSplit(slide);
            }
            renderer.draw(eye, lighting, townDrawables, &world.ground(), casters);
            if (desk.ready()) desk.submit(gfx::ViewHud, window.width(), window.height());
            if (entrance && curtain.ready()) {
                if (frame >= 2) entranceSeconds += float(deltaSeconds);
                const float t = std::min(1.0f, entranceSeconds / 0.1f);
                const float black = 1.0f - t * t * (3.0f - 2.0f * t);
                if (black > 0.0f) {
                    // After the HUD in the same view: the HUD draws in submission order, so
                    // this lies over it and the whole picture comes up together.
                    curtain.begin(window.width(), window.height());
                    curtain.panel(0.0f, 0.0f, float(window.width()), float(window.height()),
                                  uint32_t(black * 255.0f) << 24);
                    curtain.submit(gfx::ViewHud);
                }
            }
            if (shadowPoints) {
                float view[16], proj[16], viewProj[16];
                renderer.cameraMatrices(world.camera(), view, proj);
                bx::mtxMul(viewProj, view, proj);
                const float w = float(window.width()), h = float(window.height());
                std::fprintf(shadowPoints, "%d", frame);
                for (size_t p = 0; p < pointGrid.size(); p += 3) {
                    const float point[4] = {pointGrid[p], pointGrid[p + 1], pointGrid[p + 2], 1.0f};
                    float clip[4];
                    bx::vec4MulMtx(clip, point, viewProj);
                    float px = -1.0f, py = -1.0f;
                    if (clip[3] > 0.0f) {
                        const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
                        if (nx > -1.0f && nx < 1.0f && ny > -1.0f && ny < 1.0f) {
                            px = (nx * 0.5f + 0.5f) * w;
                            py = (0.5f - ny * 0.5f) * h;
                        }
                    }
                    std::fprintf(shadowPoints, ",%.3f,%.3f", px, py);
                }
                std::fprintf(shadowPoints, "\n");
            }
            if (shadowLog) {
                const gfx::Renderer::SplitRecord& split = renderer.lastSplit();
                const gfx::Camera& cam = world.camera();
                auto phase = [](float v) { return v - std::floor(v); };
                float heroX = cam.target[0], heroZ = cam.target[2];
                const bool played = world.characterAt(&heroX, &heroZ);
                const float lagMm = played ? 1000.0f * std::hypot(heroX - cam.target[0],
                                                                  heroZ - cam.target[2])
                                           : 0.0f;
                std::fprintf(shadowLog,
                             "%d,%.3f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%.4f,%.3f,%.4f,%.4f,%.4f,"
                             "%.2f\n",
                             frame, deltaSeconds * 1000.0, cam.target[0], cam.target[2],
                             split.texel * 1000.0f, split.texelX, split.texelY,
                             phase(split.texelX), phase(split.texelY), split.depthQuanta,
                             phase(split.depthQuanta), heroX, heroZ, lagMm);
            }
        } else if (studio) {
            steer();
            // The sweep: a block of --shot frames an angle, --turns angles a time of day,
            // noon then dusk then night. The shot at the end of a block is taken at the frame
            // the next begins on, so a block is counted from the frame after it.
            if (args.turns > 0 && args.shotEvery > 0) {
                const int block = frame > 0 ? (frame - 1) / args.shotEvery : 0;
                bench.setTurn(360.0f * float(block % args.turns) / float(args.turns));
                const int time = (block / args.turns) % 3;
                if (time != daytime) {
                    daytime = time;
                    applyTime();
                }
            }
            bench.update(elapsed, deltaSeconds, !args.still);
            if (args.lampsOn) {
                world.lamps().update(float(deltaSeconds), world.town(), renderer,
                                     bench.camera().target);
                world.lamps().gather(renderer.effects(), bench.camera().target,
                                     daylightOf(lighting));
            }
            townDrawables.clear();
            if (world.town().isOpen()) world.town().gatherAll(townDrawables);
            const std::vector<gfx::Drawable>& subject = bench.gather(renderer);
            townDrawables.insert(townDrawables.end(), subject.begin(), subject.end());
            renderer.draw(bench.camera(), lighting, townDrawables, &world.ground());
            drawList();
        } else {
            steer();
            bench.update(elapsed, deltaSeconds, !args.still);
            // The stage's lamps, the way a world's are run: the flicker, the glows written into
            // its town, and the flames near the camera into the transparent pass.
            if (bench.hasStage() && args.lampsOn) {
                bench.stageLamps().update(float(deltaSeconds), bench.stageTown(), renderer,
                                          bench.camera().target);
                bench.stageLamps().gather(renderer.effects(), bench.camera().target,
                                          daylightOf(lighting));
            }
            renderer.draw(bench.camera(), lighting, bench.gather(renderer), bench.ground());
            drawList();
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
        if (args.fixedDtMs > 0.0f) deltaSeconds = args.fixedDtMs * 0.001;
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
            // The transparent pass's pool, for the same reason the culling counts are here:
            // "no allocation per frame in the pools" is sprint 6's proving sentence, and a
            // claim nothing reports is not proved. The high-water mark against the reserve
            // is the evidence -- it cannot exceed it, because add() refuses instead of
            // growing -- and a refusal count above zero means the reserve is too small and
            // the picture is already missing something.
            if (renderer.effects().highWater() > 0 || renderer.effects().refused() > 0) {
                core::logf("  effects: %u sprites in %u draws; high water %u of %u reserved, "
                           "%u refused",
                           renderer.effects().lastSpriteCount(),
                           renderer.effects().lastDrawCount(), renderer.effects().highWater(),
                           renderer.effects().capacity(), renderer.effects().refused());
            }
            if (desk.ready()) core::logf("%s", desk.line().c_str());
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
            if (bench.browsing()) core::logf("  %s", bench.browseLine().c_str());
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

    if (shadowLog) std::fclose(shadowLog);
    if (shadowPoints) std::fclose(shadowPoints);
    const bool withinBudget = stats.finish(args.budget);

    bench.shutdown();
    desk.shutdown();
    overlay.shutdown();
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
