#include "game/fx/staff_fire.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kFrame = 1.0f / 25.0f;  // MU's reference frame
constexpr float kUnit = 0.01f;          // a MU unit in metres
// Ours: how much of MU's size and light each part keeps (see the header).
constexpr float kSize = 0.4f;
constexpr float kLight = 0.45f;
constexpr int kMostSparks = 64;
constexpr float kSparkGravity = 2.5f;  // metres a second, a second: they fall off the staff

}  // namespace

float StaffFire::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void StaffFire::points(float head[3], float sparks[3], float shaft[10][3]) {
    // MU's points are in the hand bone's frame, (x, y, z) with the staff along -y; this staff's
    // glb runs along +z in metres: (0, -145, 0) is (0, 0, 1.45).
    head[0] = 0.0f;
    head[1] = 0.0f;
    head[2] = 145.0f * kUnit;
    sparks[0] = 0.0f;
    sparks[1] = 0.0f;
    sparks[2] = 95.0f * kUnit;  // rand() % 10 - 10 - 90: 90 to 100 up, the middle of it
    for (int j = 0; j < 10; ++j) {
        shaft[j][0] = 0.0f;
        shaft[j][1] = 0.0f;
        shaft[j][2] = -(60.0f - 20.0f * float(j)) * kUnit;
    }
}

bool StaffFire::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    shutdown();
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("staff fire: no cooked effect named '%s'", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    spark_ = take("spark");
    shiny_ = take("shiny");
    light_ = take("light");
    open_ = bgfx::isValid(spark_) && bgfx::isValid(light_);
    return open_;
}

void StaffFire::shutdown() {
    staffs_.clear();
    sparks_.clear();
    open_ = false;
}

void StaffFire::update(float seconds) {
    staffs_.clear();
    const float dt = std::min(seconds, 0.1f);
    for (Spark& one : sparks_) {
        one.age += dt;
        one.velocity[1] -= kSparkGravity * dt;
        for (int k = 0; k < 3; ++k) one.at[k] += one.velocity[k] * dt;
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(),
                                 [](const Spark& s) { return s.age >= s.life; }),
                  sparks_.end());
    // MU's rolls, once a reference frame: the luminosity and the three-in-four of each light.
    step_ -= seconds;
    if (step_ <= 0.0f) {
        step_ += kFrame;
        if (step_ < 0.0f) step_ = kFrame;
        luminosity_ = (float(int(unit() * 30.0f)) + 70.0f) * 0.01f;
        lit_ = 0;
        for (int j = 0; j < 10; ++j) {
            if (unit() < 0.75f) lit_ |= uint16_t(1u << j);
        }
        owed_ += 1.0f;  // a quarter of MU's four
    }
}

void StaffFire::feed(const float head[3], const float sparks[3], const float shaft[10][3]) {
    if (!open_) return;
    Staff one;
    for (int k = 0; k < 3; ++k) one.head[k] = head[k];
    for (int j = 0; j < 10; ++j) {
        for (int k = 0; k < 3; ++k) one.shaft[j][k] = shaft[j][k];
    }
    staffs_.push_back(one);
    while (owed_ >= 1.0f && int(sparks_.size()) < kMostSparks) {
        owed_ -= 1.0f;
        Spark spark;
        for (int k = 0; k < 3; ++k) spark.at[k] = sparks[k] + (unit() - 0.5f) * 0.1f;
        spark.velocity[0] = (unit() - 0.5f) * 0.8f;
        spark.velocity[1] = unit() * 0.6f;
        spark.velocity[2] = (unit() - 0.5f) * 0.8f;
        spark.life = 0.2f + unit() * 0.2f;
        sparks_.push_back(spark);
    }
    owed_ = std::min(owed_, 1.0f);
}

void StaffFire::gather(gfx::Effects& effects) const {
    if (!open_) return;
    const float l = luminosity_ * kLight;
    const auto put = [&](bgfx::TextureHandle sheet, const float at[3], float half,
                         float r, float g, float b, float spin) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = at[k];
        sprite.halfWidth = sprite.halfHeight = half;
        sprite.colour[0] = r;
        sprite.colour[1] = g;
        sprite.colour[2] = b;
        sprite.colour[3] = 1.0f;
        sprite.spin = spin;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };
    for (const Staff& one : staffs_) {
        // MU's Scale over a 64-texel sheet, in metres, halved for a half width, then ours.
        put(spark_, one.head, 0.5f * 3.0f * 64.0f * kUnit * kSize * 0.25f, l, 0.6f * l, 0.4f * l,
            0.0f);
        put(shiny_, one.head, 0.5f * 1.5f * 64.0f * kUnit * kSize * 0.25f, l, 0.6f * l, 0.4f * l,
            luminosity_ * 6.0f);
        for (int j = 0; j < 10; ++j) {
            if (!(lit_ & (1u << j))) continue;
            put(light_, one.shaft[j], 0.5f * 64.0f * kUnit * kSize * 0.25f, l, 0.2f * l, 0.1f * l,
                0.0f);
        }
    }
    for (const Spark& one : sparks_) {
        const float fade = 1.0f - one.age / one.life;
        put(spark_, one.at, 0.03f, l * fade, 0.6f * l * fade, 0.4f * l * fade, 0.0f);
    }
}

}  // namespace mu::game
