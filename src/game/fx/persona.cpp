#include "game/fx/persona.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;           // MU's reference frame
constexpr float kUnit = 0.01f;            // a MU unit, in metres
constexpr float kSprite = 0.64f * 0.33f;  // the sheets are 64 texels; ours at a third
constexpr float kBrowScale = 2.0f;
constexpr float kBrowLight[3] = {0.5f, 0.6f, 1.0f};
constexpr float kSmokeLight[3] = {0.5f, 0.5f, 1.0f};
constexpr float kDripLight = 0.5f;
constexpr float kDripScale = 0.8f;
constexpr float kSwingKey = 3.0f;
constexpr const char* kBrow = "a1";                                    // PRSona_A1
constexpr const char* kTail[2] = {"Bip01 Tail", "Bip01 Tail1"};        // PRSona_Tail, _Tail1
constexpr size_t kMostPuffs = 600;

}  // namespace

float Persona::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool Persona::open(const std::string& assetDir, content::Textures& textures,
                   const content::Showing& table) {
    shutdown();
    const auto cooked = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    light_ = cooked("light");            // BITMAP_LIGHT, Effect/flare01
    smoke_ = cooked("smoke01");          // BITMAP_SMOKE
    water_ = cooked("waterfall_spray");  // BITMAP_WATERFALL_3, Effect/WATERFALL3
    core::logf("persona: light %s, smoke %s, water %s", bgfx::isValid(light_) ? "yes" : "NO",
               bgfx::isValid(smoke_) ? "yes" : "NO", bgfx::isValid(water_) ? "yes" : "NO");
    return bgfx::isValid(light_);
}

void Persona::shutdown() {
    ones_.clear();
    puffs_.clear();
    light_ = smoke_ = water_ = BGFX_INVALID_HANDLE;
}

void Persona::drip(const float at[3], int kind, float fade) {
    if (puffs_.size() >= kMostPuffs) return;
    Puff one;
    one.kind = kind;
    one.fade = fade;
    one.life = 20.0f;
    one.light = kDripLight;
    one.gravity = std::floor(unit() * 100.0f) / 100.0f * 2.0f;
    one.scale = (std::floor(unit() * 10.0f) + 10.0f) * 0.02f + kDripScale;
    one.spin = unit() * 6.2832f;
    // MU's x and y across, its z up: ours x and z across, y up.
    one.at[0] = at[0] + (std::floor(unit() * 40.0f) - 20.0f) * kUnit;
    one.at[2] = at[2] + (std::floor(unit() * 40.0f) - 20.0f) * kUnit;
    one.at[1] = at[1] - 50.0f * kUnit;
    puffs_.push_back(one);
}

Persona::Asks Persona::watch(uint32_t id, const Figure& figure, int attack, float key, bool dying,
                             const float feet[3], float fade, float seconds) {
    Asks asks;
    One& one = ones_[id];
    one.seen = true;
    one.bright = 0.0f;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return asks;
    if (!one.found) {
        one.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (size_t b = 0; b < bones.size(); ++b) {
            if (bones[b].name == kBrow) one.brow = int(b);
            for (int k = 0; k < 2; ++k)
                if (bones[b].name == kTail[k]) one.tails[k] = int(b);
        }
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float tails[2][3];
    bool has[2];
    for (int k = 0; k < 2; ++k) has[k] = one.tails[k] >= 0 && figure.pointOn(one.tails[k], origin, tails[k]);

    // The swing's one storm or one release of spirits, as its key passes 3.
    if (attack == 0 || dying) {
        one.swung = 0;
    } else if (one.swung != attack && key >= kSwingKey) {
        one.swung = attack;
        (attack == 1 ? asks.storm : asks.spirits) = true;
    }

    // The light on its brow, every frame while it stands.
    if (!dying && one.brow >= 0 && figure.pointOn(one.brow, origin, one.brightAt)) {
        one.bright = (std::floor(unit() * 30.0f) + 70.0f) * 0.01f;
        one.fade = fade;
    }

    for (one.owed += seconds / kFrame; one.owed >= 1.0f; one.owed -= 1.0f) {
        if (dying) {
            for (int k = 0; k < 2; ++k)
                if (has[k]) drip(tails[k], 6, fade);
            continue;
        }
        // One frame in five, the blue smoke round its feet.
        if (unit() < 0.2f && puffs_.size() < kMostPuffs) {
            Puff puff;
            puff.fade = fade;
            puff.life = 50.0f;
            puff.scale = (std::floor(unit() * 32.0f) + 80.0f) * 0.01f;
            puff.spin = unit() * 6.2832f;
            puff.at[0] = feet[0] + (std::floor(unit() * 64.0f) - 32.0f) * kUnit;
            puff.at[2] = feet[2] + (std::floor(unit() * 64.0f) - 32.0f) * kUnit;
            puff.at[1] = feet[1] + (std::floor(unit() * 64.0f) + 32.0f) * kUnit;
            const float yaw = unit() * 6.2832f;
            const float pitch = (unit() * 90.0f - 45.0f) * 0.01745329f;
            const float speed = (std::floor(unit() * 8.0f) + 40.0f) * kUnit;
            puff.speed[0] = std::sin(yaw) * std::cos(pitch) * speed;
            puff.speed[2] = std::cos(yaw) * std::cos(pitch) * speed;
            puff.speed[1] = std::sin(pitch) * speed;
            puffs_.push_back(puff);
        }
        // One frame in three, a drip under each tail.
        if (unit() < 1.0f / 3.0f)
            for (int k = 0; k < 2; ++k)
                if (has[k]) drip(tails[k], 5, fade);
    }
    return asks;
}

void Persona::update(float seconds) {
    for (auto it = ones_.begin(); it != ones_.end();) {
        if (!it->second.seen) it = ones_.erase(it);
        else (it++)->second.seen = false;
    }
    const float frames = std::min(seconds, 0.25f) / kFrame;
    for (Puff& one : puffs_) {
        one.age += frames;
        if (one.kind == 0) {
            for (int i = 0; i < 3; ++i) one.at[i] += one.speed[i] * frames;
            const float slow = std::pow(0.4f, frames);
            for (float& s : one.speed) s *= slow;
            one.scale += 0.05f * frames;
            one.light = std::max(0.0f, 1.0f - one.age / one.life);
        } else {
            const float sign = one.kind == 6 ? 1.0f : -1.0f;
            one.at[1] += sign * one.gravity * kUnit * frames;
            one.gravity += sign * 0.05f * frames;
            one.light *= std::pow(1.0f / 1.1f, frames);
        }
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) { return p.age >= p.life; }),
                 puffs_.end());
}

void Persona::gather(gfx::Effects& effects) const {
    const auto add = [&](bgfx::TextureHandle sheet, const float at[3], float half, const float colour[3],
                         float light, float spin) {
        if (!bgfx::isValid(sheet) || light <= 0.0f) return;
        gfx::Sprite sprite;
        sprite.sheet = sheet;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = at[i];
            sprite.colour[i] = colour[i] * light;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = half;
        sprite.spin = spin;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };
    for (const auto& [id, one] : ones_)
        if (one.bright > 0.0f) add(light_, one.brightAt, 0.5f * kSprite * kBrowScale, kBrowLight, one.bright * one.fade, 0.0f);
    const float grey[3] = {1.0f, 1.0f, 1.0f};
    for (const Puff& one : puffs_) {
        const bool smoke = one.kind == 0;
        add(smoke ? smoke_ : water_, one.at, 0.5f * kSprite * one.scale, smoke ? kSmokeLight : grey,
            one.light * one.fade, one.spin);
    }
}

}  // namespace mu::game
