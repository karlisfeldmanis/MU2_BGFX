#include "game/fx/kentauros.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
// smoke01 and Clud64 are 64 texels, a unit each; ours at a third.
constexpr float kSmoke = 0.64f * 0.33f;
constexpr float kCloud = 0.64f * 0.33f;
constexpr float kBowLight[3] = {0.3f, 0.5f, 0.7f};
constexpr float kArrowLight[3] = {0.3f, 0.5f, 1.0f};
constexpr float kWhite[3] = {1.0f, 1.0f, 1.0f};
constexpr float kRelease = 5.5f;      // inside the full draw both attacks hold, keys 5.37-6.10
constexpr float kSpeed = 70.0f;       // units a frame
constexpr float kTrail = 16.0f;       // the joint's tails, a frame apart
constexpr float kTrailHalf = 7.5f * kUnit;
constexpr float kTrailFade = 6.0f;    // frames the streak takes to go once the arrow has struck
constexpr float kSpread = 15.0f * 0.0174533f;
constexpr float kChest = 1.2f;        // metres above its target's feet that an arrow ends
// KENTAUROS_BIP_23 to _26 and _18 to _21; _TAIL, _TAIL_1, _TAIL_2 and _SPAIN_1 to _3
// (GM_Kanturu_1st.cpp:505-518).
constexpr const char* kBowBones[8] = {"Bone23", "Bone24", "Bone25", "Bone26",
                                      "Bone18", "Bone19", "Bone20", "Bone21"};
constexpr const char* kBackBones[6] = {"Bip01 Tail", "Bip01 Tail1", "Bip01 Tail2",
                                       "Bip01 Spine1", "Bip01 Spine2", "Bip01 Spine3"};
constexpr size_t kMostSmokes = 1200;
constexpr size_t kMostArrows = 48;

}  // namespace

float Kentauros::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool Kentauros::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    shutdown();
    const auto cooked = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    smoke_ = cooked("smoke01");      // BITMAP_SMOKE, Effect/smoke01
    cloud_ = cooked("iron_cloud");   // BITMAP_CLUD64, Effect/Clud64
    flare_ = cooked("pierce");       // BITMAP_FLARE + 1, Effect/Flare02
    core::logf("kentauros: smoke %s, cloud %s, streak %s", bgfx::isValid(smoke_) ? "yes" : "NO",
               bgfx::isValid(cloud_) ? "yes" : "NO", bgfx::isValid(flare_) ? "yes" : "NO");
    return bgfx::isValid(smoke_);
}

void Kentauros::shutdown() {
    ones_.clear();
    smokes_.clear();
    arrows_.clear();
    smoke_ = cloud_ = flare_ = BGFX_INVALID_HANDLE;
}

void Kentauros::puff(const float at[3], int kind, float scale, const float colour[3], float fade) {
    if (smokes_.size() >= kMostSmokes) return;
    Smoke one;
    for (int i = 0; i < 3; ++i) {
        one.at[i] = at[i];
        one.colour[i] = colour[i];
    }
    one.kind = kind;
    one.scale = scale;
    one.spin = unit() * 6.2832f;
    one.fade = fade;
    // Sub 31 and sub 26 rise 6-9.1 a frame (their Gravity); the death's cloud as the Iron Rider's.
    one.rise = kind == 2 ? std::floor(unit() * 50.0f) * 0.1f : (std::floor(unit() * 32.0f) + 60.0f) * 0.1f;
    one.life = kind == 0 ? 15.0f : kind == 1 ? 25.0f : 60.0f;
    smokes_.push_back(one);
}

