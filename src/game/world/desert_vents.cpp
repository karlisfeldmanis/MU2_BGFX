#include "game/world/desert_vents.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;           // MU's reference frame, 25 a second
constexpr float kUnit = 0.01f;            // a MU unit, in metres
constexpr float kSheetMetres = 0.64f;     // a 64-texel sheet at Scale 1
constexpr float kReach = 28.0f;           // metres from the camera's point an emitter throws
constexpr size_t kMostPuffs = 420;
// Ours: MU's smoke light times this. Its dun steam at full is a bright smear added over noon
// sand.
// 0.75: smoke01's heart is two thirds of smoke02's, so half of MU's on smoke02 is this on it.
constexpr float kPuffLevel = 0.75f;
// And the standing dust at this of MU's (0.36, 0.3, 0.24): 82 clouds of twenty over the plateaus.
constexpr float kDustLevel = 0.75f;

// SubType 7, the falling sand: 30 frames; Gravity starts at rand(0, 1) units a frame and gains
// one a frame, all of it down; Scale times 1.2-1.4 and gains 0.03 a frame; dark over the last
// five frames.
constexpr float kSandLife = 30.0f * kFrame;
constexpr float kSandEvery = 5.0f * kFrame;
constexpr float kSandColour[3] = {0.725f, 0.572f, 0.333f};
// SubType 4, the steam: 16 frames; Gravity gains 0.2 a frame, all of it up; Scale 0.48-0.8,
// gaining 0.05 a frame; light LifeTime / 8 of the dun.
constexpr float kSteamLife = 16.0f * kFrame;
constexpr float kSteamColour[3] = {120.0f / 255.0f, 100.7f / 255.0f, 80.0f / 255.0f};
// SubType 8, the plume: 24 frames; Scale 0.8 of the box's; Gravity gains 0.02 a frame, added to
// Scale and twenty times over to its height; white at LifeTime / 24, halved over the last five.
constexpr float kPlumeLife = 24.0f * kFrame;
// The windows: steam the last half second of every five, a geyser half a second of every ten
// from 3.5 s in plus its stagger.
constexpr float kSteamCycle = 5.0f, kSteamFrom = 4.5f;
constexpr float kGeyserCycle = 10.0f, kGeyserFrom = 3.5f, kGeyserFor = 0.5f;
constexpr float kGlowOver = 0.34f;        // bone 2 over the box's origin

float accel(float unitsPerFramePerFrame) { return unitsPerFramePerFrame * kUnit / (kFrame * kFrame); }

}  // namespace

float DesertVents::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void DesertVents::open(const std::string& assetDir, const std::string& world,
                       const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (world != "tarkan") return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        // smoke01, not the showing's `smoke` (smoke02): smoke02 is bright out to its square
        // edges, and added it stood as blocks (the user, 2026-10-05: 'this smoke effect is little
        // bit pixelated'), as Blood Castle's mist found. smoke01 falls to black at its rim.
        if (const content::EffectSheet* sheet = table.effect("smoke01"))
            smoke_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        if (const content::EffectSheet* sheet = table.effect("impact"))
            impact_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    size_t counts[5] = {0, 0, 0, 0, 0};
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model != content::TownEmitter::kWorld) continue;
        const int k = int(one.kind) - int(content::EmitterKind::SandFall);
        if (k < 0 || k > 4) continue;
        if (one.kind == content::EmitterKind::DustCloud) {
            // MU's twenty, scattered as SubType 6's creation does: +-1 m (times the box's scale,
            // which an anchor does not carry), 20-40 units up.
            for (int i = 0; i < 20; ++i) {
                Mote mote;
                mote.at[0] = one.at[0] + (unit() * 2.0f - 1.0f);
                mote.at[1] = one.at[1] + 0.2f + unit() * 0.2f;
                mote.at[2] = one.at[2] + (unit() * 2.0f - 1.0f);
                mote.g = unit() * 1000.0f;
                mote.spin = unit() * 6.2831853f;
                motes_.push_back(mote);
            }
            ++counts[4];
            continue;
        }
        Vent vent;
        vent.kind = one.kind;
        for (int i = 0; i < 3; ++i) vent.at[i] = one.at[i];
        // Where it stands, hashed: MU's stagger by the box's turn.
        uint32_t h = uint32_t(std::lround(one.at[0] * 37.0f)) * 73856093u ^
                     uint32_t(std::lround(one.at[2] * 37.0f)) * 19349663u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        vent.phase = float(h % 3600u) / 1000.0f;
        if (vent.kind == content::EmitterKind::GlowSprite) vent.at[1] += kGlowOver;
        vents_.push_back(vent);
        ++counts[k];
    }
    open_ = true;
    puffs_.reserve(kMostPuffs);
    core::logf("desert vents: %zu sand falls, %zu steam vents, %zu geysers, %zu glow sprites, "
               "%zu dust clouds (%zu puffs); smoke %s, impact %s",
               counts[0], counts[1], counts[2], counts[3], counts[4], motes_.size(),
               bgfx::isValid(smoke_) ? "yes" : "NO",
               bgfx::isValid(impact_) ? "yes" : "NO");
}

