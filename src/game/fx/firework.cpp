#include "game/fx/firework.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;

}  // namespace

bool Firework::open(const std::string& assetDir, content::Textures& textures,
                    const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    bool all = true;
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("firework: no cooked effect named '%s'", name);
            all = false;
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    shiny_ = take("shiny");          // BITMAP_SHINY, Shiny01.jpg
    light_ = take("light");          // BITMAP_LIGHT, flare01.jpg
    shock_ = take("shockwave");      // BITMAP_DS_SHOCK, Shockwave.jpg
    spark_ = take("spark_flash");    // BITMAP_SPARK + 1, Spark03.jpg
    blast_ = take("explosion_mono"); // BITMAP_EXPLOTION_MONO, explotion01mono.jpg
    const char* frames[7] = {"firework_1", "firework_2", "firework_3", "firework_4",
                             "firework_5", "firework_6", "firework_7"};
    for (int i = 0; i < 7; ++i) stars_[i] = take(frames[i]);
    core::logf("firework: %s", all ? "every sheet in hand" : "SHEETS MISSING");
    return all;
}

void Firework::launch(const float at[3]) {
    for (Launcher& launcher : launchers_) {
        if (launcher.alive) continue;
        launcher.alive = true;
        for (int k = 0; k < 3; ++k) launcher.at[k] = at[k];
        // LifeTime 31: its first frame is the 31 that sends the first rocket.
        launcher.left = 31.0f;
        return;
    }
}

bool Firework::live() const {
    for (const Launcher& one : launchers_) if (one.alive) return true;
    for (const Rocket& one : rockets_) if (one.alive) return true;
    for (const Blast& one : blasts_) if (one.alive) return true;
    for (const Star& one : starList_) if (one.alive) return true;
    for (const Mote& one : sparks_) if (one.alive) return true;
    for (const Mote& one : glitter_) if (one.alive) return true;
    return false;
}

int Firework::dice(int n) {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return n > 0 ? int(dice_ % uint32_t(n)) : 0;
}

float Firework::roll(float low, float high) {
    return low + (high - low) * float(dice(1 << 16)) / float(1 << 16);
}

void Firework::fly(Rocket& rocket, bool& bursts) {
    for (int i = kTails - 1; i > 0; --i) {
        for (int k = 0; k < 3; ++k) rocket.tail[i][k] = rocket.tail[i - 1][k];
    }
    for (int k = 0; k < 3; ++k) rocket.tail[0][k] = rocket.at[k];
    rocket.tails = std::min(rocket.tails + 1, kTails);
    const bool climbing = rocket.left >= 10.0f;
    // The joint's own 9 along its pitch, and while it climbs the sub-type's 9 more.
    rocket.at[1] += (climbing ? 18.0f : 9.0f) * kUnit;
    if (climbing) {
        rocket.at[0] += float(dice(16) - 8) * kUnit;
        rocket.at[2] += float(dice(16) - 8) * kUnit;
        rocket.width += 3.0f;
    } else {
        for (float& c : rocket.light) c /= 1.45f;
        if (rocket.light[0] < 0.2f) rocket.alive = false;
    }
    if (int(rocket.left + 0.5f) == 10) bursts = true;
    rocket.left -= 1.0f;
    if (rocket.left <= 0.0f) rocket.alive = false;
}

