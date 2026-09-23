#include "app/modes/play_mode.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/preloader.h"
#include "core/log.h"
#include "gfx/views.h"

namespace mu::app {

void PlayMode::readSave(Context& ctx) {
    core::Args& args = ctx.args;
    if (!args.play) return;
    // An arena has no save of its own and must never touch the player's: it is a level-80
    // hero standing in a field of one breed, and writing that over the character somebody
    // is playing would be the worst kind of helpful. A named `--save` is still obeyed,
    // because then the caller asked for a file by name.
    savePath_ = !args.savePath.empty()                     ? args.savePath
                : (args.frames == 0 && args.arena.empty()) ? game::defaultSavePath()
                                                           : std::string();
    if (!savePath_.empty() && !args.fresh && game::loadSave(savePath_, saved_)) {
        if (saved_.world == args.world) {
            resumed_ = true;
            args.kin = int(saved_.hero.kin);
            args.level = saved_.hero.level;
            args.weapon.clear();
            args.shield.clear();
        } else {
            core::logError("save: the hero is in %s and this run is %s; starting new here",
                           saved_.world.c_str(), args.world.c_str());
        }
    }
    if (!savePath_.empty()) {
        core::logf("save: %s %s", resumed_ ? "resuming from" : "a new character, saving to",
                   savePath_.c_str());
    }
}

void PlayMode::keep(Context& ctx) {
    if (savePath_.empty() || !world_.played().isOpen()) return;
    game::Saved now;
    now.world = ctx.args.world;
    now.hero = world_.played().record();
    for (int key = 0; key < 5; ++key) now.quick[key] = desk_.quick(key);
    for (int key = 0; key < 5; ++key) now.bar[key] = desk_.bound(key);
    now.zoom = world_.zoomDistance();
    game::writeSave(savePath_, *world_.played().realm().tables(), now);
}

bool PlayMode::open(Context& ctx) {
    core::Args& args = ctx.args;
    const std::string& assets = ctx.paths.assets;

    readSave(ctx);

    // The game's own entrance, only when somebody is playing.
    entrance_ = args.play && (args.frames == 0 || args.entrance);

    if (args.atSet) world_.setFocusTile(args.atColumn, args.atRow);
    else if (!args.arena.empty()) {
        // The arena's own patch, unless the caller named a tile. Why that one is in
        // Play::Arena beside the constants; the short of it is that it is the flattest,
        // emptiest, non-safe square on the only cooked world.
        world_.setFocusTile(float(game::Play::Arena::kColumn), float(game::Play::Arena::kRow));
    } else if (resumed_) {
        world_.setFocusTile(float(saved_.hero.column), float(saved_.hero.row));
    }

    // Everything below runs on the preloader's worker while this thread draws the spinner.
    // See app/preloader.h for why that is legal.
    const bool up = Preloader::run(ctx, [&]() {
        // No crowd when the realm is going to be raised: the crowd stands monsters where
        // it chooses and the sim stands them where they are, and raising both means
        // loading, posing and then throwing away 45 figures a run.
        const bool ok = world_.open(assets, args.world, ctx.textures,
                                    args.play ? 0 : args.crowd, args.figuresOn);
        // And the realm behind it, when there is somebody playing. A world that cannot
        // raise one -- no cooked tables yet -- says so and is still a world to look at.
        if (ok && args.play) {
            // Before the realm is raised, because all the arena is is the nest table the
            // realm is about to be handed. See Play::Arena.
            if (!args.arena.empty()) {
                game::Play::Arena arena;
                arena.breed = args.arena;
                arena.count = args.arenaCount;
                world_.played().setArena(arena);
            }
            world_.play(assets, args.world, args.seed, args.kin, args.level, args.weapon,
                        args.shield);
        }
        // And everything that hangs off a realm, only when there IS one. This used to run
        // on the answer to `args.play` alone, which is what was ASKED for and not what
        // happened: a play that opened its tables and then failed -- no cooked realm, or a
        // breed `--arena` could not find -- left a Play with no bodies in it, and
        // `openSound` reads `bodies_[0]` for the hero's own death cry. That is a read off
        // the front of an empty vector, and it is a crash and not a missing sound.
        if (world_.played().isOpen()) {
            if (resumed_) {
                game::resolveSave(*world_.played().realm().tables(), saved_);
                world_.played().restore(saved_.hero);
                // And the camera where the wheel left it last time. A save with no zoom in
                // it -- an older file, or a run that never touched the wheel -- leaves the
                // world's own default standing.
                world_.setZoomDistance(saved_.zoom);
            }
            // What a blow looks like and where a click sent him. Not fatal: open() has
            // said why in the log.
            world_.played().showing().open(assets, ctx.textures);
            world_.played().marker().open(assets, ctx.textures);
            world_.played().aura().open(assets, ctx.textures);
            if (world_.played().showing().isOpen()) {
                world_.played().breath().open(assets, ctx.textures,
                                              world_.played().showing().table(),
                                              &world_.ground());
                world_.played().meteor().open(assets, ctx.textures,
                                              world_.played().showing().table(),
                                              &world_.ground());
                world_.played().bones().open(assets, ctx.textures, &world_.ground());
                world_.played().streak().open(assets, ctx.textures,
                                              world_.played().showing().table());
            }
            world_.played().openSound(assets, args.mute);
            // Not fatal either: a game with no HUD is still a game.
            if (args.windows != "off" && !desk_.open(ctx.paths.shaders, assets, &ctx.textures)) {
                core::logError("the windows did not open; playing without a HUD");
            }
            if (resumed_) {
                for (int key = 0; key < 5; ++key) desk_.setQuick(key, saved_.quick[key]);
                desk_.restoreBar(saved_.bar, 5);
            }
        }
        // An arena whose realm did not rise is a failed run and not a world to look at.
        // Everywhere else a realm that cannot be raised leaves a still world standing,
        // which is the right answer for a cook that has not been run; here the realm IS
        // what was asked for, and `--arena Wyvren` drawing 300 frames of empty grass and
        // exiting 0 is the same fault `--at 9999,9999` was fixed for.
        const bool arenaUp = args.arena.empty() || world_.played().isOpen();
        return ok && arenaUp;
    }, &quitEarly_);

    if (!up) {
        core::logError("the world did not open");
        // The world may have failed half-open -- `--at` off the map is refused after the
        // ground's buffers are already made -- and a failure path that skips the world's
        // own shutdown leaks a vertex and an index buffer past bgfx's own shutdown.
        world_.shutdown();
        return false;
    }

    // The lamps' static set, once: the renderer lays its light grid over the ground here.
    if (args.lampsOn) world_.lamps().light(ctx.renderer);
    // Placed once before the first frame: the loop answers the pointer against the camera
    // already on screen, and on frame zero there has to be one.
    world_.update(0.0, args.still);
    // He comes in once the frame has faded most of the way up. Long enough a wait to
    // outlast the first frame, which compiles every pipeline and takes a tenth of a second.
    if (entrance_ && world_.played().isOpen()) world_.played().appear(0.3f);
    // And the map's name with him, MU's ShowMapName on entering a world.
    if (entrance_ && world_.played().isOpen() && desk_.ready()) desk_.arrive(args.world, 0.3f);

    runScript(ctx);

    // And the played world, for the tile over the character's head: the numbers `--at` takes,
    // so a screenshot of something to fix says where to go back to.
    if (world_.played().isOpen() && !ctx.overlay.init(ctx.paths.shaders)) {
        core::logError("the world opened without the tile over the character's head");
    }

    openProbes(ctx);
    keptAt_ = bx::getHPCounter();
    return true;
}

void PlayMode::runScript(Context& ctx) {
    core::Args& args = ctx.args;
    if (world_.played().isOpen() && !args.give.empty()) {
        size_t from = 0;
        while (from <= args.give.size()) {
            const size_t comma = args.give.find(',', from);
            const std::string one = args.give.substr(from, comma - from);
            const size_t colon = one.find(':');
            if (!one.empty()) {
                world_.played().give(one.substr(0, colon),
                                     colon == std::string::npos
                                         ? 1
                                         : std::atoi(one.c_str() + colon + 1));
            }
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
    }
    if (world_.played().isOpen()) {
        if (args.zen > 0) world_.played().earn(args.zen);
        if (!args.talk.empty()) world_.played().talkTo(args.talk);
    }
    if (args.windows.find("inventory") != std::string::npos) desk_.setInventoryOpen(true);
    if (args.windows.find("character") != std::string::npos) desk_.setCharacterOpen(true);
}

void PlayMode::openProbes(Context& ctx) {
    core::Args& args = ctx.args;
    if (args.shadowView || args.shadowNoise >= 0) {
        ctx.renderer.setShadowDebug(args.shadowView ? 1 : 0, args.shadowNoise);
    }
    // The pan test's grid: fixed ground points around where the camera starts, and a csv
    // row a frame of the pixel each lands on. tools/pan.py samples the shots there.
    if (!args.shadowPoints.empty()) {
        shadowPoints_ = std::fopen(args.shadowPoints.c_str(), "w");
        if (!shadowPoints_) {
            core::logError("could not open --shadow-points %s", args.shadowPoints.c_str());
        } else {
            constexpr int kSide = 60;
            constexpr float kStep = 0.3f;
            const gfx::Camera& cam = world_.camera();
            for (int j = 0; j < kSide; ++j) {
                for (int i = 0; i < kSide; ++i) {
                    const float x = cam.target[0] + (float(i) - kSide * 0.5f) * kStep;
                    const float z = cam.target[2] + (float(j) - kSide * 0.5f) * kStep;
                    pointGrid_.insert(pointGrid_.end(), {x, world_.ground().heightAt(x, z), z});
                }
            }
            std::fprintf(shadowPoints_, "# %zu points; frame then x,y pixel pairs, -1 when off "
                                        "screen\n", pointGrid_.size() / 3);
        }
    }
    // The shadow probe's csv. docs/shadow-probe.md reads it.
    if (!args.shadowLog.empty()) {
        shadowLog_ = std::fopen(args.shadowLog.c_str(), "w");
        if (shadowLog_) {
            std::fprintf(shadowLog_,
                         "frame,dt_ms,focus_x,focus_z,texel_mm,texel_x,texel_y,phase_x,phase_y,"
                         "depth_quanta,depth_phase,hero_x,hero_z,camera_lag_mm\n");
        } else {
            core::logError("could not open --shadow-log %s", args.shadowLog.c_str());
        }
    }
}

void PlayMode::arenaHand() {
    // Fight the nearest of them, and the next one when that one is down. It raises the same
    // Attack request a click raises (Play::fight) and decides nothing else -- the walk to it,
    // the reach, the roll and the damage are all the realm's, which is the whole point of an
    // arena over a bench.
    //
    // Only when he has nobody: the request is not re-raised every frame. Re-asking is not free
    // -- Realm::accept takes the pending order at the top of a tick and an Attack order
    // re-taken resets the chase's re-plan -- and a hand that only speaks when the hero is idle
    // is also the one a person would be.
    const sim::Realm& realm = world_.played().realm();
    const sim::Body& hero = realm.hero();
    const sim::Body* held = arenaTarget_ != 0 ? realm.find(arenaTarget_) : nullptr;
    if (hero.alive() && (!held || !held->alive())) {
        const sim::Body* nearest = nullptr;
        float best = 1e9f;
        for (const sim::Body& body : realm.bodies()) {
            if (body.player || !body.alive()) continue;
            const float dx = body.x - hero.x, dy = body.y - hero.y;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                nearest = &body;
            }
        }
        if (nearest) {
            arenaTarget_ = nearest->id;
            world_.played().fight(arenaTarget_);
        }
    }
}

bool PlayMode::scriptedPointer(Context& ctx, const Frame& at, const float* view,
                               const float* proj, float* pointerX, float* pointerY) {
    // The scripted pointer, for a run with nobody at the mouse. It goes through the same
    // unprojection, the same tile, the same request: what it skips is the hand and nothing else.
    core::Args& args = ctx.args;
    if (args.demoClicks <= 0 || at.index % args.demoClicks != 0) return false;
    static const float kSpots[6][2] = {{0.50f, 0.50f}, {0.62f, 0.38f},
                                       {0.38f, 0.60f}, {0.70f, 0.55f},
                                       {0.44f, 0.34f}, {0.56f, 0.66f}};
    const int spot = (at.index / args.demoClicks) % 6;
    *pointerX = kSpots[spot][0] * float(ctx.window.width());
    *pointerY = kSpots[spot][1] * float(ctx.window.height());

    // Aimed at the nearest living monster when there is one, and at the spot above when
    // there is not.
    //
    // The six spots wander: the camera follows the hero, so the middle of the screen is
    // roughly his own tile and a scripted click mostly walks a step and comes back. That is
    // fine for proving a walk and useless for proving a fight -- over 900 frames in a spider
    // field it produced blows landing ON the hero and not one landing on a monster, which is
    // the half of sprint 6 worth looking at. The click still goes through the same
    // unprojection, the same tile and the same request; only where it points is chosen.
    const sim::Realm& realm = world_.played().realm();
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
    // With --loot, what lies on the ground comes first: the scripted hand picks up before it
    // fights again, which is the half of sprint 7 a run with nobody at the mouse can otherwise
    // never show.
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
    // Only when it is close enough to walk to and fight in a few ticks; anything further and
    // the run is a march rather than a fight.
    if (nearest != nullptr && best < 12.0f * 12.0f) {
        const content::Ground& land = world_.ground();
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
            // Clip space to pixels. y is flipped because clip space runs up the screen and
            // the pointer runs down it, which is the same flip vs_overlay.sc makes for the
            // same reason.
            const float ndcX = clip[0] / clip[3];
            const float ndcY = clip[1] / clip[3];
            *pointerX = (ndcX * 0.5f + 0.5f) * float(ctx.window.width());
            *pointerY = (0.5f - ndcY * 0.5f) * float(ctx.window.height());
        }
    }
    return true;
}