void DesertVents::shutdown() {
    open_ = false;
    vents_.clear();
    puffs_.clear();
    motes_.clear();
    clock_ = 0.0f;
    smoke_ = impact_ = BGFX_INVALID_HANDLE;
}

void DesertVents::throwPuff(Kind kind, const float at[3], float scaleBy) {
    if (puffs_.size() >= kMostPuffs) return;
    Puff puff;
    puff.kind = kind;
    for (int i = 0; i < 3; ++i) puff.at[i] = at[i];
    puff.spin = unit() * 6.2831853f;
    puff.half = scaleBy;
    switch (kind) {
        case Kind::Sand:
            puff.life = kSandLife;
            puff.speed = -unit() * kUnit / kFrame;
            puff.scale = 1.2f + unit() * 0.2f;
            break;
        case Kind::Steam:
            puff.life = kSteamLife;
            puff.scale = 0.48f + unit() * 0.32f;
            break;
        case Kind::Plume:
            puff.life = kPlumeLife;
            puff.scale = 0.8f;
            break;
    }
    puffs_.push_back(puff);
}

void DesertVents::update(float seconds, const float near[3],
                         const std::function<void(const float*)>& stone) {
    if (!open_) return;
    clock_ += seconds;
    for (int i = 0; i < 3; ++i) near_[i] = near[i];
    const float frames = seconds / kFrame;
    for (Puff& one : puffs_) {
        one.age += seconds;
        switch (one.kind) {
            case Kind::Sand:
                one.speed -= accel(1.0f) * seconds;
                one.scale += 0.03f * frames;
                break;
            case Kind::Steam:
                one.speed += accel(0.2f) * seconds;
                one.scale += 0.05f * frames;
                break;
            case Kind::Plume:
                // Gravity gains 0.02 a frame; Scale gains Gravity, the height twenty Gravities.
                if (one.age < one.life - 5.0f * kFrame) {
                    const float gravity = 0.02f * (one.age / kFrame);
                    one.scale += gravity * frames;
                    one.speed = gravity * 20.0f * kUnit / kFrame;
                } else {
                    one.speed = 0.0f;
                }
                break;
        }
        one.at[1] += one.speed * seconds;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) { return p.age >= p.life; }),
                 puffs_.end());

    for (Vent& vent : vents_) {
        if (vent.kind == content::EmitterKind::GlowSprite) continue;
        const float dx = vent.at[0] - near[0], dz = vent.at[2] - near[2];
        if (dx * dx + dz * dz > kReach * kReach) continue;
        vent.owed += seconds;
        const bool sand = vent.kind == content::EmitterKind::SandFall;
        const float every = sand ? kSandEvery : kFrame;
        while (vent.owed >= every) {
            vent.owed -= every;
            if (sand) {
                throwPuff(Kind::Sand, vent.at, 1.0f);
            } else if (vent.kind == content::EmitterKind::SteamVent) {
                if (std::fmod(clock_, kSteamCycle) > kSteamFrom) throwPuff(Kind::Steam, vent.at, 1.0f);
            } else {
                const float t = std::fmod(clock_, kGeyserCycle) - vent.phase;
                if (t <= kGeyserFrom || t >= kGeyserFrom + kGeyserFor) continue;
                throwPuff(Kind::Plume, vent.at, 1.0f);
                if ((vent.frames++ % 3) == 0) {
                    float side[3] = {vent.at[0] + (unit() * 1.28f - 0.64f), vent.at[1],
                                     vent.at[2] + (unit() * 1.28f - 0.64f)};
                    throwPuff(Kind::Steam, side, 0.5f);
                    if (stone) stone(vent.at);
                }
            }
        }
    }
}

