#include "game/fx/inferno.h"

#include <cstdio>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

float Inferno::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Inferno::open(const std::string& assetDir, content::Textures& textures,
                   const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/inferno/";
    keyCount_ = 0;
    std::vector<EffectCorner> wall, floor;
    for (int k = 0; k < kKeys; ++k) {
        char name[32];
        std::snprintf(name, sizeof(name), "Inferno01_%02d.obj", k);
        wall.clear();
        floor.clear();
        if (!loadEffectObj(dir + name, kUnit, "ring3", wall)) break;
        loadEffectObj(dir + name, kUnit, "ring4", floor);
        // Blended key to key, so every pose must be the same corners in the same order.
        if (k > 0 && (wall.size() != wallCorners_ || wall.size() + floor.size() != poses_[0].size())) {
            break;
        }
        wallCorners_ = wall.size();
        poses_[k].clear();
        for (const std::vector<EffectCorner>* part : {&wall, &floor}) {
            for (const EffectCorner& c : *part) {
                poses_[k].push_back(Polar{std::hypot(c.x, c.z), std::atan2(c.z, c.x), c.y, c.u, c.v});
            }
        }
        keyCount_ = k + 1;
    }
    wall_.assign(wallCorners_, EffectCorner{});
    floor_.assign(keyCount_ > 0 ? poses_[0].size() - wallCorners_ : 0, EffectCorner{});
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (!core::fileExists(dir + name)) return BGFX_INVALID_HANDLE;
        return textures.load(dir + name, content::TextureRole::Albedo);
    };
    wallSheet_ = sheet("ring3.png");
    floorSheet_ = sheet("ring4.png");
    if (const content::EffectSheet* spark = table.effect("spark")) {
        spark_ = textures.load(assetDir + "/" + spark->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* blast = table.effect("explosion")) {
        blastSheet_ = textures.load(assetDir + "/" + blast->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* flare = table.effect("light")) {
        flareSheet_ = textures.load(assetDir + "/" + flare->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* smoke = table.effect("smoke01")) {
        smokeSheet_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    }
    const bool sheets = bgfx::isValid(flareSheet_) && bgfx::isValid(smokeSheet_) &&
                        bgfx::isValid(wallSheet_) && bgfx::isValid(floorSheet_) &&
                        bgfx::isValid(spark_) && bgfx::isValid(blastSheet_);
    core::logf("inferno: %d keys, wall %zu, floor %zu triangles, sheets %s", keyCount_,
               wallCorners_ / 3, floor_.size() / 3, sheets ? "yes" : "NO");
    return keyCount_ > 0 && sheets;
}

void Inferno::clear() {
    for (Ring& one : rings_) one = Ring{};
    for (Spark& one : sparks_) one = Spark{};
    for (Blast& one : blasts_) one = Blast{};
    for (Puff& one : puffs_) one = Puff{};
}

void Inferno::sparksAt(const float at[3]) {
    if (!bgfx::isValid(spark_)) return;
    const float perUnit = metres() / 100.0f;
    int made = 0;
    for (Spark& one : sparks_) {
        if (made == kSparksPerBomb) return;
        if (one.alive) continue;
        ++made;
        one = Spark{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        one.scale = float(int(unit() * 4.0f) + 4) * 0.2f;
        // MU's rand()%16 + 24, held to the ring's fifteen (inferno.h kSparkMostFrames).
        one.left = std::min(kSparkMostFrames, float(int(unit() * 16.0f) + 24));
        one.gravity = float(int(unit() * 16.0f) + 6) * perUnit;
        // (0, speed, 0) pitched 150-209 degrees and turned to a random yaw: flat out, and a
        // little up or down.
        const float speed = float(int(unit() * 20.0f) + 20) * 0.1f * 3.0f * perUnit;
        const float pitch = (float(int(unit() * 60.0f)) + 150.0f) * kDegrees;
        const float yaw = unit() * kTwoPi;
        const float flat = speed * std::fabs(std::cos(pitch));
        one.flat[0] = std::cos(yaw) * flat;
        one.flat[1] = std::sin(yaw) * flat;
        one.rise = speed * std::sin(pitch);
    }
}

void Inferno::update(float seconds) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    for (Ring& ring : rings_) {
        if (!ring.alive) continue;
        ring.left -= frames;
        ring.key = std::fmin(float(keyCount_ - 1), ring.key + kKeysPerFrame * frames);
        if (ring.left <= 0.0f) ring.alive = false;
    }
    for (Blast& one : blasts_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        if (one.wait > 0.0f) {
            one.wait -= frames;
            continue;
        }
        one.age += frames;
        one.at[1] += kPuffRise * seconds;
        if (one.age >= kPuffFrames) one.alive = false;
    }
    const float perUnit = metres() / 100.0f;
    for (Spark& one : sparks_) {
        if (!one.alive) continue;
        one.at[1] += (one.gravity + one.rise) * frames;
        one.gravity -= 2.0f * perUnit * frames;
        one.at[0] += one.flat[0] * frames;
        one.at[2] += one.flat[1] * frames;
        one.left -= frames;
        if (ground_) {
            const float floor = ground_->heightAt(one.at[0], one.at[2]);
            if (one.at[1] < floor) {
                one.at[1] = floor;
                one.gravity = -one.gravity * 0.6f;
                one.rise = 0.0f;
                one.left -= 4.0f;
            }
        }
        if (one.left <= 0.0f) one.alive = false;
    }
}

void Inferno::gatherEffects(gfx::Effects& effects) const {
    const float up[3] = {0.0f, 1.0f, 0.0f};
    for (const Ring& ring : rings_) {
        if (!ring.alive || keyCount_ == 0) continue;
        const int from = std::clamp(int(ring.key), 0, keyCount_ - 1);
        const int to = std::min(from + 1, keyCount_ - 1);
        const float t = ring.key - float(from);
        const std::vector<Polar>& a = poses_[from];
        const std::vector<Polar>& b = poses_[to];
        for (size_t i = 0; i < a.size(); ++i) {
            // The turn the short way round, then the radius and height.
            float turn = b[i].angle - a[i].angle;
            if (turn > 3.14159265f) turn -= kTwoPi;
            if (turn < -3.14159265f) turn += kTwoPi;
            const float angle = a[i].angle + turn * t;
            const float r = a[i].r + (b[i].r - a[i].r) * t;
            EffectCorner& out = i < wallCorners_ ? wall_[i] : floor_[i - wallCorners_];
            out.x = std::cos(angle) * r;
            out.z = std::sin(angle) * r;
            out.y = a[i].y + (b[i].y - a[i].y) * t;
            out.u = a[i].u;
            out.v = a[i].v;
        }
        const float s = std::sin(ring.yaw), c = std::cos(ring.yaw);
        const float x[3] = {c, 0.0f, -s};
        const float z[3] = {s, 0.0f, c};
        // `BlendMeshLight = LifeTime / 20` under its Light of 0.8.
        const float light = kLight * std::clamp(ring.left / 20.0f, 0.0f, 1.0f);
        const float colour[3] = {light, light, light};
        submitEffectAlong(effects, floor_, floorSheet_, gfx::Blend::Additive, ring.at, x, up, z,
                          kScale, colour, 1.0f);
        submitEffectAlong(effects, wall_, wallSheet_, gfx::Blend::Additive, ring.at, x, up, z,
                          kScale, colour, 1.0f);
    }
    // The blasts, Explotion01's flipbook walked a cell every two frames as fx/meteor.h walks it,
    // added; the ring's own tint and size (the head of inferno.h).
    const float perUnit = metres() / 100.0f;
    const float side = 1.0f / float(kBlastGrid);
    for (const Blast& one : blasts_) {
        if (!one.alive || !bgfx::isValid(blastSheet_)) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = kBlastUnits * kBlastShare * perUnit * 0.5f;
        sprite.spin = one.spin;
        const float fade = std::clamp(one.left / (kBlastFrames * kBlastFadeShare), 0.0f, 1.0f);
        // The soft flare under it first, so the fire's edge melts into its neighbours'.
        if (bgfx::isValid(flareSheet_)) {
            gfx::Sprite flare = sprite;
            flare.halfWidth = flare.halfHeight = sprite.halfWidth * kFlareShare;
            for (int k = 0; k < 3; ++k) flare.colour[k] = kFlareTint[k] * fade;
            flare.sheet = flareSheet_;
            flare.blend = gfx::Blend::Additive;
            if (!effects.add(flare)) return;
        }
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kBlastTint[k] * fade;
        sprite.sheet = blastSheet_;
        sprite.blend = gfx::Blend::Additive;
        const int step = std::clamp(
            int((kBlastFrames - one.left) / kBlastFrames * float(kBlastCells)), 0, kBlastCells - 1);
        sprite.u0 = float(step % kBlastGrid) * side + kBlastInset;
        sprite.v0 = float(step / kBlastGrid) * side + kBlastInset;
        sprite.u1 = sprite.u0 + side - kBlastInset * 2.0f;
        sprite.v1 = sprite.v0 + side - kBlastInset * 2.0f;
        if (!effects.add(sprite)) return;
    }
    // The smoke after it: smoke01 mixed as forge's lamp smoke is, opening as it rises, in and
    // out softly.
    for (const Puff& one : puffs_) {
        if (!one.alive || one.wait > 0.0f || !bgfx::isValid(smokeSheet_)) continue;
        const float t = std::clamp(one.age / kPuffFrames, 0.0f, 1.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = 0.5f * (kPuffBorn + (kPuffGrown - kPuffBorn) * t);
        sprite.spin = one.spin + t * 0.6f;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kPuffGrey;
        const float in = std::min(1.0f, t / 0.15f);
        const float out = 1.0f - std::clamp((t - 0.35f) / 0.65f, 0.0f, 1.0f);
        sprite.colour[3] = kPuffAlpha * in * out;
        sprite.sheet = smokeSheet_;
        sprite.blend = gfx::Blend::Smoke;
        if (!effects.add(sprite)) return;
    }
    // The motes, as forge draws its own: Light = LifeTime / 16 read as heat, white-hot and
    // cooling (fx/forge.cpp).
    for (const Spark& one : sparks_) {
        if (!one.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = kMotePixels * one.scale * kUnit * 0.5f;
        const float light = std::clamp(one.left / kMoteBright, 0.0f, 1.0f);
        sprite.colour[0] = kSparkShape;
        sprite.colour[1] = light * kMoteHeat;
        sprite.colour[2] = 0.0f;
        sprite.colour[3] = std::min(1.0f, light * 2.0f);
        sprite.sheet = spark_;
        sprite.blend = gfx::Blend::Flame;
        if (!effects.add(sprite)) return;
    }
}

uint32_t Inferno::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Ring& ring : rings_) {
        if (!ring.alive || count >= max) continue;
        // With the blasts: whole at the burst, dimmed over their last third, gone with them.
        const float lit = std::clamp(ring.left / (kBlastFrames * kBlastFadeShare), 0.0f, 1.0f);
        gfx::PointLight& one = out[count++];
        for (int k = 0; k < 3; ++k) one.position[k] = ring.at[k];
        one.reach = kGlowTiles * metres();
        one.height = 1.0f;
        for (int k = 0; k < 3; ++k) one.colour[k] = kGlow[k] * lit;
    }
    return count;
}

}  // namespace mu::game
