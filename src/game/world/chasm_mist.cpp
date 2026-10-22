#include "game/world/chasm_mist.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/json.h"
#include "core/log.h"
#include "game/world/world_show.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;        // MU's reference frame, 25 a second
constexpr float kUnit = 0.01f;         // a MU unit, in metres
constexpr float kSheetMetres = 2.56f;  // clouds.jpg's 256 texels at Scale 1
constexpr float kReach = 36.0f;        // metres from the camera's point a box counts as seen
constexpr float kRatio = 10.0f;        // MU's puffs one of ours stands for
constexpr size_t kMost = 300;          // MU's pool of 3,000, over kRatio
constexpr float kThrows = 20.0f / kRatio;  // ours a box throws a frame
constexpr float kRiseLife = 300.0f, kRiseLight = 0.04f;
constexpr float kHoverLight = 0.06f, kShadeLight = 0.2f;
constexpr float kAfterSeen = 50.0f;    // frames a Hover or Shade outlives its box's sight
constexpr float kDegrees = 0.0174533f;
// Ours: metres over which a puff thins out as it nears the lip or a cliff behind it, where MU's
// depth test cut it on a hard line (gfx::Sprite::soft).
constexpr float kSoft = 2.0f;

}  // namespace

float ChasmMist::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void ChasmMist::open(const std::string& assetDir, const std::string& world, const content::Ground& ground,
                     content::Textures& textures) {
    shutdown();
    ground_ = &ground;
    if (!worldShow(world).chasmMist) return;
    strength_ = std::clamp(worldShow(world).chasmMistStrength, 0.0f, 1.0f);
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error))
        if (const content::EffectSheet* sheet = table.effect("cloud"))
            sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    const core::Json doc = core::parseJsonFile(assetDir + "/world/" + world + "/" + world + ".json");
    const core::Json& objects = doc["objects"];
    size_t counts[3] = {0, 0, 0};
    for (size_t i = 0; i < objects.size(); ++i) {
        const core::Json& one = objects.at(i);
        const std::string model = one["model"].stringOr("");
        Kind kind;
        if (model == "Object63") kind = Kind::Rise;
        else if (model == "Object108") kind = Kind::Hover;
        else if (model == "Object109") kind = Kind::Shade;
        else continue;
        const core::Json& at = one["at"];
        if (at.size() < 3) continue;
        // MU's x, y and height in units; ours are x, height and -y in metres.
        Box box;
        box.kind = kind;
        box.at[0] = float(at.at(0).numberOr(0)) * kUnit;
        box.at[1] = float(at.at(2).numberOr(0)) * kUnit;
        box.at[2] = -float(at.at(1).numberOr(0)) * kUnit;
        box.scale = float(one["scale"].numberOr(1.0));
        boxes_.push_back(box);
        ++counts[int(kind)];
    }
    open_ = true;
    puffs_.reserve(kMost);
    core::logf("chasm mist: %zu rise boxes, %zu hover boxes, %zu shade boxes; sheet %s", counts[0],
               counts[1], counts[2], bgfx::isValid(sheet_) ? "yes" : "NO");
}

void ChasmMist::shutdown() {
    open_ = false;
    boxes_.clear();
    puffs_.clear();
    ground_ = nullptr;
    sheet_ = BGFX_INVALID_HANDLE;
}

bool ChasmMist::voidAt(float x, float z) const {
    const float m = ground_->metresPerTile();
    const int c = int(std::floor(x / m)), r = int(std::floor(-z / m));
    if (c < 0 || r < 0 || c >= ground_->size() || r >= ground_->size()) return true;
    return (ground_->attributesAt(c, r) & content::kNoGround) != 0;
}

// The share of its middle and sixteen points round it, at `radius`, that are over the void.
float ChasmMist::overVoid(float x, float z, float radius) const {
    int over = voidAt(x, z) ? 1 : 0;
    for (int k = 0; k < 16; ++k) {
        const float a = 0.3926991f * float(k);
        over += voidAt(x + std::cos(a) * radius, z + std::sin(a) * radius) ? 1 : 0;
    }
    return float(over) / 17.0f;
}

