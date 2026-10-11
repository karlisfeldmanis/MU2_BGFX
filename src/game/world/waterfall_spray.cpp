#include "game/world/waterfall_spray.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;        // MU's reference frame, 25 a second
constexpr float kUnit = 0.01f;         // a MU unit, in metres
constexpr float kSheetMetres = 0.64f;  // a 64-texel sheet at Scale 1; all three are 64
constexpr float kReach = 28.0f;        // metres from the camera's point a box throws
constexpr size_t kMostDrops = 1200;
constexpr float kTau = 6.2831853f;

// Per kind, in MU's frames and units (waterfall_spray.h has the lines).
constexpr float kDropsLife = 30.0f, kBurstLife = 20.0f, kMistLife = 40.0f;
constexpr float kDropsLight = 0.2f, kBurstLight = 1.0f, kMistLight = 0.4f;

float perSecond(float unitsPerFrame) { return unitsPerFrame * kUnit / kFrame; }
float accel(float unitsPerFramePerFrame) { return unitsPerFramePerFrame * kUnit / (kFrame * kFrame); }

// MU's light after `age` frames of its per-frame multiplications, in closed form.
float lightOf(uint8_t kind, float age, float life) {
    const float left = life - age;
    switch (kind) {
        case 0: {  // Drops: 1.1 a frame while LifeTime > 20, then held, 1/1.2 a frame under 5
            float level = kDropsLight * std::pow(1.1f, std::min(age, life - 20.0f));
            if (left < 5.0f) level *= std::pow(1.0f / 1.2f, 5.0f - left);
            return level;
        }
        case 1:  // Burst: 1/1.1 a frame from the first
            return kBurstLight * std::pow(1.0f / 1.1f, age);
        default:  // Mist: 1/1.1 a frame under 10
            return left < 10.0f ? kMistLight * std::pow(1.0f / 1.1f, 10.0f - left) : kMistLight;
    }
}

}  // namespace

float WaterfallSpray::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void WaterfallSpray::open(const std::string& assetDir, const std::string& world,
                          const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (world != "aida") return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        const char* names[3] = {"waterfall_drops", "waterfall_spray", "waterfall_mist"};
        for (int k = 0; k < 3; ++k)
            if (const content::EffectSheet* sheet = table.effect(names[k]))
                sheets_[k] = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    size_t counts[3] = {0, 0, 0};
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model != content::TownEmitter::kWorld) continue;
        const int k = int(one.kind) - int(content::EmitterKind::SprayDrops);
        if (k < 0 || k > 2) continue;
        Box box;
        box.kind = Kind(k);
        for (int i = 0; i < 3; ++i) box.at[i] = one.at[i];
        box.scale = one.high;
        boxes_.push_back(box);
        ++counts[k];
    }
    open_ = true;
    drops_.reserve(kMostDrops);
    core::logf("waterfall spray: %zu drop boxes, %zu burst boxes, %zu mist boxes; sheets %s %s %s",
               counts[0], counts[1], counts[2], bgfx::isValid(sheets_[0]) ? "yes" : "NO",
               bgfx::isValid(sheets_[1]) ? "yes" : "NO", bgfx::isValid(sheets_[2]) ? "yes" : "NO");
}

void WaterfallSpray::shutdown() {
    open_ = false;
    boxes_.clear();
    drops_.clear();
    for (auto& sheet : sheets_) sheet = BGFX_INVALID_HANDLE;
}

void WaterfallSpray::throwDrop(const Box& box) {
    if (drops_.size() >= kMostDrops) return;
    Drop drop;
    drop.kind = box.kind;
    for (int i = 0; i < 3; ++i) drop.at[i] = box.at[i];
    drop.spin = unit() * kTau;
    switch (box.kind) {
        case Kind::Drops:
            drop.life = kDropsLife;
            drop.speed = -perSecond(7.0f + std::floor(unit() * 5.0f));
            drop.scale = 1.6f + box.scale;
            break;
        case Kind::Burst:
            drop.life = kBurstLife;
            drop.speed = perSecond(2.0f + std::floor(unit() * 5.0f));
            drop.scale = (10.0f + std::floor(unit() * 10.0f)) * 0.02f + box.scale;
            break;
        case Kind::Mist:
            drop.life = kMistLife;
            drop.speed = -perSecond(3.0f + std::floor(unit() * 3.0f));
            drop.scale = (6.0f + std::floor(unit() * 6.0f)) * 0.1f + box.scale;
            drop.at[0] += (std::floor(unit() * 20.0f) - 10.0f) * kUnit;
            drop.at[2] += (std::floor(unit() * 20.0f) - 10.0f) * kUnit;
            break;
    }
    drop.bornY = drop.at[1];
    drops_.push_back(drop);
}

void WaterfallSpray::update(float seconds, const float near[3]) {
    if (!open_) return;
    // A long hitch would throw a second's spray in one place at once.
    seconds = std::min(seconds, 0.25f);
    const float frames = seconds / kFrame;
    for (Drop& one : drops_) {
        one.age += frames;
        switch (one.kind) {
            case Kind::Drops:
                one.speed += accel(0.1f) * seconds;
                one.scale -= 0.005f * frames;
                break;
            case Kind::Burst:
                one.speed -= accel(2.5f) * seconds;
                one.scale += 0.005f * frames;
                break;
            case Kind::Mist:
                one.speed += accel(0.1f) * seconds;
                one.scale += 0.03f * frames;
                one.at[0] += (unit() * 2.0f - 1.0f) * kUnit * frames;
                one.at[2] += (unit() * 2.0f - 1.0f) * kUnit * frames;
                break;
        }
        one.at[1] += one.speed * seconds;
        if (one.kind == Kind::Mist && one.life - one.age < 10.0f) one.at[1] = one.bornY;
    }
    drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                [](const Drop& d) { return d.age >= d.life || d.scale <= 0.0f; }),
                 drops_.end());

    for (Box& box : boxes_) {
        const float dx = box.at[0] - near[0], dz = box.at[2] - near[2];
        if (dx * dx + dz * dz > kReach * kReach) {
            box.owed = 0.0f;
            continue;
        }
        box.owed += seconds;
        while (box.owed >= kFrame) {
            box.owed -= kFrame;
            // rand_fps_check(2), every frame, rand_fps_check(3).
            const float odds = box.kind == Kind::Drops ? 0.5f : box.kind == Kind::Burst ? 1.0f : 1.0f / 3.0f;
            if (odds >= 1.0f || unit() < odds) throwDrop(box);
        }
    }
}

void WaterfallSpray::gather(gfx::Effects& effects) const {
    if (!open_) return;
    for (const Drop& one : drops_) {
        const bgfx::TextureHandle sheet = sheets_[int(one.kind)];
        if (!bgfx::isValid(sheet)) continue;
        const float level = lightOf(uint8_t(one.kind), one.age, one.life);
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = level;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * one.scale;
        sprite.spin = one.spin;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
