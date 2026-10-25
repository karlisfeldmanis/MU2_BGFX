#include "game/fx/strike_of_destruction.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;
// MU's tints (MoveHandlers.cpp:7549-7634): the blades' and cracks' (0.3, 0.3, 1.0), the flares'
// and the splash's (0.5, 0.5, 1.0).
// **Ours, fire** (the user, 2026-10-25: "more like fire"; the realm strikes in fire and lays a
// burn): every blue of MU's turned to a forge's orange -- the cracks' and blades' (0.3, 0.3, 1.0)
// to kCrackLight, the flares' (0.5, 0.5, 1.0) to kFlareTint -- the floor flare laid with the
// neutral flare01 for MU's blue flareBlue, and the splash MU's flame01 tongues and rising ember
// speckles for its blue water spray.
constexpr float kCrackLight[3] = {1.0f, 0.42f, 0.12f};
constexpr float kFlareTint[3] = {1.0f, 0.55f, 0.2f};
constexpr float kFlameTint[3] = {1.0f, 0.85f, 0.7f};  // flame01 is orange in its own pixels
// The impact: 100 units before him, 20 to his side (`(-20, -100, 0)` turned by his angle).
constexpr float kAhead = 100.0f, kSide = -20.0f;
// The sheets' pixels: a MU sprite is its bitmap's width times its scale, in units.
constexpr float kSheetPixels = 64.0f;
// The floor flares, laid on the land in cells no wider than this, as fx/sleep's circle is.
constexpr float kCell = 0.75f;
constexpr float kLift = 5.0f;          // RenderTerrainAlphaBitmap's own lift, in MU units
constexpr float kOverGround = 0.03f;   // ours: every crack three centimetres over the ground
// The line of cracks: one every 55 units toward the point, `length / 100 + 1` of them.
constexpr float kLineStep = 55.0f;

// **Ours, the look against the footage** (docs/plans/ancient-runes/strike-of-destruction-footage.jpg:
// a white-cyan floor burst and violet blades through the body). MU adds these on an 8-bit screen
// where a bright sheet clips toward white; added linearly they read darker and more saturated,
// and a white smoke at full light swallowed the burst.
constexpr float kBladeLight = 1.0f;    // the blades, x MU's (0.3, 0.3, 1.0)
constexpr float kCrackGain = 1.0f;     // the cracks, x MU's (0.3, 0.3, 1.0)
constexpr float kFloorGain = 1.0f;     // the floor flares, x the controller's Light
// The smoke: MU's white at a quarter, leant blue, so the spray reads as the footage's blue haze
// and not a grey cloud over the burst (seen offscreen, 2026-10-25).
constexpr float kSmokeLight = 0.25f;   // the smoke, x its Light
constexpr float kSmokeTint[3] = {0.75f, 0.8f, 1.0f};
constexpr float kSplashLight = 1.0f;   // the blue splash, x MU's (0.5, 0.5, 1.0) x its Light
// The camera shake, ours: half MU's (rand() % 8 - 4) x 0.1, and only the hero's own.
constexpr float kQuakeShare = 0.5f;
constexpr float kQuakeDecay = 0.2f, kQuakeEpsilon = 0.0001f;
// **Ours, calmer** (the user, 2026-10-25: "its to haotics"). MU's two halves -- the impact a metre
// before him and the burst at the point -- were split by his leap; a rune's proc has none, so in
// melee they lay one over the other a metre apart. Everything is drawn at the point: no impact
// flares, crack A or line of cracks, the point's two blade crowns and not the impact's two, no
// stones, no smoke, and the splash a few a frame for six frames where MU's is 15 for ten.
constexpr bool kImpact = false;      // MU's impact half before him
constexpr int kSplashCalm = 4;       // drops a frame, of MU's 15
constexpr int kSplashCalmFrames = 6;

