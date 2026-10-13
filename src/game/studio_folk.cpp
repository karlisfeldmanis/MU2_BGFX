#include "game/studio_folk.h"

#include <bx/math.h>

#include <cmath>
#include <filesystem>

#include "content/ground.h"
#include "core/json.h"
#include "core/log.h"
#include "core/paths.h"
#include "game/figures.h"
#include "sim/realm.h"

namespace mu::game {

void StudioFolk::clear() {
    folk_.clear();
    world_.clear();
    stamp_ = -1;
}

void StudioFolk::follow(const std::string& root, const std::string& world, const Figures& figures,
                        const content::Ground& ground, double seconds) {
    // The figures built again (the browser refilled): the bodies the folk stand in are gone.
    if (figures.generation() != generation_) {
        generation_ = figures.generation();
        folk_.clear();
        stamp_ = -1;
    }
    if (world == world_ && stamp_ != -1 && seconds - lookedAt_ < 0.5) return;
    lookedAt_ = seconds;
    const std::string path = core::folders().content + "/world/" + world + "/world.json";
    std::error_code error;
    const auto when = std::filesystem::last_write_time(path, error);
    const long long stamp = error ? 0 : (long long)when.time_since_epoch().count();
    if (world == world_ && stamp == stamp_) return;
    world_ = world;
    stamp_ = stamp;
    folk_.clear();
    if (error) return;
    if (scratch_.empty()) scratch_.assign(size_t(gfx::Renderer::kMaxBones) * 12, 0.0f);

    const core::Json manifest = core::parseJsonFile(path);
    const core::Json& npcs = manifest["npcs"];
    const float metresPerTile = ground.metresPerTile();
    for (size_t i = 0; i < npcs.size(); ++i) {
        const core::Json& person = npcs.at(i);
        const FigureBody* look = figures.body(person["figure"].stringOr(""));
        if (!look) continue;   // "" is the town's own placement, drawn by the town
        // game/play_open.cpp's lines, as they stand the table's folk.
        const int lookNumber = int(person["look"].numberOr(3.0));
        const int facing = lookNumber >= 1 && lookNumber <= 8 ? lookNumber : 3;
        const float bearing = float(((facing - 3) % 8 + 8) % 8) * (bx::kPi / 4.0f);
        const float angle = std::atan2(-std::cos(bearing), std::sin(bearing));
        const float yaw = std::atan2(std::cos(angle), -std::sin(angle));
        const float x = (float(person["at"].at(0).numberOr(0.0)) + 0.5f) * metresPerTile;
        const float z = -(float(person["at"].at(1).numberOr(0.0)) + 0.5f) * metresPerTile;
        const float at[3] = {x, ground.heightAt(x, z), z};
        const float scale = look->scale * (int(person["npc"].numberOr(0.0)) == sim::kTersia ? 0.93f : 1.0f);
        Figure one;
        // In town: the studio holds no tables and so no safe-zone bit; a townsperson stands
        // unarmed, as the four inside Lorencia's do.
        one.stand(look, at, yaw, scale, true);
        folk_.push_back(std::move(one));
    }
    core::logf("studio: %zu townsfolk stood in %s from its manifest", folk_.size(), world.c_str());
}

void StudioFolk::update(float deltaSeconds) {
    for (Figure& one : folk_) one.update(deltaSeconds);
}

void StudioFolk::gather(gfx::Renderer& renderer, std::vector<gfx::Drawable>& out) {
    for (Figure& one : folk_) {
        const int bones = one.pose(scratch_.data());
        const int row = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        one.poseHeld(renderer, scratch_.data());
        one.gather(row, out);
    }
}

}  // namespace mu::game
