#include "game/fx/iron_rider.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
// Spark03 is 32 texels, a unit each; ours at a third, as the Aida sparks (fx/aida_sparks.h).
constexpr float kSpark = 0.32f * 0.33f;
// Impack01 and BowE are 64 texels; the arrow's sprites at a third of MU's 4 and 2.5.
constexpr float kSprite = 0.64f * 0.33f;
constexpr float kCloud = 0.64f;   // Clud64 and WATERFALL3, 64 texels
constexpr float kGreen[3] = {0.8f, 1.0f, 0.8f};
// Bip01, IRON_RIDER_BOW_6, _15 and _16: bones 2, 42, 52 and 47 (GM_Kanturu_1st.cpp:468-471).
constexpr const char* kBones[4] = {"Bip01", "Bone06", "Bone15", "Bone16"};
constexpr size_t kMostSparks = 900;
constexpr size_t kMostSmokes = 300;
constexpr size_t kMostArrows = 32;

}  // namespace

float IronRider::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool IronRider::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    shutdown();
    const std::string dir = assetDir + "/effects/kanturu/";
    const auto raw = [&](const char* name) -> bgfx::TextureHandle {
        const std::string path = dir + name;
        if (!core::fileExists(path)) {
            core::logError("iron rider: no %s", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    const auto cooked = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    spark_ = cooked("spark_flash");       // BITMAP_SPARK + 1, Effect/Spark03
    spray_ = cooked("waterfall_spray");   // BITMAP_WATERFALL_3, Effect/WATERFALL3
    impact_ = cooked("iron_light");       // BITMAP_LIGHT + 3, Effect/Impack01
    bow_ = cooked("iron_bow");            // BITMAP_DS_EFFECT, Effect/BowE
    cloud_ = cooked("iron_cloud");        // BITMAP_CLUD64, Effect/Clud64
    if (loadEffectObj(dir + "IronArrow01.obj", kUnit, "whitemark_R", mark_)) markSheet_ = raw("whitemark_r.png");
    if (loadEffectObj(dir + "IronArrow01.obj", kUnit, "motion_blur", blur_)) blurSheet_ = raw("motion_blur.png");
    core::logf("iron rider: spark %s, arrow %zu + %zu triangles, smoke %s",
               bgfx::isValid(spark_) ? "yes" : "NO", mark_.size() / 3, blur_.size() / 3,
               bgfx::isValid(cloud_) ? "yes" : "NO");
    return bgfx::isValid(spark_);
}

void IronRider::shutdown() {
    ones_.clear();
    sparks_.clear();
    smokes_.clear();
    arrows_.clear();
    mark_.clear();
    blur_.clear();
    spark_ = impact_ = bow_ = cloud_ = spray_ = markSheet_ = blurSheet_ = BGFX_INVALID_HANDLE;
}

void IronRider::spark(const float at[3], float fade) {
    if (sparks_.size() >= kMostSparks) return;
    Spark one;
    for (int i = 0; i < 3; ++i) one.at[i] = at[i] + (4.0f - std::floor(unit() * 5.0f)) * kUnit;
    one.scale = std::floor(unit() * 5.0f) / 10.0f + 1.0f;
    one.fade = fade;
    sparks_.push_back(one);
}

void IronRider::watch(uint32_t id, const Figure& figure, int attack, float key, float yaw,
                      const float* target, bool dying, float fade, float seconds) {
    One& one = ones_[id];
    one.seen = true;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return;
    if (!one.found) {
        one.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (int k = 0; k < 4; ++k)
            for (size_t b = 0; b < bones.size(); ++b)
                if (bones[b].name == kBones[k]) one.bones[k] = int(b);
    }
    // A new blow: the clip changed, or the same one began again.
    if (attack != one.lastAttack || key + 0.5f < one.lastKey) {
        one.frames = 0.0f;
        one.loosed = false;
    }
    one.lastAttack = attack;
    one.lastKey = key;

    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float at[4][3];
    bool has[4];
    for (int k = 0; k < 4; ++k) has[k] = one.bones[k] >= 0 && figure.pointOn(one.bones[k], origin, at[k]);

    one.owed += seconds / kFrame;
    while (one.owed >= 1.0f) {
        one.owed -= 1.0f;
        if (dying) {
            if (!has[0] || smokes_.size() + 2 > kMostSmokes) continue;
            for (int s = 0; s < 2; ++s) {
                Smoke smoke;
                smoke.cloud = s == 0;
                smoke.life = smoke.cloud ? 60.0f : 30.0f;
                smoke.scale = (smoke.cloud ? 2.0f : 1.0f) * (std::floor(unit() * 64.0f) + 64.0f) * 0.02f;
                smoke.spin = unit() * 6.2832f;
                // MU's x and y across, its z down: ours x and z across, y down.
                smoke.at[0] = at[0][0] + (std::floor(unit() * 250.0f) - 125.0f) * kUnit;
                smoke.at[2] = at[0][2] + (std::floor(unit() * 250.0f) - 125.0f) * kUnit;
                smoke.at[1] = at[0][1] - std::floor(unit() * 45.0f) * kUnit;
                smoke.rise = std::fabs((std::floor(unit() * 100.0f) - 50.0f) * 0.1f);
                smoke.fade = fade;
                smokes_.push_back(smoke);
            }
            continue;
        }
        if (has[2]) spark(at[2], fade);
        if (has[3]) spark(at[3], fade);
        if (attack == 0) continue;
        if (has[1]) spark(at[1], fade);
        one.frames += 1.0f;
        // AttackTime 10: the arrow nine frames into the blow, from the nock.
        if (!one.loosed && one.frames >= 9.0f && has[1] && arrows_.size() < kMostArrows) {
            one.loosed = true;
            Arrow arrow;
            for (int i = 0; i < 3; ++i) arrow.at[i] = at[1][i];
            arrow.way[0] = std::sin(yaw);
            arrow.way[1] = std::cos(yaw);
            if (target) {
                const float dx = target[0] - at[1][0], dz = target[2] - at[1][2];
                if (const float far = std::sqrt(dx * dx + dz * dz); far > 0.1f) {
                    arrow.way[0] = dx / far;
                    arrow.way[1] = dz / far;
                    // Ours: it ends where it strikes, rather than flying on its 12 frames past him.
                    arrow.life = std::min(12.0f, std::ceil(far / (70.0f * kUnit)));
                }
            }
            arrow.fade = fade;
            arrows_.push_back(arrow);
            if (++loosed_ <= 3) {
                core::logf("iron rider: an arrow from %.1f, %.1f toward %.2f, %.2f, %.0f frames (%s at %.1f, %.1f)",
                           arrow.at[0], arrow.at[2], arrow.way[0], arrow.way[1], arrow.life,
                           target ? "its target" : "its facing", target ? target[0] : 0.0f,
                           target ? target[2] : 0.0f);
            }
        }
    }
}

void IronRider::update(float seconds) {
    for (auto it = ones_.begin(); it != ones_.end();) {
        if (!it->second.seen) it = ones_.erase(it);
        else (it++)->second.seen = false;
    }
    const float frames = std::min(seconds, 0.25f) / kFrame;
    for (Spark& one : sparks_) {
        one.age += frames;
        one.scale -= 0.03f * frames;
        one.light *= std::pow(1.0f / 1.08f, frames);
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(),
                                 [](const Spark& s) { return s.age >= 20.0f || s.scale <= 0.0f; }),
                  sparks_.end());
    for (Smoke& one : smokes_) {
        one.age += frames;
        one.light *= std::pow(1.0f / 1.012f, frames);
        one.scale += 0.01f * frames;
        one.spin += one.rise * 0.5f * frames * 0.01745f;
        one.at[1] += one.rise * kUnit * frames;
    }
    smokes_.erase(std::remove_if(smokes_.begin(), smokes_.end(),
                                 [](const Smoke& s) { return s.age >= s.life || s.light <= 0.05f; }),
                  smokes_.end());
    // Moved smoothly every drawn frame, its scale and light read off its age: stepped a whole
    // reference frame at a time it jumped 70 units 25 times a second (the user, 2026-10-23:
    // 'bolts from monsters looked low fps'). Only its sparks keep MU's frame.
    for (Arrow& one : arrows_) {
        const float step = std::min(frames, one.life - one.age);
        one.age += step;
        one.at[0] += one.way[0] * 70.0f * kUnit * step;
        one.at[2] += one.way[1] * 70.0f * kUnit * step;
        one.scale = 0.5f * std::pow(1.05f, one.age);
        one.light = std::pow(0.7f, std::max(0.0f, one.age - (one.life - 5.0f)));
        for (one.owed += step; one.owed >= 1.0f; one.owed -= 1.0f) spark(one.at, one.fade);
    }
    arrows_.erase(std::remove_if(arrows_.begin(), arrows_.end(),
                                 [](const Arrow& a) { return a.age >= a.life; }),
                  arrows_.end());
}

void IronRider::gather(gfx::Effects& effects) const {
    const auto add = [&](bgfx::TextureHandle sheet, const float at[3], float half, float light,
                         gfx::Blend blend, float spin) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite one;
        one.sheet = sheet;
        for (int i = 0; i < 3; ++i) {
            one.position[i] = at[i];
            one.colour[i] = kGreen[i] * light;
        }
        if (blend != gfx::Blend::Additive) one.colour[0] = one.colour[1] = one.colour[2] = light;
        one.colour[3] = 1.0f;
        one.halfWidth = one.halfHeight = half;
        one.spin = spin;
        one.blend = blend;
        effects.add(one);
    };
    for (const Spark& one : sparks_) {
        add(spark_, one.at, 0.5f * kSpark * one.scale, one.light * one.fade, gfx::Blend::Additive, 0.0f);
    }
    for (const Smoke& one : smokes_) {
        add(one.cloud ? cloud_ : spray_, one.at, 0.5f * kCloud * one.scale, one.light * one.fade,
            gfx::Blend::Additive, one.spin);
    }
    for (const Arrow& one : arrows_) {
        const float light = one.light * one.fade;
        add(impact_, one.at, 0.5f * kSprite * 4.0f, light, gfx::Blend::Additive, 0.0f);
        add(bow_, one.at, 0.5f * kSprite * 2.5f, light, gfx::Blend::Additive, 0.0f);
        // The mesh's +Z (after loadEffectObj's turn, its -Z) is its point: laid along the flight.
        const float x[3] = {one.way[1], 0.0f, -one.way[0]};
        const float y[3] = {0.0f, 1.0f, 0.0f};
        const float z[3] = {-one.way[0], 0.0f, -one.way[1]};
        const float colour[3] = {kGreen[0] * light, kGreen[1] * light, kGreen[2] * light};
        submitEffectAlong(effects, mark_, markSheet_, gfx::Blend::Additive, one.at, x, y, z, one.scale,
                          colour, 1.0f);
        submitEffectAlong(effects, blur_, blurSheet_, gfx::Blend::Additive, one.at, x, y, z, one.scale,
                          colour, 1.0f);
    }
}

}  // namespace mu::game