void Kentauros::loose(const float from[3], float yaw, const float* target, int count, float fade) {
    float aim = yaw;
    float far = -1.0f;
    float fall = 0.0f;
    if (target) {
        const float dx = target[0] - from[0], dz = target[2] - from[2];
        if (const float d = std::sqrt(dx * dx + dz * dz); d > 0.1f) {
            aim = std::atan2(dx, dz);
            far = d;
            fall = (from[1] - (target[1] + kChest)) / d;
        }
    }
    for (int k = 0; k < count && arrows_.size() < kMostArrows; ++k) {
        const float turn = count == 1 ? 0.0f : (k - 1) * kSpread;
        Arrow arrow;
        for (int i = 0; i < 3; ++i) arrow.from[i] = from[i];
        arrow.way[0] = std::sin(aim + turn);
        arrow.way[1] = std::cos(aim + turn);
        arrow.fall = fall;
        // Ours: it ends where it strikes, rather than flying on its 24 frames past him.
        if (far > 0.0f) arrow.flight = std::min(24.0f, std::ceil(far / (kSpeed * kUnit)));
        arrow.fade = fade;
        arrows_.push_back(arrow);
    }
    if (++loosed_ <= 3) {
        core::logf("kentauros: %d arrow%s from %.1f, %.1f, %.1f toward %.0f degrees (%s, %.1f m, falling %.2f)",
                   count, count == 1 ? "" : "s", from[0], from[1], from[2], aim * 57.2958f,
                   far > 0.0f ? "its target" : "its facing", far, fall);
    }
}

void Kentauros::watch(uint32_t id, const Figure& figure, int attack, float key, float yaw,
                      const float* target, bool dying, float fade, float seconds) {
    One& one = ones_[id];
    one.seen = true;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return;
    if (!one.found) {
        one.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        const auto find = [&](const char* name) {
            for (size_t b = 0; b < bones.size(); ++b)
                if (bones[b].name == name) return int(b);
            return -1;
        };
        for (int k = 0; k < kBow; ++k) one.bow[k] = find(kBowBones[k]);
        for (int k = 0; k < kBack; ++k) one.back[k] = find(kBackBones[k]);
        one.grip = find("Bone29");
    }
    // A new blow: the clip changed, or the same one began again.
    if (attack != one.lastAttack || key + 0.5f < one.lastKey) {
        one.loosed = false;
    }
    one.lastAttack = attack;
    one.lastKey = key;
    if (attack == 0 && !dying) {
        one.owed = 0.0f;
        return;
    }

    const float origin[3] = {0.0f, 0.0f, 0.0f};
    one.owed += seconds / kFrame;
    while (one.owed >= 1.0f) {
        one.owed -= 1.0f;
        float at[3];
        if (dying) {
            for (int k = 0; k < kBack; ++k) {
                if (one.back[k] < 0 || !figure.pointOn(one.back[k], origin, at)) continue;
                puff(at, 2, 0.3f * (std::floor(unit() * 64.0f) + 64.0f) * 0.02f * 2.0f, kWhite, fade);
            }
            continue;
        }
        for (int k = 0; k < kBow; ++k) {
            if (one.bow[k] < 0 || !figure.pointOn(one.bow[k], origin, at)) continue;
            puff(at, 0, 2.0f, kBowLight, fade);
        }
        // MU's AttackTime 14 and its arrow's ten frames' wait come to key 6.2 at 0.27 a frame:
        // the string's release, keys 5-6 held at full draw and 7 thrown back. MU's wait kept the
        // point it was made at, key 3.5, while the bow was still coming up, 70 cm off (the user,
        // 2026-10-23: "bolts is not coming from release points of wepon for monster"). Ours looses
        // at key 5.5, inside the held draw, from the grip: at 6.1 the bow was already thrown level
        // the next frame and the streak's root stood off it ("still did not released perfectly
        // from center of that weapon").
        if (!one.loosed && key >= kRelease && one.grip >= 0 && figure.pointOn(one.grip, origin, at)) {
            one.loosed = true;
            loose(at, yaw, target, attack == 1 ? 1 : 3, fade);
        }
    }
}