void ChasmMist::throwPuff(uint16_t index) {
    if (puffs_.size() >= kMost) return;  // MU's pool is full: CreateParticle finds no slot
    const Box& box = boxes_[index];
    Puff puff;
    puff.kind = box.kind;
    puff.box = index;
    for (int i = 0; i < 3; ++i) puff.at[i] = box.at[i];
    puff.at[0] += (std::floor(unit() * 500.0f) - 250.0f) * kUnit;
    puff.at[2] += (std::floor(unit() * 500.0f) - 250.0f) * kUnit;
    puff.phase = std::floor(unit() * 1000.0f);
    if (box.kind == Kind::Rise) {
        // MU's spread, tried a few times for a spot with the void under half its width.
        const float half = 0.5f * kSheetMetres * 1.9f * box.scale;
        bool clear = false;
        for (int tries = 0; tries < 4 && !clear; ++tries) {
            puff.at[0] = box.at[0] + (std::floor(unit() * 500.0f) - 250.0f) * kUnit;
            puff.at[2] = box.at[2] + (std::floor(unit() * 500.0f) - 250.0f) * kUnit;
            clear = overVoid(puff.at[0], puff.at[2], 0.5f * half) >= 0.99f;
        }
        if (!clear) return;
        puff.at[1] += (std::floor(unit() * 20.0f) - 20.0f) * kUnit;
        puff.left = kRiseLife;
        puff.alpha = 0.1f;
        puff.scale = (180.0f + std::floor(unit() * 20.0f)) * 0.01f * box.scale;
    } else {
        puff.at[1] += (20.0f + std::floor(unit() * 20.0f)) * kUnit;
        puff.scale = (180.0f + std::floor(unit() * 20.0f)) * 0.01f;
        puff.turning = box.scale + std::floor(unit() * 30.0f) / 100.0f;
    }
    // Fainter the less of it lies over the void (Rise and Hover; Shade darkens the ground it
    // stands on, as MU's does).
    if (box.kind != Kind::Shade) {
        const float over = overVoid(puff.at[0], puff.at[2], 0.45f * kSheetMetres * puff.scale);
        puff.edge = box.kind == Kind::Rise ? over * over : 1.0f;
    }
    puff.bornY = puff.at[1];
    puffs_.push_back(puff);
}

void ChasmMist::update(float seconds, const float near[3]) {
    if (!open_) return;
    seconds = std::min(seconds, 0.25f);
    const float frames = seconds / kFrame;
    clock_ += seconds * 1000.0f;
    for (Box& box : boxes_) {
        const float dx = box.at[0] - near[0], dz = box.at[2] - near[2];
        box.seen = dx * dx + dz * dz <= kReach * kReach;
    }
    for (Puff& one : puffs_) {
        one.age += frames;
        if (one.kind == Kind::Rise) {
            one.left -= frames;
            if (one.left > 150.0f) {
                one.alpha = std::min(one.alpha + 0.04f * frames, 1.0f);
                one.at[1] += kUnit * frames;
            } else {
                if (one.alpha > 0.1f) one.alpha = std::max(one.alpha - 0.025f * frames, 0.1f);
                one.at[1] -= 0.5f * kUnit * frames;
            }
        } else {
            // LifeTime held at 50 while its box is seen, counted down after.
            one.left = boxes_[one.box].seen ? 0.0f : one.left + frames;
            one.at[1] = one.bornY + std::sin((clock_ + one.phase) / 5000.0f) * 20.0f * kUnit;
        }
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) {
                                    return p.kind == Kind::Rise ? p.left <= 0.0f : p.left >= kAfterSeen;
                                }),
                 puffs_.end());

    for (uint16_t i = 0; i < uint16_t(boxes_.size()); ++i) {
        Box& box = boxes_[i];
        if (!box.seen) {
            box.owed = 0.0f;
            continue;
        }
        box.owed += seconds;
        while (box.owed >= kFrame) {
            box.owed -= kFrame;
            // kThrows a frame, the fraction by chance.
            float n = kThrows;
            for (; n >= 1.0f; n -= 1.0f) throwPuff(i);
            if (n > 0.0f && unit() < n) throwPuff(i);
        }
    }
}

void ChasmMist::gather(gfx::Effects& effects) const {
    if (!open_ || !bgfx::isValid(sheet_)) return;
    // The minus kind's darkening as kRatio of MU's laid over each other: 1 - (1 - 0.2)^kRatio.
    const float shade = 1.0f - std::pow(1.0f - kShadeLight, kRatio * strength_);
    for (const Puff& one : puffs_) {
        float level;
        float spin = 0.0f;
        gfx::Blend blend = gfx::Blend::Additive;
        switch (one.kind) {
            case Kind::Rise:
                level = kRiseLight * one.alpha * kRatio * strength_ * one.edge;
                break;
            case Kind::Hover:
                level = kHoverLight * kRatio * strength_;
                spin = clock_ * -0.02f * one.turning * kDegrees;
                break;
            default:
                level = shade;
                blend = gfx::Blend::Minus;
                break;
        }
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = level;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * one.scale;
        sprite.spin = spin;
        sprite.sheet = sheet_;
        sprite.blend = blend;
        sprite.soft = kSoft;
        effects.add(sprite);
    }
}

}  // namespace mu::game