float clampOne(float v) { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

float StrikeOfDestruction::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool StrikeOfDestruction::open(const std::string& assetDir, content::Textures& textures,
                               const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/destruction/";
    const auto take = [&](const std::string& path) -> bgfx::TextureHandle {
        if (!core::fileExists(path)) return BGFX_INVALID_HANDLE;
        return textures.load(path, content::TextureRole::Albedo);
    };
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    blade_.clear();
    loadEffectObj(dir + "NightWater01.obj", kUnit, "Swordeff_mono", blade_);
    bladeSheet_ = take(dir + "swordeff_mono.png");
    // Each crack's groups in its .obj's order, each at all five keys.
    const char* names[kCracks] = {"PlanCrackA", "PlanCrackB", "PlanCrackGrand"};
    const std::vector<const char*> groups[kCracks] = {
        {"grandmark2", "lines2"}, {"grandmark", "lines2"}, {"grandmark2", "grandmark", "lines2"}};
    size_t most = 0;
    bool whole = true;
    for (int c = 0; c < kCracks; ++c) {
        cracks_[c].clear();
        cracks_[c].resize(groups[c].size());
        for (size_t g = 0; g < groups[c].size(); ++g) {
            Mesh& mesh = cracks_[c][g];
            mesh.sheet = take(dir + groups[c][g] + ".png");
            // A and B scroll their columns; the grand crack does not (ZzzObject.cpp:617-628).
            mesh.streams = c != kGrand && std::string(groups[c][g]) == "lines2";
            for (int k = 0; k < kKeys; ++k) {
                char file[64];
                std::snprintf(file, sizeof(file), "%s_%02d.obj", names[c], k);
                loadEffectObj(dir + file, kUnit, groups[c][g], mesh.poses[k]);
                whole = whole && !mesh.poses[k].empty() &&
                        mesh.poses[k].size() == mesh.poses[0].size();
            }
            most = std::max(most, mesh.poses[0].size());
        }
    }
    blend_.reserve(most);  // resized under it each draw, never past: a frame allocates nothing
    white_ = sheet("destruction_flare");     // BITMAP_SWORD_EFFECT_MONO, Effect/Swordeff_mono
    blue_ = sheet("light");                  // ours, fire: flare01 for MU's Effect/flareBlue
    light_ = sheet("light");                 // BITMAP_LIGHT, Effect/flare01
    speckle_ = sheet("destruction_speckle");  // BITMAP_WATERFALL_5, Effect/waterFall5
    spray_ = sheet("flame");                 // ours, fire: flame01 for MU's Effect/waterFall3
    smoke_ = sheet("smoke01");               // BITMAP_SMOKE, Effect/smoke01
    whole = whole && !blade_.empty() && bgfx::isValid(bladeSheet_);
    core::logf("strike of destruction: blades and cracks %s, flares %s, splash %s",
               whole ? "yes" : "NO",
               bgfx::isValid(white_) && bgfx::isValid(blue_) && bgfx::isValid(light_) ? "yes" : "NO",
               bgfx::isValid(speckle_) && bgfx::isValid(spray_) && bgfx::isValid(smoke_) ? "yes"
                                                                                       : "NO");
    return whole;
}

void StrikeOfDestruction::lay(Placed& out, const float at[3], float yaw) const {
    const float c = std::cos(yaw), s = std::sin(yaw);
    out.x[0] = c, out.x[1] = 0.0f, out.x[2] = s;
    out.y[0] = 0.0f, out.y[1] = 1.0f, out.y[2] = 0.0f;
    out.z[0] = -s, out.z[1] = 0.0f, out.z[2] = c;
    out.at[0] = at[0];
    out.at[2] = at[2];
    out.at[1] = (ground_ ? ground_->heightAt(at[0], at[2]) : at[1]) + kOverGround;
}

bool StrikeOfDestruction::cast(const float caster[3], const float target[3], uint32_t caster_id,
                               bool shake) {
    (void)caster_id;
    if (playing_) return false;  // ours: one at a time (see the header)
    float f[2] = {target[0] - caster[0], target[2] - caster[2]};
    const float flat = std::sqrt(f[0] * f[0] + f[1] * f[1]);
    if (flat > 1e-3f) {
        f[0] /= flat;
        f[1] /= flat;
    } else {
        f[0] = 0.0f;
        f[1] = 1.0f;
    }
    yaw_ = std::atan2(f[1], f[0]);
    // 100 units before him and 20 to his side.
    const float side[2] = {-f[1], f[0]};
    impact_[0] = caster[0] + (f[0] * kAhead + side[0] * kSide) * kUnit;
    impact_[2] = caster[2] + (f[1] * kAhead + side[1] * kSide) * kUnit;
    impact_[1] = ground_ ? ground_->heightAt(impact_[0], impact_[2]) : caster[1];
    for (int k = 0; k < 3; ++k) target_[k] = target[k];
    playing_ = true;
    shake_ = shake;
    owed_ = 0.0f;
    frame_ = 0;
    clock_ = 0.0f;
    glow_ = 1.2f;
    laid_ = false;
    bladeAge_ = crackAge_ = 0;
    lineCount_ = 0;
    step();  // ours: MU's frame 16, drawn on the tick it is struck
    return true;
}

void StrikeOfDestruction::step() {
    quake_ *= kQuakeDecay;
    if (std::fabs(quake_) < kQuakeEpsilon) quake_ = 0.0f;
    // The splash and the smoke, born or not.
    bool dropsLive = false;
    for (Drop& one : drops_) {
        if (!one.alive) continue;
        if (one.smoke) {
            // Subtype 55: rising faster each frame, growing, its light /1.08.
            one.rise += 0.1f;
            one.light /= 1.08f;
        } else {
            // Ours, fire: MU's subtype 8 is thrown up and falls 0.6 a frame like water; a flame
            // and an ember drift up, slowing, its light /1.1 as MU's.
            one.rise = std::fmax(one.rise * 0.9f, 0.6f);
            one.light /= 1.1f;
        }
        one.at[1] += one.rise * kUnit;
        one.scale += 0.05f;
        if (--one.life <= 0) one.alive = false;
        dropsLive = dropsLive || one.alive;
    }
    if (!playing_) return;
    if (frame_ < kLife) {
        // Ten frames of shake and splash (MU's LifeTime 24 to 15).
        if (frame_ < kSplashCalmFrames) {
            if (shake_) quake_ = float(int(unit() * 8.0f) - 4) * 0.1f * kQuakeShare;
            const float floor = ground_ ? ground_->heightAt(target_[0], target_[2]) : target_[1];
            for (int i = 0; i < kSplashCalm; ++i) {
                float at[3] = {target_[0] + float(int(unit() * 300.0f) - 150) * kUnit, 0.0f,
                               target_[2] + float(int(unit() * 300.0f) - 150) * kUnit};
                // Ours: born low, from the ground up to 100 units over it, to rise (fire).
                at[1] = floor + float(int(unit() * 100.0f)) * kUnit;
                const bool speckle = unit() < 0.5f;
                const float scale = 1.6f + float(int(unit() * 10.0f)) * 0.1f;
                for (Drop& one : drops_) {
                    if (one.alive) continue;
                    one = Drop{};
                    one.alive = true;
                    one.speckle = speckle;
                    for (int k = 0; k < 3; ++k) one.at[k] = at[k];
                    if (speckle) {
                        one.rise = float(int(unit() * 3.0f) + 1);
                        one.scale = scale + float(int(unit() * 5.0f) + 5) * 0.05f;
                    } else {
                        one.rise = float(int(unit() * 5.0f) + 5);
                        one.scale = float(int(unit() * 10.0f) + 10) * 0.05f;
                    }
                    one.spin = unit() * 6.2831853f;
                    break;
                }
                if (false) {  // ours: no smoke (kImpact's note)
                    for (Drop& one : drops_) {
                        if (one.alive) continue;
                        one = Drop{};
                        one.alive = true;
                        one.smoke = true;
                        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
                        one.scale = 1.0f + float(int(unit() * 32.0f) + 48) * 0.01f;
                        one.spin = unit() * 6.2831853f;
                        break;
                    }
                }
                dropsLive = true;
            }
        }
        // MU's frame 17, once: the blades, the cracks and the stones.
        if (frame_ == 1 && !laid_) {
            laid_ = true;
            const float* at[kBlades] = {impact_, impact_, target_, target_};
            const bool shown[kBlades] = {kImpact, kImpact, true, true};
            const float scales[kBlades] = {1.0f, 1.0f, 2.0f, 1.0f};
            for (int b = 0; b < kBlades; ++b) {
                lay(blades_[b].where, at[b], unit() * 6.2831853f);
                blades_[b].scale = scales[b];
                blades_[b].alpha = shown[b] ? 1.0f : 0.0f;
            }
            lay(crackA_, impact_, yaw_);
            lay(grand_, target_, unit() * 6.2831853f);
            const float dx = target_[0] - impact_[0], dz = target_[2] - impact_[2];
            const float length = std::sqrt(dx * dx + dz * dz) / kUnit;
            lineCount_ = kImpact ? std::min(kLineMost, int(length / 100.0f) + 1) : 0;
            const float way = std::atan2(dz, dx);
            for (int i = 0; i < lineCount_; ++i) {
                const float along = kLineStep * float(i) * kUnit;
                const float there[3] = {impact_[0] + std::cos(way) * along, impact_[1],
                                        impact_[2] + std::sin(way) * along};
                const float off = float(int(unit() * 20.0f) + 10) * kDegrees;
                // Its streak (the model's X) along the line, off it one way and then the other.
                lay(line_[i], there, way + (i % 2 == 0 ? off : -off));
            }
            stonesOwed_ = kImpact ? 5 + int(unit() * 3.0f) : 0;
        }
        glow_ /= 1.05f;
    }
    if (laid_) {
        ++bladeAge_;
        ++crackAge_;
        for (Blade& one : blades_) one.alpha -= 0.04f;
    }
    ++frame_;
    // Done once the grand crack (40 frames at -0.03, gone by 34) and every drop are out.
    if (frame_ > 36 && !dropsLive) playing_ = false;
}

void StrikeOfDestruction::floorFlare(gfx::Effects& effects, const float at[3], float yaw,
                                     float cells, float light) const {
    if (!bgfx::isValid(blue_) || ground_ == nullptr || light <= 0.0f) return;
    const float across = cells * ground_->metresPerTile();
    const int n = std::clamp(int(std::ceil(across / kCell)), 1, 12);
    const float c = std::cos(yaw), s = std::sin(yaw);
    const auto place = [&](float u, float v, float* out) {
        const float x = at[0] + (u * c - v * s) * across;
        const float z = at[2] + (u * s + v * c) * across;
        out[0] = x;
        out[1] = ground_->heightAt(x, z) + kLift * kUnit;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = blue_;
    sprite.blend = gfx::Blend::Additive;
    for (int k = 0; k < 3; ++k) sprite.colour[k] = clampOne(light * kFloorGain * kFlareTint[k]);
    sprite.colour[3] = 1.0f;
    const float inv = 1.0f / float(n);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float u0 = float(i) * inv - 0.5f, u1 = u0 + inv;
            const float v0 = float(j) * inv - 0.5f, v1 = v0 + inv;
            const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            for (int k = 0; k < 4; ++k) {
                place(uv[k][0], uv[k][1], sprite.corner[k]);
                sprite.cornerUv[k][0] = uv[k][0] + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - uv[k][1];
            }
            place((u0 + u1) * 0.5f, (v0 + v1) * 0.5f, sprite.position);
            effects.add(sprite);
        }
    }
}

