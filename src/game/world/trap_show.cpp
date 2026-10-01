#include "game/world/trap_show.h"

#include <algorithm>
#include <cmath>

#include "content/placement.h"
#include "core/files.h"
#include "core/log.h"
#include "sim/traps.h"

namespace mu::game {
namespace {

// PlaySpeed 0.4 keys a reference frame is ten keys a second; the cook spaces Object41's four keys
// 0.25 s apart, so its clip runs at 2.5.
constexpr float kStrikeRate = 0.4f * 25.0f * 0.25f;
constexpr float kFrames = 25.0f;  // REFERENCE_FPS
constexpr float kUnit = 0.01f;    // MU's unit, in metres
constexpr float kPi = 3.14159265f;

// The 29 hidden Object53 markers (type 52), where the ceiling drops its stones: MU's stored
// position in units (x east, y south, z up) and its angle about z in degrees, as dungeon.json has
// them. Carried here because the cook hides the placement (terrain.py HIDDEN_BY_MAP).
struct Marker {
    float x, y, z, angle;
};
constexpr Marker kMarkers[] = {
    {164.6f, 1534.0f, 255.1f, 0.0f},
    {224.9f, 2051.3f, 260.2f, 60.0f},
    {828.8f, 3478.0f, 267.8f, 0.0f},
    {130.9f, 5225.8f, 167.1f, 1170.0f},
    {106.6f, 8766.0f, 182.3f, 1170.0f},
    {117.3f, 8190.3f, 182.6f, 1170.0f},
    {91.1f, 8058.2f, 164.3f, 90.0f},
    {151.2f, 8544.4f, 151.8f, 90.0f},
    {135.3f, 13299.5f, 187.4f, 1080.0f},
    {882.2f, 13306.2f, 169.2f, 1080.0f},
    {3015.2f, 12707.4f, 247.6f, 0.0f},
    {2817.3f, 23889.1f, 169.3f, -1080.0f},
    {4444.1f, 12783.8f, 173.7f, 90.0f},
    {4049.5f, 12809.9f, 234.2f, 1080.0f},
    {3216.5f, 12885.1f, 191.9f, 1080.0f},
    {4202.5f, 15400.1f, 159.3f, -990.0f},
    {4106.3f, 16988.6f, 174.1f, -1080.0f},
    {4500.0f, 21400.0f, 169.5f, 0.0f},
    {5700.0f, 16700.0f, 169.8f, 0.0f},
    {8300.0f, 6800.0f, 189.2f, 0.0f},
    {9270.1f, 18189.7f, 124.5f, -1080.0f},
    {11600.0f, 5200.0f, 168.4f, 360.0f},
    {11200.0f, 9900.0f, 208.2f, 0.0f},
    {15500.0f, 10200.0f, 175.2f, 90.0f},
    {18860.8f, 12999.8f, 126.4f, -1080.0f},
    {19368.9f, 14996.6f, 167.7f, -1080.0f},
    {20402.3f, 15002.3f, 155.1f, -1080.0f},
    {23900.0f, 14650.0f, 211.0f, 450.0f},
    {23800.0f, 15300.0f, 200.5f, 450.0f},
};
// Only markers this near him drop anything: MU moves only what is in view.
constexpr float kStoneReach = 25.0f;
// And never more than this many in the air, which the 29 markers never reach.
// MU's size is kept: a 6 cm pebble is grit at this camera, and the user took it as it is
// (2026-09-30: "if they have to be small then let it be").
constexpr size_t kMostStones = 600;
// One frame in this many drops a pebble at a marker. MU's rand_fps_check(3) is about eight a
// second at each, a steady trickle; ours, at the user's word (2026-10-01: 'we need this stone
// droping aniamtions happend more rarely'), a pebble every second or so.
constexpr uint32_t kStoneOdds = 30;
// The Meteorite Trap's plate: how far it is sunk (Object26 is 9.3 cm thick; at 8.5 cm the uneven
// floor swallowed it whole) and its light's share.
constexpr float kPlateSink = 0.04f;
constexpr float kPlateShade = 0.6f;

}  // namespace

bool TrapShow::open(const std::string& assetDir, const std::string& world,
                    content::Textures& textures, const content::Showing& table) {
    shutdown();
    if (world != "dungeon" && world != "losttower") return true;
    const std::string dir = core::join(assetDir, "cooked/" + world);
    const auto load = [&](const char* model) -> const content::Mesh* {
        std::vector<uint8_t> bytes = core::readFile(core::join(dir, std::string("meshes/") + model + ".mum"));
        content::CookedMesh cooked;
        std::string error;
        if (bytes.empty() || !content::parseCookedMesh(bytes, cooked, error)) {
            core::logError("traps: no cooked %s in %s (tools/cook_one.py %s --world %s)", model,
                           dir.c_str(), model, world.c_str());
            return nullptr;
        }
        auto mesh = std::make_unique<content::Mesh>();
        if (!mesh->buildFromCooked(cooked, model, assetDir, textures)) return nullptr;
        meshes_.push_back(std::move(mesh));
        return meshes_.back().get();
    };
    if (world == "losttower") {
        plate_ = load("Object26");
        return true;
    }
    lance_ = load("Object40");
    stick_ = load("Object41");
    fire_ = load("Object52");
    saw_ = load("Saw01");
    stone_ = load("DungeonStone01");
    if (const content::EffectSheet* sheet = table.effect("fire2")) {
        fire2_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    } else {
        core::logError("traps: no cooked effect named 'fire2', so the Fire Trap throws nothing");
    }

    // The Iron Stick's strike, as a figure: its clip is baked from the same glb in the same joint
    // order, so the bones map one to one (as Boids' body).
    if (stick_ != nullptr && stick_->isSkinned()) {
        std::vector<uint8_t> clipBytes = core::readFile(core::join(dir, "clips/Object41.muc"));
        auto library = std::make_unique<ClipLibrary>();
        std::string error;
        if (!clipBytes.empty() && content::parseCookedClips(clipBytes, library->clips, error) &&
            !library->clips.clips.empty()) {
            auto body = std::make_unique<FigureBody>();
            body->name = "Object41";
            body->parts.push_back(stick_);
            body->skeletonMesh = stick_;
            body->library = library.get();
            const size_t bones = std::min(stick_->bones().size(), size_t(library->clips.bones));
            body->clipBoneOf.assign(stick_->bones().size(), -1);
            for (size_t i = 0; i < bones; ++i) body->clipBoneOf[i] = int32_t(i);
            body->idleClip = 0;
            const content::Bounds& box = stick_->bounds();
            for (int axis = 0; axis < 3; ++axis) {
                body->min[axis] = box.min[axis];
                body->max[axis] = box.max[axis];
            }
            body->radius = box.radius;
            library_ = std::move(library);
            body_ = std::move(body);
            scratch_.assign(size_t(gfx::Renderer::kMaxBones) * 12, 0.0f);
        } else {
            core::logError("traps: Object41's strike did not load, so the Iron Sticks stand still");
        }
    }
    core::logf("traps: %zu of 4 models, the Iron Stick %s", meshes_.size(),
               body_ ? "strikes" : "does not strike");
    return true;
}

void TrapShow::shutdown() {
    stood_.clear();
    figures_.clear();
    body_.reset();
    library_.reset();
    for (auto& mesh : meshes_) mesh->shutdown();
    meshes_.clear();
    lance_ = stick_ = fire_ = saw_ = stone_ = plate_ = nullptr;
    ground_ = nullptr;
    stones_.clear();
    fire2_ = BGFX_INVALID_HANDLE;  // the Textures cache owns it
    saws_.clear();
    jets_.clear();
    flames_.clear();
    scratch_.clear();
}

void TrapShow::stand(const std::vector<sim::Realm::Trap>& traps, const content::Ground& ground) {
    stood_.clear();
    figures_.clear();
    ground_ = &ground;
    if (!isOpen()) return;
    const float perTile = std::max(ground.metresPerTile(), 0.001f);
    size_t sticks = 0;
    for (const sim::Realm::Trap& trap : traps) sticks += trap.number == 101 ? 1 : 0;
    figures_.resize(body_ ? sticks : 0);
    size_t figure = 0;
    for (const sim::Realm::Trap& trap : traps) {
        Stood one;
        one.number = trap.number;
        one.mesh = trap.number == 100   ? lance_
                   : trap.number == 101 ? stick_
                   : trap.number == 103 ? plate_
                                        : fire_;
        one.position[0] = (float(trap.column) + 0.5f) * perTile;
        one.position[2] = -(float(trap.row) + 0.5f) * perTile;
        one.position[1] = ground.heightAt(one.position[0], one.position[2]);
        one.yaw = std::atan2(float(trap.dx), float(-trap.dy));
        // The way it fires in the world: +column is +x, +row is -z.
        one.facing[0] = float(trap.dx);
        one.facing[1] = -float(trap.dy);
        ground.lightAt(trap.column, trap.row, one.light);
        // **Ours** (the user, 2026-10-01: "trat trap is very good vissible, need to integrate it
        // better on world"): the Meteorite Trap's plate sunk to lie flush with the floor, its
        // 9 cm edge below the ground, and lit a little under the floor round it, so it reads as a
        // carved tile of the floor rather than a slab laid on it.
        if (trap.number == 103) {
            one.position[1] -= kPlateSink;
            for (float& channel : one.light) channel *= kPlateShade;
        }
        if (trap.number == 101 && body_ && figure < figures_.size()) {
            one.figure = int(figure);
            Figure& f = figures_[figure++];
            f.stand(body_.get(), one.position, one.yaw, 1.0f);
            f.play(body_->idleClip);
            f.setClock(0.0f);
        }
        stood_.push_back(one);
    }
}

void TrapShow::fired(size_t index, float at[3]) {
    if (index >= stood_.size()) return;
    Stood& one = stood_[index];
    for (int k = 0; k < 3; ++k) at[k] = one.position[k];
    if (one.figure >= 0) {
        Figure& f = figures_[size_t(one.figure)];
        f.setClock(0.0f);
        one.striking = f.length() / kStrikeRate;
    }
    // CreateEffect(MODEL_SAW): 130 units up, and away along the trap's angle.
    if (one.number == 100 && saw_ != nullptr) {
        Saw saw;
        for (int k = 0; k < 3; ++k) saw.position[k] = one.position[k];
        saw.position[1] += 130.0f * kUnit;
        saw.along[0] = one.facing[0];
        saw.along[1] = one.facing[1];
        saw.spin = one.yaw * 180.0f / kPi;
        saw.life = 10.0f;
        for (int k = 0; k < 3; ++k) saw.light[k] = one.light[k];
        saws_.push_back(saw);
    }
    // CreateEffect(BITMAP_FIRE + 1): 60 units out of the vent and 130 up, for ten frames.
    if (one.number == 102) {
        Jet jet;
        jet.position[0] = one.position[0] + one.facing[0] * 60.0f * kUnit;
        jet.position[1] = one.position[1] + 130.0f * kUnit;
        jet.position[2] = one.position[2] + one.facing[1] * 60.0f * kUnit;
        jet.along[0] = one.facing[0];
        jet.along[1] = one.facing[1];
        jet.life = 10.0f;
        jet.owed = 1.0f;
        jets_.push_back(jet);
    }
}

bool TrapShow::where(size_t index, float at[3], float facing[2], int32_t* number) const {
    if (index >= stood_.size()) return false;
    const Stood& one = stood_[index];
    for (int k = 0; k < 3; ++k) at[k] = one.position[k];
    facing[0] = one.facing[0];
    facing[1] = one.facing[1];
    *number = one.number;
    return true;
}

void TrapShow::update(float seconds, const float hero[3], gfx::Renderer& renderer) {
    const float factor = seconds * kFrames;
    // The ceiling: `if (rand_fps_check(3)) CreateEffect(MODEL_DUNGEON_STONE01, ...)` at each
    // marker (ZzzObject.cpp:3874-3879), born (+-32, -(50..81), 200..327) off it turned by its
    // angle, 24 to 39 frames of life, scale 0.6 to 1.3, falling at 0 to 3 units a frame
    // (ZzzEffect.cpp:2875-2882).
    const auto roll = [&]() {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return seed_;
    };
    if (stone_ != nullptr && ground_ != nullptr) {
        stoneOwed_ += factor;
        while (stoneOwed_ >= 1.0f) {
            stoneOwed_ -= 1.0f;
            for (const Marker& m : kMarkers) {
                const float mx = m.x * kUnit, mz = -m.y * kUnit;
                const float dx = mx - hero[0], dz = mz - hero[2];
                if (dx * dx + dz * dz > kStoneReach * kStoneReach) continue;
                if (roll() % kStoneOdds != 0u || stones_.size() >= kMostStones) continue;
                const float px = float(int(roll() % 64u) - 32);
                const float py = -float(roll() % 32u + 50u);
                const float pz = float(roll() % 128u + 200u);
                const float a = m.angle * kPi / 180.0f;
                const float ox = px * std::cos(a) - py * std::sin(a);
                const float oy = px * std::sin(a) + py * std::cos(a);
                Stone stone;
                stone.position[0] = (m.x + ox) * kUnit;
                stone.position[1] = (m.z + pz) * kUnit;
                stone.position[2] = -(m.y + oy) * kUnit;
                stone.gravity = -float(roll() % 4u);
                stone.life = float(roll() % 16u + 24u);
                stone.scale = float(roll() % 8u + 6u) * 0.1f;
                stone.yaw = a;
                const float perTile = std::max(ground_->metresPerTile(), 0.001f);
                ground_->lightAt(int(stone.position[0] / perTile), int(-stone.position[2] / perTile),
                                 stone.light);
                stones_.push_back(stone);
            }
        }
    }
    // Move_MODEL_DUNGEON_STONE01: a unit a frame of gravity; off the ground at 0.4 of its speed,
    // each bounce taking four frames off its life.
    for (Stone& stone : stones_) {
        stone.position[1] += stone.gravity * kUnit * factor;
        stone.gravity -= 1.0f * factor;
        const float floor = ground_ ? ground_->heightAt(stone.position[0], stone.position[2]) : 0.0f;
        if (stone.position[1] < floor) {
            stone.position[1] = floor;
            stone.gravity = -stone.gravity * 0.4f;
            stone.life -= 4.0f * factor;
        }
        stone.life -= factor;
    }
    stones_.erase(std::remove_if(stones_.begin(), stones_.end(),
                                 [](const Stone& s) { return s.life <= 0.0f; }),
                  stones_.end());
    // The saw: MoveParticle along its Direction, 60 units a frame, and Move_MODEL_SAW's
    // `Angle[2] -= 30` a frame.
    for (Saw& saw : saws_) {
        saw.position[0] += saw.along[0] * 60.0f * kUnit * factor;
        saw.position[2] += saw.along[1] * 60.0f * kUnit * factor;
        saw.spin -= 30.0f * factor;
        saw.life -= factor;
    }
    saws_.erase(std::remove_if(saws_.begin(), saws_.end(), [](const Saw& s) { return s.life <= 0.0f; }),
                saws_.end());
    // The fire: one particle a frame from each emitter, thrown on at (32 to 39) * 0.3 units a
    // frame and quickening 1.05 a frame, three frames long.
    for (Jet& jet : jets_) {
        jet.owed += factor;
        while (jet.owed >= 1.0f && jet.life > 0.0f) {
            jet.owed -= 1.0f;
            seed_ ^= seed_ << 13;
            seed_ ^= seed_ >> 17;
            seed_ ^= seed_ << 5;
            const float speed = float(seed_ % 8u + 32u) * 0.3f * kUnit;
            Flame flame;
            for (int k = 0; k < 3; ++k) flame.position[k] = jet.position[k];
            flame.velocity[0] = jet.along[0] * speed;
            flame.velocity[1] = jet.along[1] * speed;
            flame.life = 3.0f;
            flames_.push_back(flame);
        }
        jet.life -= factor;
    }
    jets_.erase(std::remove_if(jets_.begin(), jets_.end(), [](const Jet& j) { return j.life <= 0.0f; }),
                jets_.end());
    for (Flame& flame : flames_) {
        const float quicken = std::pow(1.05f, factor);
        flame.velocity[0] *= quicken;
        flame.velocity[1] *= quicken;
        flame.position[0] += flame.velocity[0] * factor;
        flame.position[2] += flame.velocity[1] * factor;
        flame.life -= factor;
    }
    flames_.erase(std::remove_if(flames_.begin(), flames_.end(),
                                 [](const Flame& f) { return f.life <= 0.0f; }),
                  flames_.end());

    for (Stood& one : stood_) {
        if (one.figure < 0) continue;
        Figure& f = figures_[size_t(one.figure)];
        if (one.striking > 0.0f) {
            one.striking -= seconds;
            f.update(seconds, kStrikeRate);
            if (one.striking <= 0.0f) f.setClock(0.0f);
        } else {
            f.update(seconds, 0.0f);
        }
        const int posed = f.pose(scratch_.data());
        one.paletteRow = posed > 0 ? renderer.addPalette(scratch_.data(), posed) : -1;
    }
}

void TrapShow::gatherEffects(gfx::Effects& effects) const {
    if (!bgfx::isValid(fire2_)) return;
    for (const Flame& flame : flames_) {
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = flame.position[k];
        // A 64 square at Scale 1: 0.64 m of flame.
        sprite.halfWidth = sprite.halfHeight = 64.0f * kUnit * 0.5f;
        // `Luminosity = LifeTime * 0.2` as its Light: 0.6, 0.4, 0.2 over its three frames.
        const float light = std::clamp(flame.life, 0.0f, 3.0f) * 0.2f;
        sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = light;
        sprite.sheet = fire2_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

uint32_t TrapShow::lights(gfx::PointLight* out, uint32_t max) const {
    uint32_t count = 0;
    for (const Jet& jet : jets_) {
        if (count >= max) break;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = jet.position[k];
        light.reach = 2.0f;  // two tiles
        light.height = 1.3f;
        // `Luminosity` is the effect's, which a burst holds at one; faded out over its last
        // frames so the floor does not snap dark.
        const float level = std::clamp(jet.life / 3.0f, 0.0f, 1.0f);
        light.colour[0] = 1.0f * level;
        light.colour[1] = 0.6f * level;
        light.colour[2] = 0.3f * level;
    }
    return count;
}

void TrapShow::gather(std::vector<gfx::Drawable>& out) const {
    for (const Stone& stone : stones_) {
        gfx::Drawable drawable;
        drawable.mesh = stone_;
        content::placementTransform(0.0f, stone.yaw, 0.0f, stone.scale, stone.position,
                                    drawable.transform);
        for (int k = 0; k < 3; ++k) drawable.light[k] = stone.light[k];
        out.push_back(drawable);
    }
    for (const Saw& saw : saws_) {
        gfx::Drawable drawable;
        drawable.mesh = saw_;
        content::placementTransform(0.0f, saw.spin * kPi / 180.0f, 0.0f, 1.0f, saw.position,
                                    drawable.transform);
        for (int k = 0; k < 3; ++k) drawable.light[k] = saw.light[k];
        out.push_back(drawable);
    }
    for (const Stood& one : stood_) {
        if (one.mesh == nullptr) continue;
        const size_t first = out.size();
        if (one.figure >= 0 && one.paletteRow >= 0) {
            figures_[size_t(one.figure)].gather(one.paletteRow, out);
        } else {
            gfx::Drawable drawable;
            drawable.mesh = one.mesh;
            content::placementTransform(0.0f, one.yaw, 0.0f, 1.0f, one.position, drawable.transform);
            out.push_back(drawable);
        }
        for (size_t at = first; at < out.size(); ++at) {
            for (int k = 0; k < 3; ++k) out[at].light[k] = one.light[k];
        }
    }
}

}  // namespace mu::game
