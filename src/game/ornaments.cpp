#include "game/ornaments.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"
#include "game/sway.h"
#include "game/town.h"

namespace mu::game {

namespace {

// MU's 25, which is what a reference frame means.
constexpr float kFramesPerSecond = 25.0f;

// ---- the spray: every number is MU2's Spray.cs, which read each one out of MuMain --------
//
// Where the landing puff is born and how its scatter lies, in the model's own metres at its
// bind pose: bone 4 (Box03), eighty units down ITS Y -- up the fall, (0, 0.87, -0.5) in our
// axes -- which is 0.61 m over the pool and 1.19 m out from the statue. The scatter is
// `rand() % 32 - 16` in the bone's X and Z: X (0, -0.5, -0.87) and Z (1, 0, 0), square across
// the stream where it lands.
//
// The mouth's puff MU also throws, at bone 1 (Box04), twenty units down its Y, is left out as
// MU2 left it out ("reads as smoke coming off the statue's beak"); to put it back it is bone 1,
// origin (0.0591, 2.7646, -0.0295), scatter X (0, -1, 0) and Z (1, 0, 0).
constexpr int kLandingBone = 4;
constexpr float kLanding[3] = {0.0599f, 0.6111f, 1.1932f};
constexpr float kLandingAcross[2][3] = {{0.0f, -0.5f, -0.866f}, {1.0f, 0.0f, 0.0f}};
constexpr float kScatter = 0.16f;  // half of MU's 32 units

// One puff: BITMAP_SMOKE at subtype 0, smoke01.jpg. LifeTime 16 reference frames; born at
// Scale 0.48 to 0.80 and growing 0.05 a frame; `Gravity += 0.2` a frame added to its height,
// so an acceleration of 0.2 x 25 x 25 = 125 units/s/s, 1.25 m/s/s up; no velocity; turned once
// at birth. `Luminosity = LifeTime / 8` in all three channels, so full for the first half and
// a straight line to nothing over the second. Added: smoke01 is a JPEG, three components,
// and RenderParticles blends those GL_ONE, GL_ONE.
constexpr float kPuffLife = 16.0f / kFramesPerSecond;
constexpr float kPuffGrowth = 0.05f * kFramesPerSecond;  // Scale a second
constexpr float kPuffLift = 1.25f;
constexpr float kSheetMetres = 64.0f / 100.0f;  // a 64-texel sheet at Scale 1, in metres
constexpr size_t kMostPuffs = 64;

// ---- the lanterns ---------------------------------------------------------------------
//
// MODEL_MERCHANT_ANIMAL01's two BITMAP_LIGHT sprites, at bones 48 (Box04) and 57 (Box10) --
// the two lamps 3.3 m up either side of the animal's load. MU transforms `Position` through
// each bone, and in RenderObjectVisual `Position` is never set before this case: whatever the
// stack held. Read here as the bone's own origin, which is the only offset the line can
// have meant. Ours, and marked as ours.
constexpr int kLanternBones[2] = {48, 57};
constexpr float kLanternColour[3] = {0.6f, 0.3f, 0.1f};
// `Luminosity = (rand() % 30 + 70) * 0.01f`, rolled every frame MU draws. MU drew at its
// reference 25; re-rolled per OUR frame it is a strobe at the monitor's rate, so it is
// re-rolled 25 times a second. The rate is MU's frame, not an invention -- but it is a reading.
constexpr float kLanternHz = kFramesPerSecond;

// A row-vector point or direction through a 4x4, as core::mulMatrix composes them.
void through(const float* m, const float* v, float w, float* out) {
    for (int j = 0; j < 3; ++j) {
        out[j] = v[0] * m[0 * 4 + j] + v[1] * m[1 * 4 + j] + v[2] * m[2 * 4 + j] +
                 w * m[3 * 4 + j];
    }
}

}  // namespace

uint32_t Ornaments::next() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

float Ornaments::unit() { return float(next() & 0xFFFFFF) / float(0x1000000); }

bool Ornaments::open(const std::string& assetDir, const Town& town,
                     content::Textures& textures) {
    const auto& models = town.cooked().models;
    // The bind pose's model-space point, carried into the bone's own frame through its
    // inverse bind; Figure::pointOn carries it back out through the posed bone.
    const auto anchor = [&](uint32_t townIndex, const content::Mesh* mesh, int bone,
                            const float point[3], const float (*across)[3]) {
        Anchor out;
        out.townIndex = townIndex;
        if (!mesh || bone < 0 || size_t(bone) >= mesh->bones().size()) return out;
        const float* inverse = mesh->bones()[size_t(bone)].inverseBind;
        out.bone = bone;
        through(inverse, point, 1.0f, out.point);
        through(inverse, across[0], 0.0f, out.across[0]);
        through(inverse, across[1], 0.0f, out.across[1]);
        return out;
    };

    for (uint32_t i = 0; i < town.cooked().instances.size(); ++i) {
        const uint32_t model = town.cooked().instances[i].model;
        if (model >= models.size()) continue;
        const std::string& name = models[model].name;
        const content::Mesh* mesh = town.meshAt(model);
        if (name == "Waterspout01") {
            Spout spout;
            spout.anchor = anchor(i, mesh, kLandingBone, kLanding, kLandingAcross);
            if (spout.anchor.bone >= 0) spouts_.push_back(spout);
        } else if (name == "MerchantAnimal01") {
            // The bone's own origin, which is zero in its own frame: nothing to carry.
            for (int bone : kLanternBones) {
                if (!mesh || size_t(bone) >= mesh->bones().size()) continue;
                Lantern lantern;
                lantern.anchor.townIndex = i;
                lantern.anchor.bone = bone;
                lanterns_.push_back(lantern);
            }
        }
    }
    puffs_.reserve(kMostPuffs);

    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        const auto take = [&](const char* name) -> bgfx::TextureHandle {
            const content::EffectSheet* sheet = table.effect(name);
            if (sheet == nullptr) return BGFX_INVALID_HANDLE;
            return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        };
        smoke_ = take("smoke01");  // Effect/smoke01, MU's BITMAP_SMOKE
        light_ = take("light");    // Effect/flare01, MU's BITMAP_LIGHT
    }
    if (!spouts_.empty() || !lanterns_.empty()) {
        core::logf("ornaments: %zu fountain spray, %zu lanterns; sheets: smoke01 %s, light %s",
                   spouts_.size(), lanterns_.size(), bgfx::isValid(smoke_) ? "yes" : "NO",
                   bgfx::isValid(light_) ? "yes" : "NO");
    }
    return true;
}