void Firework::pop(const Rocket& rocket) {
    for (Blast& blast : blasts_) {
        if (blast.alive) continue;
        blast.alive = true;
        for (int k = 0; k < 3; ++k) {
            blast.at[k] = rocket.at[k];
            blast.light[k] = rocket.light[k];
        }
        blast.left = 30.0f;
        break;
    }
    // The shock ring, `rand() % 100 - 50` units off it, `rand() % 10 * 0.1 + 1.5` across.
    for (Flash& flash : flashes_) {
        if (flash.alive) continue;
        flash.alive = true;
        flash.at[0] = rocket.at[0] + float(dice(100) - 50) * kUnit;
        flash.at[1] = rocket.at[1];
        flash.at[2] = rocket.at[2] + float(dice(100) - 50) * kUnit;
        for (int k = 0; k < 3; ++k) flash.light[k] = rocket.light[k];
        flash.scale = float(dice(10)) * 0.1f + 1.5f;
        break;
    }
    // Sixty sparks flung every way at 12 units a frame (sub-type 27), and thirty thrown out
    // flat that fall, bounce and die (28).
    const int flings = int(60 * kShare), falls = int(30 * kShare), sprinkles = int(60 * kShare);
    int flung = 0, fallen = 0;
    for (Mote& spark : sparks_) {
        if (spark.alive) continue;
        if (flung == flings && fallen == falls) break;
        spark = Mote{};
        spark.alive = true;
        for (int k = 0; k < 3; ++k) spark.at[k] = rocket.at[k];
        spark.light[0] = spark.light[1] = spark.light[2] = 1.0f;
        spark.scale = float(dice(20)) / 20.0f + 1.0f;
        if (flung < flings) {
            float d[3] = {float(dice(16) - 8), float(dice(16) - 8), float(dice(16) - 8)};
            float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (length < 1e-4f) {
                d[1] = 1.0f;
                length = 1.0f;
            }
            for (int k = 0; k < 3; ++k) spark.velocity[k] = d[k] / length * 12.0f * kUnit;
            spark.left = float(dice(10) + 10);
            ++flung;
        } else {
            spark.falls = true;
            spark.gravity = 20.0f;
            spark.velocity[0] = float(dice(40) - 20) * 0.1f * 2.5f * kUnit;
            spark.velocity[2] = float(dice(40) - 20) * 0.1f * 2.5f * kUnit;
            spark.left = float(dice(10) + 20);
            ++fallen;
        }
    }
    // Sixty specks of glitter, each its own colour, `(8, 8, 8)` turned every way.
    int specks = 0;
    for (Mote& speck : glitter_) {
        if (speck.alive) continue;
        if (specks == sprinkles) break;
        speck = Mote{};
        speck.alive = true;
        for (int k = 0; k < 3; ++k) {
            speck.at[k] = rocket.at[k];
            speck.light[k] = 0.3f + float(dice(700)) * 0.001f;
        }
        speck.base = speck.scale = float(dice(5)) / 10.0f + 0.5f;
        speck.left = float(dice(10) + 60);
        speck.gravity = 20.0f;
        const float yaw = float(dice(360)) * kDegrees, pitch = (float(dice(180)) - 90.0f) * kDegrees;
        const float speed = std::sqrt(3.0f) * 8.0f * kUnit;
        speck.velocity[0] = std::cos(pitch) * std::cos(yaw) * speed;
        speck.velocity[1] = std::sin(pitch) * speed;
        speck.velocity[2] = std::cos(pitch) * std::sin(yaw) * speed;
        ++specks;
    }
}