void Kentauros::update(float seconds) {
    for (auto it = ones_.begin(); it != ones_.end();) {
        if (!it->second.seen) it = ones_.erase(it);
        else (it++)->second.seen = false;
    }
    const float frames = std::min(seconds, 0.25f) / kFrame;
    for (Smoke& one : smokes_) {
        one.age += frames;
        if (one.kind == 0) {
            one.scale -= 0.05f * frames;
            one.light *= std::pow(1.0f / 1.1f, frames);
        } else if (one.kind == 1) {
            one.scale += 0.15f * frames;
            one.light *= std::pow(std::max(0.0f, one.life - one.age) / 20.0f, frames);
        } else {
            one.scale += 0.01f * frames;
            one.light *= std::pow(1.0f / 1.012f, frames);
        }
        one.spin += one.rise * 0.5f * frames * 0.01745f;
        one.at[1] += one.rise * kUnit * frames;
    }
    smokes_.erase(std::remove_if(smokes_.begin(), smokes_.end(),
                                 [](const Smoke& s) {
                                     return s.age >= s.life || s.scale <= 0.0f || s.light <= 0.02f;
                                 }),
                  smokes_.end());
    // Moved smoothly every drawn frame (the Iron Rider's lesson: 'bolts from monsters looked low
    // fps'); only its smoke keeps MU's frame.
    for (Arrow& one : arrows_) {
        const float before = one.age;
        one.age += frames;
        const float flying = std::min(one.age, one.flight);
        for (one.owed += std::min(one.age, one.flight) - std::min(before, one.flight);
             one.owed >= 1.0f; one.owed -= 1.0f) {
            const float along = kSpeed * kUnit * flying;
            const float at[3] = {one.from[0] + one.way[0] * along, one.from[1] - one.fall * along,
                                 one.from[2] + one.way[1] * along};
            puff(at, 1, (std::floor(unit() * 64.0f) + 64.0f) * 0.01f, kArrowLight, one.fade);
            puff(at, 1, (std::floor(unit() * 64.0f) + 64.0f) * 0.01f, kArrowLight, one.fade);
        }
    }
    arrows_.erase(std::remove_if(arrows_.begin(), arrows_.end(),
                                 [](const Arrow& a) { return a.age >= a.flight + kTrailFade; }),
                  arrows_.end());
}

void Kentauros::gather(gfx::Effects& effects) const {
    for (const Smoke& one : smokes_) {
        const bgfx::TextureHandle sheet = one.kind == 2 ? cloud_ : smoke_;
        if (!bgfx::isValid(sheet)) continue;
        gfx::Sprite sprite;
        sprite.sheet = sheet;
        const float light = one.light * one.fade;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = one.colour[i] * light;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * (one.kind == 2 ? kCloud : kSmoke) * one.scale;
        sprite.spin = one.spin;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    if (!bgfx::isValid(flare_)) return;
    for (const Arrow& one : arrows_) {
        const float t = one.age;
        if (t <= 0.0f) continue;
        // The joint's sixteen tails behind its head; once it has struck, the tail runs on into it
        // and the streak fades.
        const float head = std::min(t, one.flight) * kSpeed * kUnit;
        const float tail = std::clamp((t - kTrail) * kSpeed * kUnit, 0.0f, head);
        if (head - tail < 0.05f) continue;
        const float light = one.fade * std::clamp(1.0f - (t - one.flight) / kTrailFade, 0.0f, 1.0f);
        if (light <= 0.0f) continue;
        float front[3], back[3];
        front[0] = one.from[0] + one.way[0] * head;
        front[2] = one.from[2] + one.way[1] * head;
        back[0] = one.from[0] + one.way[0] * tail;
        back[2] = one.from[2] + one.way[1] * tail;
        front[1] = one.from[1] - one.fall * head;
        back[1] = one.from[1] - one.fall * tail;
        const float across[3] = {one.way[1], 0.0f, -one.way[0]};
        const float lift[3] = {0.0f, 1.0f, 0.0f};
        for (const float* width : {static_cast<const float*>(across), static_cast<const float*>(lift)}) {
            gfx::Sprite sprite;
            sprite.placed = true;
            for (int k = 0; k < 3; ++k) {
                sprite.corner[0][k] = back[k] - width[k] * kTrailHalf;
                sprite.corner[1][k] = front[k] - width[k] * kTrailHalf;
                sprite.corner[2][k] = front[k] + width[k] * kTrailHalf;
                sprite.corner[3][k] = back[k] + width[k] * kTrailHalf;
                sprite.position[k] = 0.5f * (front[k] + back[k]);
                sprite.colour[k] = kWhite[k] * light;
            }
            // Flare02's streak runs along u, its tail end at the back.
            sprite.cornerUv[0][0] = 0.0f;
            sprite.cornerUv[0][1] = 0.0f;
            sprite.cornerUv[1][0] = 1.0f;
            sprite.cornerUv[1][1] = 0.0f;
            sprite.cornerUv[2][0] = 1.0f;
            sprite.cornerUv[2][1] = 1.0f;
            sprite.cornerUv[3][0] = 0.0f;
            sprite.cornerUv[3][1] = 1.0f;
            sprite.colour[3] = 1.0f;
            sprite.sheet = flare_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
