#include "app/modes/bench_mode.h"

#include <cmath>

#include "app/preloader.h"
#include "content/showing.h"
#include "content/tables.h"
#include "core/loading.h"
#include "core/log.h"
#include "core/paths.h"
#include "game/world/maps.h"
#include "game/shine.h"
#include "game/wings.h"
#include "game/ui/browser_list.h"

namespace mu::app {

BenchMode::Kind BenchMode::kindOf(const core::Args& args) {
    if (!args.figure.empty()) return Kind::Figure;
    if (args.browse) return args.studio ? Kind::Studio : Kind::Browser;
    return Kind::Model;
}

bool BenchMode::openStudioWorld(Context& ctx, const std::string& benchWorld) {
    if (!world_.open(ctx.paths.assets, benchWorld, ctx.textures, 0, false)) {
        core::logError("the studio's world did not open");
        world_.shutdown();
        return false;
    }
    if (ctx.args.lampsOn) world_.lamps().light(ctx.renderer);
    const content::CookedTown& cooked = world_.town().cooked();
    const float middle = float(world_.ground().size()) * 0.5f * world_.ground().metresPerTile();
    float best = 1e30f;
    for (const content::TownInstance& one : cooked.instances) {
        if (one.model >= cooked.models.size() || cooked.models[one.model].name != "Bonfire01") {
            continue;
        }
        const float dx = one.position[0] - middle, dz = one.position[2] + middle;
        if (dx * dx + dz * dz < best) {
            best = dx * dx + dz * dz;
            for (int i = 0; i < 3; ++i) studioFire_[i] = one.position[i];
        }
    }
    if (best == 1e30f) {
        core::logf("studio: no Bonfire01 in %s; standing at the middle", benchWorld.c_str());
        studioFire_[0] = middle + 1.6f;
        studioFire_[2] = -middle - 1.6f;
    }
    // The stage's arrangement: the fire 1.6 m to the subject's +x and -z, which is the
    // right of MU's picture.
    studioStand_[0] = studioFire_[0] - 1.6f;
    studioStand_[2] = studioFire_[2] + 1.6f;
    studioStand_[1] = world_.ground().heightAt(studioStand_[0], studioStand_[2]);
    studioFire_[1] = world_.ground().heightAt(studioFire_[0], studioFire_[2]) + 0.6f;
    return true;
}

bool BenchMode::open(Context& ctx) {
    core::Args& args = ctx.args;
    kind_ = kindOf(args);
    if (args.distance > 0.0f) bench_.setDistance(args.distance);
    const std::string modelPath = (args.model.empty() || args.model[0] == '/')
                                      ? args.model
                                      : ctx.paths.under(ctx.paths.assets, args.model);
    const std::string benchWorld = args.world.empty() ? "lorencia" : args.world;

    if (kind_ == Kind::Figure) {
        if (!bench_.openFigure(ctx.paths.assets, benchWorld, args.figure, args.clip, args.safe,
                               ctx.textures)) {
            core::logError("the bench did not open");
            return false;
        }
        return true;
    }

    const bool browsing = kind_ == Kind::Browser || kind_ == Kind::Studio;
    if (browsing && !ctx.overlay.init(ctx.paths.shaders)) {
        core::logError("the viewer opened without its list; the names are in the log");
    }
    // The browser's load: for the studio, its world, the figure tables and the wardrobe -- four
    // to ten seconds -- on the preloader's worker behind the game's own spinner (app/preloader.h
    // says why the creations in it are legal); for the bare browser, here, as it always was.
    auto load = [&]() -> bool {
        // The spinner's stages: World::open names its own in the first half.
        core::Loading::span(0.0f, 0.5f);
        if (kind_ == Kind::Studio && !openStudioWorld(ctx, benchWorld)) return false;
        core::Loading::span(0.0f, 1.0f);
        core::Loading::stage("the figures and the wardrobe", 0.5f, 0.96f);
        if (kind_ == Kind::Studio) setShine_.open(ctx.paths.assets, ctx.textures);

        if (browsing) {
            // The stage is the browser with the world's own lamps, fire and objects stood round
            // the subject; the bare browser is the subject on its plot and nothing else.
            const bool opened =
                kind_ == Kind::Studio
                    ? bench_.openStudio(ctx.paths.assets, benchWorld, studioStand_, studioFire_,
                                        ctx.textures)
                    : (args.stage ? bench_.openStage(ctx.paths.assets, benchWorld, ctx.textures)
                                  : bench_.openBrowser(ctx.paths.assets, benchWorld, ctx.textures));
            if (!opened) {
                core::logError("the bench did not open");
                world_.shutdown();
                return false;
            }
            // Where the browser opens, for a run with nobody at the keyboard. Neither is fatal: a
            // category or a name that is not there has said so, and the viewer is still a viewer.
            bench_.showBearer(args.bearer);
            if (!args.category.empty()) bench_.openCategory(args.category, ctx.textures);
            if (!args.pick.empty()) bench_.pick(args.pick, ctx.textures);
            core::logf("browser: %s", bench_.browseLine().c_str());
            core::Loading::stage("the shine", 0.96f, 1.0f);
        // The shine's sheets out of the showing, always: the Wings of Darkness draw their violet
            // chrome off Chrome02 at +0 (game/shine.h kShineDarkness). Not fatal.
            {
                content::Showing showing;
                std::string error;
                if (content::loadShowing(ctx.paths.assets + "/cooked/showing/showing.mus", showing,
                                         error)) {
                    game::lendShine(showing, ctx.paths.assets, ctx.textures, ctx.renderer);
                    game::WingLook::lendSparks(showing, ctx.paths.assets, ctx.textures);
                    if (kind_ == Kind::Studio) standShow_.open(ctx.paths.assets, ctx.textures, showing);
                }
            }
            // The refinement shine, on the subject's items: their rows out of the world's tables
            // and the two sheets out of the showing. Only when asked, since neither is otherwise
            // the viewer's business. Not fatal: the subject is still shown, at +0.
            if (args.plus > 0) {
                content::Tables tables;
                content::Showing showing;
                std::string error;
                const std::string world = benchWorld.empty() ? std::string("lorencia") : benchWorld;
                if (!content::loadTables(ctx.paths.assets + "/cooked/" + world + "/" + world + ".mur",
                                         tables, error)) {
                    core::logError("--plus: no item rows for %s: %s", world.c_str(), error.c_str());
                } else if (!content::loadShowing(ctx.paths.assets + "/cooked/showing/showing.mus",
                                                 showing, error)) {
                    core::logError("--plus: the showing did not open: %s", error.c_str());
                } else {
                    bench_.setPlus(args.plus, tables.items);
                    game::lendShine(showing, ctx.paths.assets, ctx.textures, ctx.renderer);
                }
            }
        }
        return true;
    };
    const bool loaded = kind_ == Kind::Studio
                            ? Preloader::run(ctx, load, &quitEarly_, false, {}, Preloader::studio())
                            : load();
    if (quitEarly_) return true;   // closed under the spinner: a quit, not a failure
    if (!loaded) return false;
    // The studio's panels, where the list would be; the bench goes on without them if they fail.
    if (kind_ == Kind::Studio) sceneWorld_ = benchWorld;
    // With the panels the studio opens on its Stage (the user's pick, 2026-10-12); the world
    // stays open behind it for Home and the world list. Without them -- studio.py's sheets --
    // it is the bonfire, as it was.
    if (kind_ == Kind::Studio && args.list && desk_.open(ctx)) setScene(ctx, "Stage");
    // The stage's lamps, once, as a world's are: the renderer lays its light grid here.
    if (bench_.hasStage() && args.lampsOn) bench_.stageLamps().light(ctx.renderer);

    if (kind_ == Kind::Model &&
        !bench_.open(ctx.paths.assets, benchWorld, modelPath, ctx.textures)) {
        core::logError("the bench did not open");
        return false;
    }
    return true;
}

void BenchMode::steer(Context& ctx) {
    // The browser's steering, before the frame it steers. Left and right walk one,
    // down and up walk ten, and the step is an edge rather than a state: at 400 fps a
    // key read as held walks the whole list on one tap.
    if (bench_.browsing()) {
        int by = 0;
        if (ctx.window.stepped(gfx::Window::Step::Previous)) by -= 1;
        if (ctx.window.stepped(gfx::Window::Step::Next)) by += 1;
        if (ctx.window.stepped(gfx::Window::Step::PreviousTen)) by -= 10;
        if (ctx.window.stepped(gfx::Window::Step::NextTen)) by += 10;
        if (by != 0) {
            bench_.step(by, ctx.textures);
            core::logf("browser: %s", bench_.browseLine().c_str());
        }
        // Tab walks the categories, skipping any that is empty: a world cooked
        // without figures must not have two tab presses that appear to do nothing.
        if (ctx.window.stepped(gfx::Window::Step::Category)) {
            const size_t count = bench_.categoryCount();
            for (size_t i = 1; i <= count; ++i) {
                const size_t next = (bench_.categoryIndex() + i) % count;
                if (bench_.setCategory(next, ctx.textures)) break;
            }
            core::logf("browser: %s", bench_.browseLine().c_str());
        }
        if (ctx.window.stepped(gfx::Window::Step::PreviousClip)) bench_.stepClip(-1);
        if (ctx.window.stepped(gfx::Window::Step::NextClip)) bench_.stepClip(1);
    }
    // T walks the times of day. It is the viewer's key; the game has no day and night yet.
    if ((kind_ == Kind::Browser || kind_ == Kind::Studio) &&
        ctx.window.stepped(gfx::Window::Step::Time)) {
        ctx.time.step();
    }
}

void BenchMode::drawList(Context& ctx) {
    if (!bench_.browsing() || !ctx.overlay.ready() || !ctx.args.list) return;
    float px = 0.0f, py = 0.0f;
    ctx.window.pointer(&px, &py);
    const game::ListHit hit = game::drawBrowserList(ctx.overlay, bench_, ctx.window.width(),
                                                    ctx.window.height(), px, py);
    const game::ListHit clips = game::drawClipList(ctx.overlay, bench_, ctx.window.width(),
                                                   ctx.window.height(), px, py);
    // Under the panel rather than in it: which clip is running, where its clock
    // stands and how long it is. A still cannot show that a clip is playing, and
    // a monster frozen on frame one looks exactly like one standing still.
    if (bench_.hasFigure()) {
        ctx.overlay.text(hit.x + 4.0f, hit.y + hit.h + 10.0f, 2.4f, 0xFFc8c8c8u,
                         bench_.clipLine());
    }
    ctx.overlay.text(hit.x + 4.0f, hit.y + hit.h + (bench_.hasFigure() ? 34.0f : 10.0f), 2.4f,
                     0xFFc8c8c8u,
                     std::string(ctx.time.name()) + "    T changes the time of day");
    ctx.overlay.submit(gfx::ViewHud);
    // The list eats the pointer while it is over it, so a click on a name does
    // not also drag the camera and the wheel scrolls the list rather than zooming.
    if (hit.over) {
        if (ctx.window.clicked(0) && hit.tab >= 0) {
            bench_.setCategory(size_t(hit.tab), ctx.textures);
            core::logf("browser: %s", bench_.browseLine().c_str());
        }
        if (ctx.window.clicked(0) && hit.hovered >= 0) {
            bench_.step(int(hit.hovered - (long long)bench_.browseIndex()), ctx.textures);
            core::logf("browser: %s", bench_.browseLine().c_str());
        }
        const float wheel = ctx.window.scroll();
        if (wheel != 0.0f) {
            bench_.step(wheel > 0.0f ? -1 : 1, ctx.textures);
        }
    } else if (clips.over) {
        // The clip list the same way: a click plays the row, the wheel steps the clip.
        if (ctx.window.clicked(0) && clips.hovered >= 0) bench_.playClip(size_t(clips.hovered));
        const float wheel = ctx.window.scroll();
        if (wheel != 0.0f) bench_.stepClip(wheel > 0.0f ? -1 : 1);
    } else {
        look(ctx);
    }
}

void BenchMode::look(Context& ctx) {
    // The right drag turns it too: in Place's Ground the left one paints (app/modes/studio_paint.h).
    if (ctx.window.held(0) || ctx.window.held(1)) {
        float dx = 0.0f, dy = 0.0f;
        ctx.window.pointerDelta(&dx, &dy);
        bench_.orbit(dx, dy);
    }
    bench_.zoom(ctx.window.scroll());
}

void BenchMode::leaveArena(Context& ctx) {
    if (!arena_.up()) return;
    arena_.close(ctx);
    desk_.arenaIs(false);
    // The bench's light and lamps back, which the arena's world had taken.
    ctx.time.setScene(onStage_ ? std::string() : game::mapSheet(ctx.paths.sheets, sceneWorld_));
    if (!onStage_ && world_.town().isOpen() && ctx.args.lampsOn) world_.lamps().light(ctx.renderer);
}

void BenchMode::steerArena(Context& ctx) {
    // Taken at the frame's start, before the panels begin theirs: an arena opening runs the
    // preloader, which draws frames of its own.
    std::string breed, world;
    const int ask = desk_.takeArena(&breed, &world);
    if (ask != 0) leaveArena(ctx);
    if (ask == 1) {
        // On the monster's own spawn tile when the bench stands it there (scene Home), else the
        // arena's own patch.
        int column = -1, row = -1;
        if (world == sceneWorld_ && !onStage_) {
            const float m = world_.ground().metresPerTile();
            column = int(std::floor(studioStand_[0] / m));
            row = int(std::floor(-studioStand_[2] / m));
        }
        arena_.open(ctx, breed, world, column, row, core::folders().work + "/logs/mu/arena.log");
    }
    desk_.arenaIs(arena_.up());
}

void BenchMode::frame(Context& ctx, const Frame& at) {
    core::Args& args = ctx.args;
    const float deltaSeconds = float(at.deltaSeconds);
    if (kind_ == Kind::Studio && desk_.isOpen()) steerArena(ctx);
    if (desk_.isOpen()) desk_.begin(ctx, deltaSeconds, at.elapsed);
    if (arena_.up()) {
        // The fight is the frame; the panels over it. A click on a panel is not a walk order.
        if (desk_.wantsPointer()) ctx.window.swallowPointer();
        arena_.frame(ctx, at);
        desk_.panels(ctx, bench_, sceneWorld_, world_, arena_.camera(), true);
        const std::string scene = desk_.takeScene();
        if (!scene.empty()) {
            leaveArena(ctx);   // another scene: the fight is left
            setScene(ctx, scene);
        }
        desk_.end();
        return;
    }
    // A filter being typed into keeps the arrows and Tab: they are the box's then.
    if (!desk_.wantsKeys()) steer(ctx);

    if (kind_ == Kind::Studio) {
        // The sweep: a block of --shot frames an angle, --turns angles a time of day,
        // noon then dusk then night. The shot at the end of a block is taken at the frame
        // the next begins on, so a block is counted from the frame after it.
        if (args.turns > 0 && args.shotEvery > 0) {
            const int block = at.index > 0 ? (at.index - 1) / args.shotEvery : 0;
            bench_.setTurn(360.0f * float(block % args.turns) / float(args.turns));
            const int time = (block / args.turns) % 3;
            if (time != ctx.time.which()) ctx.time.set(time);
        }
        // With the panels open the camera turns only when dragged (the user, 2026-10-12: "dont
        // rotate camera automatically, set it so you can control orbit yourself"); the
        // turntable sheets, which run without them, still go round. Held still while the desk
        // shoots a swap's before and after: one pose, one angle.
        const bool spin = !args.still && !desk_.isOpen();
        if (desk_.holding()) {
            bench_.update(desk_.heldSeconds(), 0.0, spin);
        } else {
            bench_.update(at.elapsed, at.deltaSeconds, spin);
        }
        if (args.lampsOn && !onStage_) {
            world_.lamps().update(deltaSeconds, world_.town(), ctx.renderer,
                                  bench_.camera().target);
            world_.lamps().gather(ctx.renderer.effects(), bench_.camera().target,
                                  daylightOf(ctx.lighting));
        }
        // The world's own sprites round the view, as play throws them (PlayMode, after the
        // ornaments): Kanturu's chasm mist, the falls' spray, the void's clouds, Icarus's road,
        // the sand haze, the bubbles and the bridges' shade. Without them the studio's Kanturu
        // had no mist, and what showed against it in the game could not be judged here.
        if (!onStage_ && world_.town().isOpen()) {
            const float step = desk_.holding() ? 0.0f : deltaSeconds;
            const float* near = bench_.camera().target;
            // The town's own animation first, as play poses it before the gather: the trees'
            // sway, the wheels and the lamps carried on them, so the studio's world moves as
            // the game's does.
            world_.sway().update(step, nullptr, ctx.renderer, world_.town());
            if (args.lampsOn) world_.lamps().follow(world_.sway(), ctx.renderer);
            world_.ornaments().update(step, world_.sway());
            world_.ornaments().gather(ctx.renderer.effects(), world_.sway());
            world_.throwAround(step, near, ctx.renderer.effects());
            world_.skyClouds().update(step, near, near, [](const float*, const float*) {});
            world_.skyClouds().gather(ctx.renderer.effects());
            world_.sandHaze().update(step);
            world_.sandHaze().gather(ctx.renderer.effects(), bench_.camera());
            world_.bubbles().update(step, near);
            world_.bubbles().gather(ctx.renderer.effects());
            world_.shades().gather(ctx.renderer.effects(), near);
        }
        townDrawables_.clear();
        if (onStage_) {
            townDrawables_ = stage_.drawables();
        } else if (world_.town().isOpen()) {
            world_.town().gatherAll(townDrawables_);
        }
        if (!onStage_ && world_.town().isOpen()) {
            folk_.follow(ctx.paths.root, sceneWorld_, bench_.figures(), world_.ground(), at.elapsed);
            folk_.update(desk_.holding() ? 0.0f : deltaSeconds);
            folk_.gather(ctx.renderer, townDrawables_);
        }
        const std::vector<gfx::Drawable>& subject = bench_.gather(ctx.renderer);
        townDrawables_.insert(townDrawables_.end(), subject.begin(), subject.end());
        // After the gather, which posed it: the suit's set light, round where it stands.
        int plus = 0, lit = 0;
        float feet[3], arms[6][3], colour[3];
        if (bench_.setSubject(plus, feet, arms, lit, colour)) {
            setShine_.wear(1, plus, feet, 0.0f, arms, lit, colour);
        }
        setShine_.update(float(at.deltaSeconds), world_.ground().metresPerTile());
        // The subject's own show, as Play would draw it standing (game/stand_show.h).
        if (standShow_.isOpen() && bench_.hasFigure()) {
            if (bench_.figure().body() != shownBody_) {
                shownBody_ = bench_.figure().body();
                standShow_.fit(bench_.figure());
            }
            standShow_.update(desk_.holding() ? 0.0f : deltaSeconds, bench_.figure());
            standShow_.gather(ctx.renderer.effects(), bench_.camera().position);
            if (onStage_) {
                // The Stage has no lamps: the show's own lights are its only point lights.
                gfx::PointLight lights[gfx::Renderer::kMaxTransientLights];
                const uint32_t count = standShow_.lights(lights, gfx::Renderer::kMaxTransientLights,
                                                         bench_.camera().target);
                ctx.renderer.setTransientLights(count ? lights : nullptr, count);
            }
        }
        setShine_.gather(ctx.renderer.effects(), world_.ground(), bench_.camera().position);
        if (desk_.isOpen() && !desk_.effectsShown()) ctx.renderer.effects().begin();
        ctx.renderer.draw(bench_.camera(), ctx.lighting, townDrawables_,
                          onStage_ ? nullptr : &world_.ground());
        if (desk_.isOpen()) {
            desk_.panels(ctx, bench_, sceneWorld_, world_, bench_.camera(), onStage_);
            const std::string scene = desk_.takeScene();
            if (!scene.empty()) setScene(ctx, scene);
            const int plus = desk_.takePlus();
            if (plus >= 0) studioPlus(ctx, plus);
            if (!desk_.wantsPointer() && !desk_.tookPointer()) look(ctx);
            desk_.end();
        } else {
            drawList(ctx);
        }
        return;
    }

    bench_.update(at.elapsed, at.deltaSeconds, !args.still);
    // The stage's lamps, the way a world's are run: the flicker, the glows written into
    // its town, and the flames near the camera into the transparent pass.
    if (bench_.hasStage() && args.lampsOn) {
        bench_.stageLamps().update(deltaSeconds, bench_.stageTown(), ctx.renderer,
                                   bench_.camera().target);
        bench_.stageLamps().gather(ctx.renderer.effects(), bench_.camera().target,
                                   daylightOf(ctx.lighting));
    }
    if (bench_.hasStage()) {
        bench_.stageSway().update(deltaSeconds, nullptr, ctx.renderer, bench_.stageTown());
    }
    ctx.renderer.draw(bench_.camera(), ctx.lighting, bench_.gather(ctx.renderer),
                      bench_.ground());
    drawList(ctx);
}

void BenchMode::report(Context& ctx) {
    // The bench says where the clock is, not only which clip: position AND length.
    if (bench_.hasFigure()) core::logf("  %s", bench_.clipLine().c_str());
    if (bench_.browsing()) core::logf("  %s", bench_.browseLine().c_str());
}

void BenchMode::shutdown(Context& ctx) {
    desk_.shutdown();
    stage_.shutdown();
    arena_.close(ctx);
    standShow_.shutdown();
    folk_.clear();
    bench_.shutdown();
    setShine_.shutdown();
    ctx.renderer.closeStages();
    world_.shutdown();
}

}  // namespace mu::app