void Firework::ribbon(gfx::Effects& effects, const Rocket& rocket) const {
    float points[kTails + 1][3];
    for (int k = 0; k < 3; ++k) points[0][k] = rocket.at[k];
    for (int i = 0; i < rocket.tails; ++i) {
        for (int k = 0; k < 3; ++k) points[i + 1][k] = rocket.tail[i][k];
    }
    const int count = rocket.tails + 1;
    const float half = rocket.width * kTrailWidth * kUnit * 0.5f;
    for (int s = 0; s + 1 < count; ++s) {
        const float* a = points[s];
        const float* b = points[s + 1];
        const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (length < 1e-4f) continue;
        const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
        // Two crossed quads along the segment, as the spirits' ribbons: one across it on the
        // ground's plane, one standing, so it has width from any side.
        float across[3] = {1.0f, 0.0f, 0.0f};
        if (flat > 1e-4f) {
            across[0] = -d[2] / flat;
            across[2] = d[0] / flat;
        }
        const float n[3] = {d[0] / length, d[1] / length, d[2] / length};
        const float other[3] = {n[1] * across[2] - n[2] * across[1],
                                n[2] * across[0] - n[0] * across[2],
                                n[0] * across[1] - n[1] * across[0]};
        const float uHead = 1.0f - float(s) / float(count - 1);
        const float uTail = 1.0f - float(s + 1) / float(count - 1);
        const float* sides[2] = {across, other};
        for (const float* side : sides) {
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = 0.5f * (a[k] + b[k]);
            sprite.placed = true;
            for (int k = 0; k < 3; ++k) {
                sprite.corner[0][k] = b[k] - side[k] * half;
                sprite.corner[1][k] = a[k] - side[k] * half;
                sprite.corner[2][k] = a[k] + side[k] * half;
                sprite.corner[3][k] = b[k] + side[k] * half;
            }
            const float uv[4][2] = {{uTail, 1}, {uHead, 1}, {uHead, 0}, {uTail, 0}};
            for (int k = 0; k < 4; ++k) {
                sprite.cornerUv[k][0] = uv[k][0];
                sprite.cornerUv[k][1] = uv[k][1];
            }
            for (int k = 0; k < 3; ++k) sprite.colour[k] = rocket.light[k] * kRocketTone;
            sprite.sheet = shiny_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
}

void Firework::gather(gfx::Effects& effects) const {
    // A billboard `pixels` of its sheet wide in MU's units, at `scale`.
    const auto board = [&](bgfx::TextureHandle sheet, const float at[3], float pixels, float scale,
                           const float light[3], float tone, float spin = 0.0f) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = at[k];
            sprite.colour[k] = light[k] * tone;
        }
        sprite.halfWidth = sprite.halfHeight = pixels * scale * kUnit * 0.5f;
        sprite.spin = spin;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };
    for (const Rocket& rocket : rockets_) {
        if (!rocket.alive) continue;
        if (bgfx::isValid(shiny_)) ribbon(effects, rocket);
        board(light_, rocket.at, 64.0f, 0.5f * kHeadSize, rocket.light, kRocketTone);
        board(shock_, rocket.at, 128.0f, 0.15f * kHeadSize, rocket.light, kRocketTone);
    }
    for (const Blast& blast : blasts_) {
        if (!blast.alive || !bgfx::isValid(blast_)) continue;
        // The mono blast lives twenty of the burst's thirty frames, `(20 - LifeTime) / 2` of its
        // 4x4 strip.
        const int age = int(30.0f - blast.left);
        if (age >= 20) continue;
        const int frame = age / 2;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = blast.at[k];
            sprite.colour[k] = blast.light[k] * kBlastTone;
        }
        sprite.halfWidth = sprite.halfHeight = 256.0f * 0.6f * kBlastSize * kUnit * 0.5f;
        sprite.u0 = float(frame % 4) * 0.25f + 0.005f;
        sprite.v0 = float(frame / 4) * 0.25f + 0.005f;
        sprite.u1 = sprite.u0 + 0.24f;
        sprite.v1 = sprite.v0 + 0.24f;
        sprite.sheet = blast_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    for (const Flash& flash : flashes_) {
        if (flash.alive) board(shock_, flash.at, 128.0f, flash.scale * kFlashSize, flash.light, kFlashTone);
    }
    for (const Mote& spark : sparks_) {
        if (spark.alive) {
            board(spark_, spark.at, 32.0f, spark.scale * kSparkSize, spark.light, kSparkTone);
        }
    }
    for (const Mote& speck : glitter_) {
        if (speck.alive && speck.shown) {
            board(shiny_, speck.at, 16.0f, speck.scale * kGlitterSize, speck.light, kGlitterTone);
        }
    }
    for (const Star& star : starList_) {
        if (!star.alive) continue;
        // The seven frames over its first eight, `(15 - LifeTime) / 8 * 7`, then the last held.
        const float age = 15.0f - star.left;
        const int frame = star.left > 7.0f ? std::min(6, int(age / 8.0f * 7.0f)) : 6;
        board(stars_[frame], star.at, 256.0f, star.scale * kStarSize, star.light, kStarTone,
              star.spin * kDegrees);
    }
}

}  // namespace mu::game