void PlayMode::frame(Context& ctx, const Frame& at) {
    core::Args& args = ctx.args;
    const double deltaSeconds = at.deltaSeconds;

    if (!savePath_.empty() &&
        double(bx::getHPCounter() - keptAt_) / double(bx::getHPFrequency()) > 15.0) {
        keep(ctx);
        keptAt_ = bx::getHPCounter();
    }

    gfx::Camera eye = world_.camera();
    // The Lich's EarthQuake, and it is a TILT and not a slide: MU adds it to
    // `m_State.Angle[0]` (DefaultCamera.cpp:700), which is the camera's pitch in
    // degrees, and decays it by 0.2 a frame (MainScene.cpp:199). Carried here as what
    // it is -- a rotation of the eye about what it is looking at, which is the same
    // orbit MU's camera has. Slid instead, as this was first written, the shake was
    // the quarter of a MU unit it says it is: four millimetres, on a camera six
    // metres out, which is nothing at all.
    {
        const float pitch = world_.played().meteor().quakeDegrees();
        if (pitch != 0.0f) {
            float ahead[3], right[3], up[3];
            for (int a = 0; a < 3; ++a) ahead[a] = eye.position[a] - eye.target[a];
            // The axis to tilt about: across the view, level with the ground.
            right[0] = -ahead[2];
            right[1] = 0.0f;
            right[2] = ahead[0];
            const float length = std::sqrt(right[0] * right[0] + right[2] * right[2]);
            if (length > 1e-6f) {
                for (int a = 0; a < 3; ++a) right[a] /= length;
                const float radians = pitch * 3.14159265f / 180.0f;
                const float c = std::cos(radians), s = std::sin(radians);
                // Rodrigues about `right`, which is a unit vector in the XZ plane.
                const float dot = ahead[0] * right[0] + ahead[2] * right[2];
                up[0] = right[1] * ahead[2] - right[2] * ahead[1];
                up[1] = right[2] * ahead[0] - right[0] * ahead[2];
                up[2] = right[0] * ahead[1] - right[1] * ahead[0];
                for (int a = 0; a < 3; ++a) {
                    eye.position[a] =
                        eye.target[a] + ahead[a] * c + up[a] * s + right[a] * dot * (1.0f - c);
                }
            }
        }
    }
    // The pointer and what it is over, before the sim is stepped: a click is taken at
    // the start of the next tick and walked on that same tick (Realm::accept).
    if (world_.played().isOpen()) {
        float view[16];
        float proj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        float pointerX = 0.0f, pointerY = 0.0f;
        ctx.window.pointer(&pointerX, &pointerY);
        if (args.pointX >= 0.0f) {
            pointerX = args.pointX * float(ctx.window.width());
            pointerY = args.pointY * float(ctx.window.height());
        }
        if (!args.arena.empty()) arenaHand();
        const bool clickNow = scriptedPointer(ctx, at, view, proj, &pointerX, &pointerY);
        // The windows first: a click that lands on one is the interface's, and the
        // world only hears the clicks that land on none. A scripted click is the
        // world's by construction -- it is aimed at a monster.
        if (desk_.ready()) {
            float viewProjNow[16];
            bx::mtxMul(viewProjNow, view, proj);
            desk_.setView(viewProjNow);
            for (const auto& [f, k] : args.uiKeys) {
                if (at.index == f) desk_.scriptKey(k - 1);
            }
            for (const auto& [f, k] : args.uiSkills) {
                if (at.index == f) desk_.scriptSkill(k - 1);
            }
            const float w = float(ctx.window.width()), h = float(ctx.window.height());
            // A pointer parked where the run asked, pressing nothing: what photographing a
            // tooltip needs, since a click on an item in the bag picks it up instead of
            // describing it. A scripted click this frame replaces it, script() keeping the last.
            if (args.hoverX >= 0.0f) desk_.script(args.hoverX * w, args.hoverY * h, false, false);
            for (const core::Args::UiClick& c : args.uiClicks) {
                const bool drag = c.x2 != c.x || c.y2 != c.y;
                const int last = c.frame + (drag ? 3 : 1);
                if (at.index < c.frame || at.index > last) continue;
                // Pressed at the first point, carried to the second, let go there.
                const bool here = at.index == c.frame;
                desk_.script((here ? c.x : c.x2) * w, (here ? c.y : c.y2) * h, here,
                             at.index == last, c.right);
            }
            desk_.update(float(deltaSeconds), ctx.window, world_.played(), pointerX, pointerY);
            // And the pictures for whatever the windows now hold: MU2's Panel.Repaint,
            // which redraws a stage only when what stands on it changed or turns.
            desk_.photograph(ctx.renderer, deltaSeconds);
        }
        const bool windowed = desk_.ready() && desk_.takesPointer();
        world_.played().point(world_.camera(), view, proj, pointerX, pointerY,
                              ctx.window.width(), ctx.window.height());
        if ((ctx.window.clicked(0) && !windowed) || clickNow) world_.played().leftClick();
        if (ctx.window.clicked(1) && !windowed) world_.played().rightClick();
        if (!windowed) world_.zoom(ctx.window.scroll());
        world_.played().update(deltaSeconds);
        // The colour goes out of the world while he is down. Half a second out and a second
        // back: a fall should land and a recovery should feel like one. The renderer drains the
        // scene's own pass, so the HUD and the message over it stay in colour -- which is the
        // point, and is why this is a renderer setting and not a grade in the sheet.
        {
            constexpr float kDrainIn = 0.5f, kDrainBack = 1.0f;
            const bool down = !world_.played().realm().hero().alive();
            const float rate = float(deltaSeconds) / (down ? kDrainIn : kDrainBack);
            drain_ = down ? std::min(1.0f, drain_ + rate) : std::max(0.0f, drain_ - rate);
        }
        for (const int f : args.rises) {
            if (at.index == f) world_.played().rise();
        }
        for (const int f : args.learns) {
            if (at.index == f) world_.played().learned();
        }
    }
    // And only THEN the camera, onto where the character is drawn this frame. Placed
    // before the step, it followed where he stood a frame ago: the town and every
    // shadow in it slid under him by his step length times the frame time, which
    // changes every frame -- 16 mm median and 79 mm worst over a walk, on a shadow
    // texel of 29 mm. docs/shadow-probe.md.
    world_.update(at.elapsed, args.still);
    // The ears, onto the camera just placed: its heading is what the stereo field turns by.
    if (world_.played().isOpen()) {
        world_.played().hear(world_.camera(),
                             world_.indoors(world_.camera().target[0],
                                            world_.camera().target[2]));
    }
    // The lamps flicker, the fires burn, and the glows' levels go into the town before
    // it is gathered, since each rides in its instance. docs/sprints/08a-the-lamps.md.
    if (args.lampsOn) {
        world_.lamps().update(float(deltaSeconds), world_.town(), ctx.renderer, eye.target);
        // What the day gives an unlit puff of smoke: the ambient and the sun on a flat
        // surface, over what the default sheet's noon gives it.
        world_.lamps().gather(ctx.renderer.effects(), eye.target, daylightOf(ctx.lighting));
    }
    // And what is burning and MOVING, which the lamps' static grid cannot hold: a
    // Lich's meteor lights the ground it is falling towards. Handed over every frame,
    // including the frame it becomes none, which is what clears it.
    {
        gfx::PointLight falling[gfx::Renderer::kMaxTransientLights];
        ctx.renderer.setTransientLights(
            falling, world_.played().meteor().lights(falling,
                                                     gfx::Renderer::kMaxTransientLights));
    }
    // The town's own animation: before the town is gathered, since each placement's
    // pose rides in its own instance the same way a glow's level does. Only what the
    // camera can see is posed -- see Sway::update.
    {
        float view[16];
        float proj[16];
        float viewProj[16];
        ctx.renderer.cameraMatrices(eye, view, proj);
        bx::mtxMul(viewProj, view, proj);
        world_.sway().update(float(deltaSeconds), args.cullChunks ? viewProj : nullptr,
                             ctx.renderer, world_.town());
    }
    // What rides those bones, on this frame's pose: the fountain's spray and the
    // merchant animal's lanterns. See game/world/ornaments.h.
    world_.ornaments().update(float(deltaSeconds), world_.sway());
    world_.ornaments().gather(ctx.renderer.effects(), world_.sway());
    // The town's drawables are gathered fresh each frame into one vector that keeps
    // its capacity: a frame appends to a flat array, as foundation 7 says, and
    // allocates nothing after the first.
    townDrawables_.clear();
    townCasters_.clear();
    hoverDrawables_.clear();
    const std::vector<gfx::Drawable>* casters = nullptr;
    if (world_.town().isOpen()) {
        if (args.cullChunks) {
            float view[16];
            float proj[16];
            ctx.renderer.cameraMatrices(eye, view, proj);
            float viewProj[16];
            bx::mtxMul(viewProj, view, proj);
            world_.town().gatherVisible(viewProj, townDrawables_);
            // The sun gets its own list, and for now it is all of them. A chunk
            // behind the camera still casts into the frame, so the camera's frustum
            // is the wrong test for the split -- foundation 7's named bug. Culling
            // the split against its own box is the next step and it is measured
            // separately; drawing every caster is the honest baseline to measure it
            // against.
            world_.town().gatherAll(townCasters_, true);
            casters = &townCasters_;
        } else {
            world_.town().gatherAll(townDrawables_);
        }
    }
    if (world_.played().isOpen()) {
        float view[16];
        float proj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        float viewProj[16];
        bx::mtxMul(viewProj, view, proj);
        ctx.renderer.setDrain(drain_);
        world_.played().gather(ctx.renderer, viewProj, townDrawables_,
                               casters ? &townCasters_ : nullptr, &hoverDrawables_);
        if (desk_.ready()) {
            desk_.overhead(float(deltaSeconds), world_.played(), viewProj, ctx.window.width(),
                           ctx.window.height());
        }
        // And the blood the blows have thrown, into the transparent pass. The figures
        // are not here any more: since the design page of 2026-09-23 they are drawn in
        // a real face by the interface, over the world -- game/ui/tally.cpp, which
        // Desk::overhead above has just placed on this same camera.
        world_.played().showing().gather(ctx.renderer.effects());
        world_.played().gatherMarker(ctx.renderer.effects());
        world_.played().gatherAura(ctx.renderer.effects(), eye.position);
        world_.played().breath().gather(ctx.renderer.effects());
        world_.played().gatherMeteor(ctx.renderer.effects());
        world_.played().gatherStreak(ctx.renderer.effects());
        // And what is lying on the grass: MU2's Drops, tossed up out of the corpse and
        // laid down where they land.
        if (!itemModels_.tables()) {
            itemModels_.open(world_.played().realm().tables(), ctx.paths.assets, &ctx.textures);
            litter_.open(&itemModels_, &world_.ground());
            desk_.useModels(&itemModels_);
        }
        litter_.update(world_.played().realm(), deltaSeconds, world_.played().heldDrops());
        world_.played().setSettledDrops(litter_.settled());
        litter_.gather(townDrawables_, casters ? &townCasters_ : nullptr);
        // The hover ring's own subject, if a drop is what is pointed at rather than a
        // body or a townsperson -- see Play::gather's `hover` for the other two, and
        // leftClick's own ladder, which this stays behind: pointedLying() can be set
        // beside a monster or a townsperson that outranks it.
        if (world_.played().pointedFolk() < 0 && world_.played().pointedAt() == 0) {
            litter_.gatherOne(world_.played().pointedLying(), hoverDrawables_);
        }
    }

    // The crowd goes into the same two lists as the town, and through the same two
    // passes. A figure is not a special case of a drawable: it is a drawable whose
    // mesh carries a skin and whose instance names a palette row.
    world_.crowd().update(float(deltaSeconds));
    if (world_.crowd().figureCount() > 0) {
        float view[16];
        float proj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        float viewProj[16];
        bx::mtxMul(viewProj, view, proj);
        world_.crowd().gather(ctx.renderer, args.cullChunks ? viewProj : nullptr,
                              townDrawables_, casters ? &townCasters_ : nullptr);
    }
    // Along both of the ground's axes, and not a whole texel's worth of either in one
    // step, so the split crosses texel boundaries in x and in y at different frames.
    if (args.shadowSlideMm != 0.0f) {
        const float metres = float(at.index) * args.shadowSlideMm * 0.001f;
        const float slide[3] = {metres, 0.0f, metres * 0.618f};
        ctx.renderer.slideSplit(slide);
    }
    ctx.renderer.draw(eye, ctx.lighting, townDrawables_, &world_.ground(), casters);
    // The gold ring: over the world the frame above just drew, under the windows the
    // line below is about to -- so a window drawn over a ringed monster still covers
    // it, the same order Godot's CanvasLayer(-1) kept the ring in. Shown whenever
    // Play::point found something, exactly as MU2's own Ringed did: it is not gated
    // on the pointer being clear of a window, only the click a monster answers to is.
    if (!hoverDrawables_.empty()) {
        float outlineView[16], outlineProj[16];
        ctx.renderer.cameraMatrices(eye, outlineView, outlineProj);
        const bool shadow = world_.played().pointedFolk() < 0 && world_.played().pointedAt() == 0;
        outline_.show(ctx.renderer, eye, outlineView, outlineProj, ctx.window.width(),
                      ctx.window.height(), hoverDrawables_, shadow);
    }
    if (desk_.ready()) desk_.submit(gfx::ViewHud, ctx.window.width(), ctx.window.height());
    // The tile the character stands on, over his head: the column and row `--at`
    // takes, so a screenshot of something to fix carries where it is. Under the
    // curtain, over the windows' bar -- it is a note on the picture, not a window.
    if (ctx.overlay.ready() && world_.played().isOpen()) {
        float feetX = 0.0f, feetZ = 0.0f;
        world_.characterAt(&feetX, &feetZ);
        const float headY = world_.ground().heightAt(feetX, feetZ) + 2.0f;
        float view[16], proj[16], viewProj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        bx::mtxMul(viewProj, view, proj);
        const float clip[4] = {feetX, headY, feetZ, 1.0f};
        float out[4];
        bx::vec4MulMtx(out, clip, viewProj);
        if (out[3] > 0.0f) {
            const float w = float(ctx.window.width()), h = float(ctx.window.height());
            const float px = (out[0] / out[3] * 0.5f + 0.5f) * w;
            const float py = (0.5f - out[1] / out[3] * 0.5f) * h;
            const sim::Body& hero = world_.played().realm().hero();
            char label[32];
            std::snprintf(label, sizeof label, "%d, %d", hero.column(), hero.row());
            const float scale = 3.0f * h / 1080.0f;
            const float across = ctx.overlay.measure(scale, label);
            const float tall = gfx::Overlay::lineHeight(scale);
            const float pad = 4.0f * scale;
            ctx.overlay.begin(ctx.window.width(), ctx.window.height());
            ctx.overlay.panel(px - across * 0.5f - pad, py - tall - pad * 0.5f,
                              across + pad * 2.0f, tall + pad, 0xA0000000u);
            ctx.overlay.text(px - across * 0.5f, py - tall, scale, 0xFFE8F4FFu, label);
            ctx.overlay.submit(gfx::ViewHud);
        }
    }
    if (args.fps) ctx.readout.draw(ctx.overlay, ctx.window.width(), ctx.window.height());
    if (entrance_ && ctx.curtain.ready()) {
        if (at.index >= 2) entranceSeconds_ += float(deltaSeconds);
        const float t = std::min(1.0f, entranceSeconds_ / 0.1f);
        const float black = 1.0f - t * t * (3.0f - 2.0f * t);
        if (black > 0.0f) {
            // After the HUD in the same view: the HUD draws in submission order, so
            // this lies over it and the whole picture comes up together.
            ctx.curtain.begin(ctx.window.width(), ctx.window.height());
            ctx.curtain.panel(0.0f, 0.0f, float(ctx.window.width()),
                              float(ctx.window.height()), uint32_t(black * 255.0f) << 24);
            ctx.curtain.submit(gfx::ViewHud);
        }
    }
    writeProbes(ctx, at);
}