void Ornaments::shutdown() {
    spouts_.clear();
    lanterns_.clear();
    puffs_.clear();
    smoke_ = light_ = BGFX_INVALID_HANDLE;
}

void Ornaments::update(float seconds, const Sway& sway) {
    // A long hitch would throw a second's puffs in one place at once. Capped, as the spray
    // does not need to be right about a frame nobody saw.
    const float dt = std::min(seconds, 0.1f);

    for (Puff& puff : puffs_) {
        // The lift is MU's accumulated gravity, which is an acceleration: the height gained
        // over this step is the integral of 1.25 t, not the step times the current speed.
        const float before = puff.age;
        puff.age += dt;
        puff.position[1] += 0.5f * kPuffLift * (puff.age * puff.age - before * before);
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) { return p.age >= kPuffLife; }),
                 puffs_.end());

    for (Spout& spout : spouts_) {
        const Figure* figure = sway.posedAt(spout.anchor.townIndex);
        spout.clock += dt * kFramesPerSecond;
        while (spout.clock >= 1.0f) {
            spout.clock -= 1.0f;
            // rand_fps_check(2): a coin flip every reference frame.
            if (!figure || (next() & 1u) || puffs_.size() >= kMostPuffs) continue;
            float local[3];
            const float u = (unit() * 2.0f - 1.0f) * kScatter;
            const float w = (unit() * 2.0f - 1.0f) * kScatter;
            for (int k = 0; k < 3; ++k) {
                local[k] = spout.anchor.point[k] + spout.anchor.across[0][k] * u +
                           spout.anchor.across[1][k] * w;
            }
            Puff puff;
            if (!figure->pointOn(spout.anchor.bone, local, puff.position)) continue;
            puff.scale = 0.48f + 0.32f * unit();
            puff.spin = 6.2831853f * unit();
            puffs_.push_back(puff);
        }
    }

    lanternWait_ -= seconds;
    if (lanternWait_ <= 0.0f) {
        lanternWait_ = 1.0f / kLanternHz;
        luminosity_ = float(next() % 30u + 70u) * 0.01f;
    }
}

void Ornaments::gather(gfx::Effects& effects, const Sway& sway) const {
    if (bgfx::isValid(smoke_)) {
        for (const Puff& puff : puffs_) {
            const float frames = puff.age * kFramesPerSecond;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = puff.position[k];
            sprite.halfWidth = sprite.halfHeight =
                0.5f * kSheetMetres * (puff.scale + kPuffGrowth * puff.age);
            sprite.spin = puff.spin;
            // LifeTime / 8, clamped as glColor clamps it.
            const float light = std::clamp((16.0f - frames) / 8.0f, 0.0f, 1.0f);
            sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = 1.0f;
            sprite.colour[3] = light;
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    if (bgfx::isValid(light_)) {
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        for (const Lantern& lantern : lanterns_) {
            const Figure* figure = sway.posedAt(lantern.anchor.townIndex);
            if (!figure) continue;
            gfx::Sprite sprite;
            if (!figure->pointOn(lantern.anchor.bone, origin, sprite.position)) continue;
            // CreateSprite's Scale is `Luminosity * 5` over a 64-texel sheet.
            sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * luminosity_ * 5.0f;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = kLanternColour[k] * luminosity_;
            sprite.colour[3] = 1.0f;
            sprite.sheet = light_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