void DesertVents::gather(gfx::Effects& effects) const {
    if (!open_) return;
    if (bgfx::isValid(smoke_)) {
        for (const Puff& one : puffs_) {
            const float left = (one.life - one.age) / kFrame;  // MU's LifeTime, frames
            float colour[3];
            float level = 1.0f;
            switch (one.kind) {
                case Kind::Sand:
                    level = left < 5.0f ? left / 8.0f : 1.0f;
                    for (int i = 0; i < 3; ++i) colour[i] = kSandColour[i];
                    break;
                case Kind::Steam:
                    level = std::min(left / 8.0f, 1.0f);
                    for (int i = 0; i < 3; ++i) colour[i] = kSteamColour[i];
                    break;
                case Kind::Plume:
                    level = left > 5.0f ? left / 24.0f : 2.5f / 24.0f;
                    for (int i = 0; i < 3; ++i) colour[i] = 1.0f;
                    break;
            }
            gfx::Sprite sprite;
            for (int i = 0; i < 3; ++i) {
                sprite.position[i] = one.at[i];
                sprite.colour[i] = colour[i] * level * kPuffLevel;
            }
            sprite.colour[3] = 1.0f;
            sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * one.scale * one.half;
            sprite.spin = one.spin;
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    // The standing dust: only within reach of the camera's point, as the vents throw.
    if (bgfx::isValid(smoke_)) {
        const float ms = clock_ * 1000.0f;
        for (const Mote& one : motes_) {
            const float dx = one.at[0] - near_[0], dz = one.at[2] - near_[2];
            if (dx * dx + dz * dz > kReach * kReach) continue;
            const float scale =
                std::sin(std::fmod(one.g + ms, 1800.0f) * 0.1f * 3.14159265f / 180.0f) * 0.5f + 1.8f;
            gfx::Sprite sprite;
            sprite.position[0] = one.at[0];
            sprite.position[1] = one.at[1] + std::sin((ms + one.g) / 5000.0f) * 0.2f;
            sprite.position[2] = one.at[2];
            const float lit[3] = {0.36f, 0.30f, 0.24f};
            for (int i = 0; i < 3; ++i) sprite.colour[i] = lit[i] * kDustLevel;
            sprite.colour[3] = 1.0f;
            sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * scale;
            sprite.spin = one.spin;
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    if (!bgfx::isValid(impact_)) return;
    for (const Vent& vent : vents_) {
        if (vent.kind != content::EmitterKind::GlowSprite) continue;
        const float l = std::sin((clock_ + vent.phase) * 2.0f) * 0.3f + 0.7f;
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = vent.at[i];
        sprite.colour[0] = l / 1.7f;
        sprite.colour[1] = l;
        sprite.colour[2] = l;
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * 1.5f * l;
        sprite.sheet = impact_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