void PlayMode::writeProbes(Context& ctx, const Frame& at) {
    if (shadowPoints_) {
        float view[16], proj[16], viewProj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        bx::mtxMul(viewProj, view, proj);
        const float w = float(ctx.window.width()), h = float(ctx.window.height());
        std::fprintf(shadowPoints_, "%d", at.index);
        for (size_t p = 0; p < pointGrid_.size(); p += 3) {
            const float point[4] = {pointGrid_[p], pointGrid_[p + 1], pointGrid_[p + 2], 1.0f};
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
            std::fprintf(shadowPoints_, ",%.3f,%.3f", px, py);
        }
        std::fprintf(shadowPoints_, "\n");
    }
    if (shadowLog_) {
        const gfx::Renderer::SplitRecord& split = ctx.renderer.lastSplit();
        const gfx::Camera& cam = world_.camera();
        auto phase = [](float v) { return v - std::floor(v); };
        float heroX = cam.target[0], heroZ = cam.target[2];
        const bool played = world_.characterAt(&heroX, &heroZ);
        const float* followed = world_.followed();
        const float lagMm =
            played ? 1000.0f * std::hypot(heroX - followed[0], heroZ - followed[2]) : 0.0f;
        std::fprintf(shadowLog_,
                     "%d,%.3f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%.4f,%.3f,%.4f,%.4f,%.4f,%.2f\n",
                     at.index, at.deltaSeconds * 1000.0, cam.target[0], cam.target[2],
                     split.texel * 1000.0f, split.texelX, split.texelY, phase(split.texelX),
                     phase(split.texelY), split.depthQuanta, phase(split.depthQuanta), heroX,
                     heroZ, lagMm);
    }
}

