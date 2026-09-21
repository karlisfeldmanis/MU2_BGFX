// MU2 on bgfx. Sprint 1: the whole six-view frame, with the model bench in front of it.
// See PLAN.md and docs/sprints/.
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
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
            // What a blow looks like, opened second because the sheets need a device and
            // Play is handed an asset directory and no Textures. Not fatal: a fight with no
            // blood in it is still a fight, and open() has already said why in the log.
            world.played().showing().open(MU2_ASSET_DIR, textures);
        }
        // The lamps' static set, once: the renderer lays its light grid over the ground here.
        if (args.lampsOn) world.lamps().light(renderer);
        // Placed once before the first frame: the loop answers the pointer against the camera
        // already on screen, and on frame zero there has to be one.
        world.update(0.0, args.still);
    }

    // The windows, over a played world and nowhere else: a bench has nobody to show them for.
    // Not fatal: a game with no HUD is still a game, and the log says why.
    game::Desk desk;
    if (inWorld && world.played().isOpen() && args.windows != "off" &&
        !desk.open(MU2_SHADER_DIR, MU2_ASSET_DIR, &textures)) {
        core::logError("the windows did not open; playing without a HUD");
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
    if (browseBench && !bench.openBrowser(MU2_ASSET_DIR, benchWorld, textures)) {
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
                world.lamps().update(float(deltaSeconds), world.town(), renderer,
                                    world.camera().target);
                // What the day gives an unlit puff of smoke: the ambient and the sun on a flat
                // surface, over what the default sheet's noon gives it.
                const float sky = lighting.ambientStrength +
                                  lighting.sunStrength *
                                      std::sin(lighting.elevation * 3.14159265f / 180.0f) /
                                      3.14159265f;
                const float daylight = std::clamp(sky / 1.45f, 0.08f, 1.0f);
                world.lamps().gather(renderer.effects(), world.camera().target, daylight);
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
            renderer.draw(world.camera(), lighting, townDrawables, &world.ground(), casters);
            if (desk.ready()) desk.submit(gfx::ViewHud, window.width(), window.height());
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
        } else {
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
            bench.update(elapsed, deltaSeconds, !args.still);
            renderer.draw(bench.camera(), lighting, bench.gather(renderer), bench.ground());
            if (bench.browsing() && overlay.ready()) {
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
