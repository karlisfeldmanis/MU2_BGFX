#include "app/modes/bench_mode.h"

#include "core/log.h"
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
        core::logError("the studio found no Bonfire01 in %s; standing at the middle",
                       benchWorld.c_str());
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
    if (kind_ == Kind::Studio && !openStudioWorld(ctx, benchWorld)) return false;

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
        if (!args.category.empty()) bench_.openCategory(args.category, ctx.textures);
        if (!args.pick.empty()) bench_.pick(args.pick, ctx.textures);
        core::logf("browser: %s", bench_.browseLine().c_str());
    }
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
    } else {
        if (ctx.window.held(0)) {
            float dx = 0.0f, dy = 0.0f;
            ctx.window.pointerDelta(&dx, &dy);
            bench_.orbit(dx, dy);
        }
        bench_.zoom(ctx.window.scroll());
    }
}

void BenchMode::frame(Context& ctx, const Frame& at) {
    core::Args& args = ctx.args;
    const float deltaSeconds = float(at.deltaSeconds);
    steer(ctx);

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
        bench_.update(at.elapsed, at.deltaSeconds, !args.still);
        if (args.lampsOn) {
            world_.lamps().update(deltaSeconds, world_.town(), ctx.renderer,
                                  bench_.camera().target);
            world_.lamps().gather(ctx.renderer.effects(), bench_.camera().target,
                                  daylightOf(ctx.lighting));
        }
        townDrawables_.clear();
        if (world_.town().isOpen()) world_.town().gatherAll(townDrawables_);
        const std::vector<gfx::Drawable>& subject = bench_.gather(ctx.renderer);
        townDrawables_.insert(townDrawables_.end(), subject.begin(), subject.end());
        ctx.renderer.draw(bench_.camera(), ctx.lighting, townDrawables_, &world_.ground());
        drawList(ctx);
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
    bench_.shutdown();
    ctx.renderer.closeStages();
    world_.shutdown();
}

}  // namespace mu::app