void StrikeOfDestruction::drawCrack(gfx::Effects& effects, int which, const Placed& where,
                                    float scale, float key, float alpha) const {
    if (alpha <= 0.0f) return;
    const int k0 = std::clamp(int(key), 0, kKeys - 1), k1 = std::min(k0 + 1, kKeys - 1);
    const float t = std::clamp(key - float(k0), 0.0f, 1.0f);
    // -WorldTime % 2000 x 0.0001 on the columns' V.
    const float scroll = -std::fmod(clock_ * 1000.0f, 2000.0f) * 0.0001f;
    float lit[3];
    for (int k = 0; k < 3; ++k) lit[k] = clampOne(kCrackLight[k] * kCrackGain * alpha);
    for (const Mesh& mesh : cracks_[which]) {
        const std::vector<EffectCorner>& a = mesh.poses[k0];
        const std::vector<EffectCorner>& b = mesh.poses[k1];
        const size_t n = std::min({a.size(), b.size(), blend_.capacity()});
        blend_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            EffectCorner& out = blend_[i];
            out = a[i];
            out.x = a[i].x + (b[i].x - a[i].x) * t;
            out.y = a[i].y + (b[i].y - a[i].y) * t;
            out.z = a[i].z + (b[i].z - a[i].z) * t;
            if (mesh.streams) out.v += scroll;
        }
        submitEffectAlong(effects, blend_, mesh.sheet, gfx::Blend::Additive, where.at, where.x,
                          where.y, where.z, scale, lit, 1.0f);
    }
}

