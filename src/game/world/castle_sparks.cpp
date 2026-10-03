#include "game/world/castle_sparks.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;             // MU's reference frame, 25 a second
constexpr float kEvery = 4.0f * kFrame;     // one frame in four
constexpr float kLife = 60.0f * kFrame;     // 2.4 s
constexpr float kTurnRadius = 0.4f;         // 40 units
constexpr float kTurnRate = 0.1f / kFrame;  // 0.1 rad a frame
constexpr float kShrink = 0.002f / kFrame;  // Scale a second
constexpr float kSheetMetres = 0.64f;       // a 64-texel sheet at Scale 1
constexpr size_t kMost = 40;
// Ours: the sparks and the emitters' flares at 0.4 of MU's size (the user, 2026-10-02: 'thos
// flying sparcles has to be much smaller'), then 0.18 ('sparkles still to big'), then 0.14 ('just little bit smaller').
constexpr float kSparkSize = 0.14f;
// The emitters: a puff about one frame in eight each (rand_fps_check(2) on a four-tick
// cadence), a flare with every other.
constexpr float kPuffEvery = 8.0f * kFrame;
constexpr float kPuffLifeMin = 20.0f * kFrame, kPuffLifeMax = 30.0f * kFrame;
constexpr float kPuffClimb = 0.6f * 25.0f * 25.0f / 100.0f;  // 0.6 units a frame per frame, m/s²
// Ours: faint, and fainter again after 'that bworn smoke is not nice' and 'neet to make the mo
// resubtle' (2026-10-02; the brown was smoke02, since smoke01).
constexpr float kPuffLevel = 0.12f;
constexpr float kPuffColour[3] = {0.80f, 0.84f, 0.95f};
constexpr float kFlareLife = 40.0f * kFrame;
constexpr size_t kMostPuffs = 160;
// The drawbridge's dust as it lands. MU throws ten BITMAP_SMOKE+1 puffs at its tip, 150 units
// either way, on a float equality its swing never meets (ZzzObject.cpp:144-153), so in MU it
// never shows; ours, asked for (the user, 2026-10-03: 'we need some minimal smoke effect on
// landing'): grey, slow, about a second and a half.
constexpr float kDustLevel = 0.16f;
constexpr float kDustColour[3] = {0.78f, 0.78f, 0.80f};

}  // namespace

float CastleSparks::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void CastleSparks::open(const std::string& assetDir, const std::string& world,
                        const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (world != "bloodcastle") return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("flare"))
            sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        // MU's white BITMAP_SMOKE: smoke02, brown on black, stood as square brown boxes added.
        if (const content::EffectSheet* sheet = table.effect("smoke01"))
            smoke_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model != content::TownEmitter::kWorld || one.kind != content::EmitterKind::Smoke)
            continue;
        Vent vent;
        for (int k = 0; k < 3; ++k) vent.at[k] = one.at[k];
        vent.owed = unit() * kPuffEvery;
        vents_.push_back(vent);
    }
    open_ = true;
    sparks_.reserve(kMost);
    puffs_.reserve(kMostPuffs);
    core::logf("castle sparks: flare %s; %zu mist emitters, smoke01 %s",
               bgfx::isValid(sheet_) ? "yes" : "NO", vents_.size(), bgfx::isValid(smoke_) ? "yes" : "NO");
}

void CastleSparks::shutdown() {
    open_ = false;
    sparks_.clear();
    puffs_.clear();
    vents_.clear();
    smoke_ = BGFX_INVALID_HANDLE;
    owed_ = 0.0f;
    sheet_ = BGFX_INVALID_HANDLE;
}