void PlayMode::report(Context& ctx) {
    // Foundation 7: the drawn and the culled go in the log, for the camera and for
    // the sun separately, or a culling change cannot be seen to have happened. The
    // sun's line says "all" while its casters are not culled at all, which is the
    // honest way to say that half of this is not built yet.
    if (world_.town().isOpen()) {
        const game::TownCounts& counts = world_.town().counts();
        const game::TownCounts& sun = world_.town().casterCounts();
        core::logf("  town: camera %u of %u chunks and %u of %zu placements; "
                   "sun %u chunks and %u placements, unculled",
                   counts.chunksDrawn, counts.chunksDrawn + counts.chunksCulled,
                   counts.instancesDrawn, world_.town().instanceCount(), sun.chunksDrawn,
                   sun.instancesDrawn);
    }
    if (desk_.ready()) core::logf("%s", desk_.line().c_str());
    if (world_.played().isOpen()) {
        const game::Play& play = world_.played();
        const sim::Body& hero = play.realm().hero();
        const sim::RealmCounts counts = play.realm().counts();
        core::logf("  play: tick %lld, %.3f ms a tick, hero level %d at %.1f,%.1f with "
                   "%d of %d health; %u monsters, %u alive, %u awake",
                   (long long)play.ticks(), play.tickMs(), hero.level, hero.x, hero.y,
                   hero.health, hero.maxHealth, counts.monsters, counts.alive, counts.roused);
        // What the ring is round, as well as what a click would take: the three are
        // the same question and the ladder Play::leftClick answers it with.
        core::logf("  pointer: tile %d,%d%s | %s", play.pointedColumn(), play.pointedRow(),
                   play.pointedFolk() >= 0 ? " (on a townsperson)"
                   : play.pointedAt()      ? " (on a monster)"
                   : play.pointedLying()   ? " (on a drop)"
                                           : "",
                   play.lastLine().c_str());
        if (play.findings().total() > 0) {
            core::logError("  play: %llu invariants broken",
                           (unsigned long long)play.findings().total());
        }
    }
    if (world_.crowd().figureCount() > 0) {
        const game::Crowd& crowd = world_.crowd();
        core::logf("  crowd: %u of %zu figures drawn, %u culled, %zu bones, "
                   "pose %.3f ms, %d palette rows of %d%s",
                   crowd.drawn(), crowd.figureCount(), crowd.culled(), crowd.boneCount(),
                   crowd.poseMs(), ctx.renderer.paletteRowsUsed(),
                   gfx::Renderer::kMaxPaletteRows,
                   ctx.renderer.paletteRowsRefused() ? " -- FULL, the rest stand in bind pose"
                                                     : "");
        if (ctx.renderer.paletteRowsRefused()) {
            core::logError("%d figures found no palette row this frame and stood in "
                           "bind pose; the palette holds %d",
                           ctx.renderer.paletteRowsRefused(), gfx::Renderer::kMaxPaletteRows);
        }
    }
}

void PlayMode::shutdown(Context& ctx) {
    keep(ctx);
    if (!savePath_.empty()) core::logf("save: kept in %s", savePath_.c_str());
    if (shadowLog_) std::fclose(shadowLog_);
    if (shadowPoints_) std::fclose(shadowPoints_);
    shadowLog_ = shadowPoints_ = nullptr;
    // This order is the one main() kept and it is not arbitrary: the stages go back to the
    // renderer before the models they photographed are let go.
    ctx.renderer.closeStages();
    itemModels_.shutdown();
    desk_.shutdown();
    world_.shutdown();
}

}  // namespace mu::app