void StrikeOfDestruction::gather(gfx::Effects& effects) const {
    if (playing_ && frame_ <= kLife) {
        const float light = glow_;
        // The impact's floor flare, four cells, and its white flare 65 up at 3.0-3.1.
        // Ours: the white flare at the point, the impact's floor flare only with kImpact.
        if (kImpact) floorFlare(effects, impact_, yaw_, 4.0f, light);
        const float* flash = kImpact ? impact_ : target_;
        if (bgfx::isValid(white_)) {
            gfx::Sprite sprite;
            sprite.position[0] = flash[0];
            sprite.position[1] = flash[1] + 65.0f * kUnit;
            sprite.position[2] = flash[2];
            sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetPixels * 3.05f * kUnit;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = clampOne(light * kFlareTint[k]);
            sprite.sheet = white_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
        // The point's floor flare, six cells, and the big blue light 100 up at scale 5.
        floorFlare(effects, target_, yaw_, 6.0f, light);
        if (bgfx::isValid(light_)) {
            gfx::Sprite sprite;
            sprite.position[0] = target_[0];
            sprite.position[1] = target_[1] + 100.0f * kUnit;
            sprite.position[2] = target_[2];
            sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetPixels * 5.0f * kUnit;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = clampOne(light * kFlareTint[k]);
            sprite.sheet = light_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    if (playing_ && laid_) {
        // The blades.
        for (const Blade& one : blades_) {
            const float alpha = clampOne(one.alpha);
            if (alpha <= 0.0f) continue;
            float lit[3];
            for (int k = 0; k < 3; ++k) lit[k] = clampOne(kCrackLight[k] * kBladeLight * alpha);
            const Placed& w = one.where;
            submitEffectAlong(effects, blade_, bladeSheet_, gfx::Blend::Additive, w.at, w.x, w.y,
                              w.z, one.scale, lit, 1.0f);
        }
        // The cracks, their columns sinking over five keys at 0.3 a frame.
        const float age = float(crackAge_);
        const float key = std::min(float(kKeys - 1), age * 0.3f);
        const float small = 1.0f - 0.04f * age;
        if (kImpact) drawCrack(effects, kCrackA, crackA_, 1.2f, key, small);
        for (int i = 0; i < lineCount_; ++i) drawCrack(effects, kCrackB, line_[i], 1.0f, key, small);
        drawCrack(effects, kGrand, grand_, 1.2f, key, 1.0f - 0.03f * age);
    }
    // The splash and the smoke.
    for (const Drop& one : drops_) {
        if (!one.alive || one.light <= 0.01f) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetPixels * one.scale * kUnit;
        sprite.spin = one.spin;
        sprite.blend = gfx::Blend::Additive;
        if (one.smoke) {
            sprite.sheet = smoke_;
            for (int k = 0; k < 3; ++k) {
                sprite.colour[k] = clampOne(one.light * kSmokeLight * kSmokeTint[k]);
            }
        } else {
            sprite.sheet = one.speckle ? speckle_ : spray_;
            const float* tint = one.speckle ? kFlareTint : kFlameTint;
            for (int k = 0; k < 3; ++k) {
                sprite.colour[k] = clampOne(one.light * tint[k] * kSplashLight);
            }
        }
        if (!bgfx::isValid(sprite.sheet)) continue;
        effects.add(sprite);
    }
}

}  // namespace mu::game