void CastleSparks::update(float seconds, const float hero[3]) {
    if (!open_) return;
    for (Spark& one : sparks_) {
        one.age += seconds;
        one.height += one.climb * seconds;
        one.scale -= kShrink * seconds;
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(),
                                 [](const Spark& s) { return s.age >= kLife || s.scale <= 0.0f; }),
                  sparks_.end());
    for (Puff& one : puffs_) {
        one.age += seconds;
        if (one.flare) {
            one.at[1] += one.rise * seconds;
        } else if (one.dust) {
            one.at[0] += one.drift[0] * seconds;
            one.at[1] += one.rise * seconds;
            one.at[2] += one.drift[1] * seconds;
            one.drift[0] *= std::pow(0.9f, seconds / kFrame);
            one.drift[1] *= std::pow(0.9f, seconds / kFrame);
        } else {
            one.rise += kPuffClimb * seconds;
            one.at[1] += one.rise * seconds;
            one.at[0] += one.drift[0] * seconds;
            one.at[2] += one.drift[1] * seconds;
            one.drift[0] *= std::pow(0.95f, seconds / kFrame);
            one.drift[1] *= std::pow(0.95f, seconds / kFrame);
        }
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) { return p.age >= p.life; }),
                 puffs_.end());
    for (Vent& vent : vents_) {
        vent.owed += seconds;
        while (vent.owed >= kPuffEvery) {
            vent.owed -= kPuffEvery;
            if (puffs_.size() >= kMostPuffs) continue;
            Puff puff;
            for (int k = 0; k < 3; ++k) puff.at[k] = vent.at[k];
            puff.rise = (unit() * 10.0f + 5.0f) * 0.2f * 0.25f;
            puff.drift[0] = (unit() * 10.0f + 5.0f) * 0.4f * 0.25f;
            puff.drift[1] = -(unit() * 10.0f - 5.0f) * 0.4f * 0.25f;
            puff.life = kPuffLifeMin + (kPuffLifeMax - kPuffLifeMin) * unit();
            puff.size = 0.32f + unit() * 0.1f;
            puff.spin = unit() * 6.2831853f;
            puffs_.push_back(puff);
            if ((vent.count++ & 1) == 0 && puffs_.size() < kMostPuffs) {
                Puff flare;
                for (int k = 0; k < 3; ++k) flare.at[k] = vent.at[k];
                flare.flare = true;
                flare.rise = (1.0f + unit() * 5.0f) * 0.25f;
                flare.life = kFlareLife;
                flare.size = 0.5f * kSheetMetres * 0.19f * kSparkSize;
                puffs_.push_back(flare);
            }
        }
    }
    owed_ += seconds;
    while (owed_ >= kEvery) {
        owed_ -= kEvery;
        if (sparks_.size() >= kMost) continue;
        Spark one;
        // MU's x is our x and its y our -z (docs/conventions.md): -3 to +6 m in each.
        one.start[0] = hero[0] + (unit() * 9.0f - 3.0f);
        one.start[2] = hero[2] - (unit() * 9.0f - 3.0f);
        one.start[1] = hero[1] + 2.5f + unit() * 0.5f;
        one.climb = (1.0f + unit() * 4.0f) * 0.01f / kFrame;
        one.phase = unit() * 300.0f - 150.0f;
        one.scale = 0.19f + float(int(unit() * 6.0f)) * 0.01f;
        sparks_.push_back(one);
    }
}

void CastleSparks::dust(const float at[3], int count, float spread) {
    if (!open_ || !bgfx::isValid(smoke_)) return;
    for (int i = 0; i < count && puffs_.size() < kMostPuffs; ++i) {
        Puff puff;
        const float across = (unit() * 2.0f - 1.0f) * spread;
        puff.at[0] = at[0] + across;
        puff.at[1] = at[1] + 0.1f + unit() * 0.2f;
        puff.at[2] = at[2] + (unit() - 0.5f) * 0.4f;
        puff.rise = 0.25f + unit() * 0.35f;
        // Out from the line, as dust pushed from under a falling board.
        puff.drift[0] = across * 0.6f;
        puff.drift[1] = (unit() - 0.3f) * 1.2f;
        puff.life = 1.2f + unit() * 0.6f;
        puff.size = 0.45f + unit() * 0.2f;
        puff.spin = unit() * 6.2831853f;
        puff.dust = true;
        puffs_.push_back(puff);
    }
}

void CastleSparks::gather(gfx::Effects& effects) const {
    if (!open_) return;
    for (const Puff& one : puffs_) {
        const float t = one.age / one.life;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        if (one.flare) {
            if (!bgfx::isValid(sheet_)) continue;
            sprite.halfWidth = sprite.halfHeight = one.size * (1.0f - 0.4f * t);
            const float level = (1.0f - t) * 0.5f;
            sprite.colour[0] = level;
            sprite.colour[1] = 0.8f * level;
            sprite.colour[2] = 0.8f * level;
            sprite.sheet = sheet_;
        } else {
            if (!bgfx::isValid(smoke_)) continue;
            // MU's Scale grows 0.05-0.09 a frame from 0.5; a 64-unit sheet at that, in metres.
            sprite.halfWidth = sprite.halfHeight = one.size * (1.0f + 2.5f * t);
            const float level = (one.dust ? kDustLevel : kPuffLevel) * std::sin(3.14159265f * t);
            for (int k = 0; k < 3; ++k) {
                sprite.colour[k] = (one.dust ? kDustColour[k] : kPuffColour[k]) * level;
            }
            sprite.spin = one.spin + t;
            sprite.sheet = smoke_;
        }
        sprite.colour[3] = 1.0f;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    if (!bgfx::isValid(sheet_)) return;
    for (const Spark& one : sparks_) {
        // MU's count runs on the life it has left: (Velocity[0] + LifeTime) * 0.1.
        const float count = one.phase * 0.1f + (kLife - one.age) * kTurnRate;
        gfx::Sprite sprite;
        sprite.position[0] = one.start[0] + std::sin(count) * kTurnRadius;
        sprite.position[2] = one.start[2] + std::cos(count) * kTurnRadius;
        sprite.position[1] = one.start[1] + one.height;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * one.scale * kSparkSize;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = 1.0f;
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
