// The studio's scenes: BenchMode::setScene (app/modes/bench_mode.h), out of bench_mode.cpp so
// the scene rules sit in one place. docs/plans/order-plan.md §9c, F3b.
//
// The user, 2026-10-12, picked "a stage and the world": the Stage to work on a thing, with
// nothing moving round it, and the world to judge it where the game puts it. Home is that
// world chosen for the subject: a monster where it spawns, an armour set where a character is
// first seen (the character screen's middle pedestal), and anything else at Lorencia's
// bonfire, where the weapon sheets were approved.
#include "app/modes/bench_mode.h"

#include <cstdio>

#include "content/showing.h"
#include "content/tables.h"
#include "core/log.h"
#include "game/shine.h"
#include "game/figures.h"
#include "game/pedestals.h"
#include "game/world/maps.h"

namespace mu::app {

void BenchMode::setScene(Context& ctx, const std::string& scene) {
    if (scene == "reload") {
        // The world's placements were cooked again: its town read anew, where it stood.
        world_.shutdown();
        setScene(ctx, sceneWorld_);
        return;
    }
    if (scene == "Stage") {
        if (!stage_.isOpen() && !stage_.open(ctx.textures)) return;
        stage_.place(studioStand_);
        // The town's lamps and the fire's flicker go dark with it: the Stage is lit by the
        // sheets alone.
        ctx.renderer.setPointLights(nullptr, 0, 0.0f, 0.0f, 0.0f);
        ctx.renderer.setTransientLights(nullptr, 0);
        ctx.time.setScene("");   // the base sheet alone, as the Stage always was
        onStage_ = true;
        core::logf("studio: on the Stage, %s kept behind it", sceneWorld_.c_str());
        return;
    }

    // "kanturu 78,91": the world with the subject stood on that tile, so an agent can look where
    // the user saw something (the control socket's `scene`, docs/content/cooker.md).
    std::string world = scene;
    int tileColumn = -1, tileRow = -1;
    const size_t space = scene.find(' ');
    if (space != std::string::npos) {
        world = scene.substr(0, space);
        if (std::sscanf(scene.c_str() + space + 1, "%d,%d", &tileColumn, &tileRow) != 2) tileColumn = -1;
    }
    bool spawn = false, pedestal = false;
    if (scene == "Home") {
        const game::FigureBody* body = bench_.subjectBody();
        spawn = body && body->kind == game::BodyKind::Monster;
        pedestal = body && body->kind == game::BodyKind::Armour;
        world = spawn ? sceneWorld_ : (pedestal ? game::Pedestals::kMap : "lorencia");
    }
    const bool leftStage = onStage_;
    onStage_ = false;
    if (world != sceneWorld_ || !world_.town().isOpen()) {
        world_.shutdown();
        if (!openStudioWorld(ctx, world)) {
            core::logError("studio: %s did not open; back to the Stage", world.c_str());
            sceneWorld_.clear();
            setScene(ctx, "Stage");
            return;
        }
        sceneWorld_ = world;
    } else if (leftStage && ctx.args.lampsOn) {
        world_.lamps().light(ctx.renderer);   // the lamps the Stage put out
    }

    // The map's own light over the base, as play_mode.cpp lays it (dry: the studio has no
    // weather). Before 2026-10-12 every world scene wore Lorencia's base sheet alone.
    ctx.time.setScene(game::mapSheet(ctx.paths.sheets, world));

    const float m = world_.ground().metresPerTile();
    if (tileColumn >= 0) {
        studioStand_[0] = (float(tileColumn) + 0.5f) * m;
        studioStand_[2] = -(float(tileRow) + 0.5f) * m;
        studioStand_[1] = world_.ground().heightAt(studioStand_[0], studioStand_[2]);
    } else if (spawn) {
        // The middle of the breed's first spawn rectangle, in tiles: column is +x, row is -z.
        const game::FigureBody* body = bench_.subjectBody();
        for (const game::Breed& breed : bench_.figures().breeds()) {
            if (breed.body != body || breed.rects.empty()) continue;
            const game::Breed::Rect& r = breed.rects.front();
            studioStand_[0] = 0.5f * float(r.x1 + r.x2) * m;
            studioStand_[2] = -0.5f * float(r.y1 + r.y2) * m;
            studioStand_[1] = world_.ground().heightAt(studioStand_[0], studioStand_[2]);
            break;
        }
    } else if (pedestal) {
        float column = 0.0f, row = 0.0f;
        game::Pedestals::middleTile(&column, &row);
        studioStand_[0] = column * m;
        studioStand_[2] = -row * m;
        studioStand_[1] = world_.ground().heightAt(studioStand_[0], studioStand_[2]);
    }
    if (!bench_.restand(ctx.paths.assets, world, studioStand_, studioFire_, ctx.textures)) {
        core::logError("studio: the subject is not cooked into %s", world.c_str());
    }
    core::logf("studio: in %s at %.1f,%.1f m%s", world.c_str(), studioStand_[0], studioStand_[2],
               spawn ? ", where it spawns" : (pedestal ? ", on the middle pedestal" : ""));
}

void BenchMode::studioPlus(Context& ctx, int plus) {
    if (plusRows_.empty() && plus > 0) {
        // Lorencia's tables hold every item, as every world's do (a new item cooks them all).
        content::Tables tables;
        content::Showing showing;
        std::string error;
        if (!content::loadTables(ctx.paths.assets + "/cooked/lorencia/lorencia.mur", tables, error)) {
            core::logError("studio: no item rows for the refinement: %s", error.c_str());
            return;
        }
        plusRows_ = tables.items;
        if (content::loadShowing(ctx.paths.assets + "/cooked/showing/showing.mus", showing, error)) {
            game::lendShine(showing, ctx.paths.assets, ctx.textures, ctx.renderer);
        }
    }
    bench_.setPlus(plus, plusRows_);
}

}  // namespace mu::app
