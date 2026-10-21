#include "game/fx/pollution.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;
constexpr float kLift = 5.0f;       // RenderTerrainAlphaBitmap's own, in units
constexpr float kCell = 0.5f;       // the land followed in cells no wider (fx/warp's)
constexpr float kSpeed = 10.0f;     // units a frame, the joint's Velocity
constexpr float kTurn = 5.0f;       // MoveHumming's degrees a frame
constexpr float kAim = 80.0f;       // units over the target
constexpr float kBorn = 150.0f;     // units either way about the target
constexpr float kWide = 100.0f, kThin = 20.0f;   // the tails' Scale, in units
constexpr float kLagulScale = 100.0f / 70.0f;
constexpr float kKeysAFrame = 0.5f;  // `o->Velocity = 0.5f`
constexpr float kTrail[3] = {0.7f, 0.7f, 0.9f};
constexpr float kBoneBlue[3] = {0.2f, 0.3f, 1.0f};
constexpr float kShinyPixels = 64.0f;
// zx01-zx06 (bones 54-59) as the lagulbody corners nearest each at key 0, within seven units.
constexpr int kBoneCorners[6] = {3311, 1740, 1511, 1752, 1529, 1750};
// The casting circle's rings: model, light, turn a frame (MoveHandlers.cpp:1140-1161).
struct RingRow {
    const char* path;   // under effects/
    const char* sheet;
    float light[3];
    float turn;
};
constexpr RingRow kRings[7] = {
    {"pollution/Suhwanzin1.obj", "pollution/testmsop.png", {0.6f, 0.3f, 0.9f}, -3.0f},
    {"pollution/Suhwanzin11.obj", "pollution/testmsop.png", {0.6f, 0.3f, 0.9f}, 3.0f},
    {"pollution/Suhwanzin111.obj", "pollution/testmsop.png", {0.6f, 0.3f, 0.9f}, -3.0f},
    {"weakness/Suhwanzin2.obj", "weakness/monolines.png", {0.8f, 0.1f, 0.6f}, 3.0f},
    {"weakness/Suhwanzin22.obj", "weakness/monolines.png", {0.8f, 0.1f, 0.6f}, -3.0f},
    {"weakness/Suhwanzin222.obj", "weakness/monolines.png", {0.8f, 0.1f, 0.6f}, 3.0f},
    {"pollution/Suhwanzin4.obj", "pollution/monolines.png", {0.9f, 0.1f, 1.0f}, 0.0f},
};

float clampOne(float v) { return std::clamp(v, 0.0f, 1.0f); }

// TurnAngle2's walk: toward `to` by at most `step` degrees, the short way round.
float turnToward(float from, float to, float step) {
    float gap = std::fmod(to - from, 360.0f);
    if (gap > 180.0f) gap -= 360.0f;
    if (gap < -180.0f) gap += 360.0f;
    return from + std::clamp(gap, -step, step);
}

}  // namespace

float Pollution::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Pollution::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/";
    const auto take = [&](const std::string& path) -> bgfx::TextureHandle {
        if (!core::fileExists(path)) return BGFX_INVALID_HANDLE;
        return textures.load(path, content::TextureRole::Albedo);
    };
    for (int i = 0; i < kRingModels; ++i) {
        rings_[i].clear();
        loadEffectObj(dir + kRings[i].path, kUnit, "", rings_[i]);
        ringSheet_[i] = take(dir + kRings[i].sheet);
    }
    const char* groups[kGroups] = {"lagulbody", "lagulpinna", "watermarkhik01"};
    const char* sheets[kGroups] = {"lagulbody.png", "lagulpinna.png", "watermarkhik01.png"};
    bool whole = true;
    for (int p = 0; p < kPoses; ++p) {
        const std::string pose = dir + "pollution/Lagul01_0" + std::to_string(p) + ".obj";
        for (int g = 0; g < kGroups; ++g) {
            lagul_[p][g].clear();
            loadEffectObj(pose, kUnit, groups[g], lagul_[p][g]);
            whole = whole && !lagul_[p][g].empty() && lagul_[p][g].size() == lagul_[0][g].size();
        }
    }
    for (int g = 0; g < kGroups; ++g) {
        lagulSheet_[g] = take(dir + "pollution/" + sheets[g]);
        blend_[g].resize(lagul_[0][g].size());
    }
    whole = whole && lagul_[0][0].size() > size_t(kBoneCorners[0]) && bgfx::isValid(lagulSheet_[0]);
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    light_ = sheet("light");            // BITMAP_LIGHT
    impact_ = sheet("move_rings");      // BITMAP_SUMMON_IMPACT, empact01
    spirit_ = sheet("joint_spirit");    // BITMAP_JOINT_SPIRIT
    smoke_ = sheet("smoke01");          // BITMAP_SMOKE
    cloud_ = sheet("cloud");            // BITMAP_CLOUD
    shiny_ = sheet("weakness_shiny");   // BITMAP_SHINY + 6
    core::logf("pollution: lagul %s, rings %s, spirit %s", whole ? "yes" : "NO",
               rings_[0].empty() || rings_[6].empty() ? "NO" : "yes",
               bgfx::isValid(spirit_) ? "yes" : "NO");
    return whole;
}

void Pollution::cast(const float caster[3], const float target[3]) {
    for (Circle& one : circles_) {
        if (one.alive) continue;
        one = Circle{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = caster[k];
        one.left = 44.0f;
        break;
    }
    int made = 0;
    for (Ring& one : ringsLive_) {
        if (made == kRingModels) break;
        if (one.alive) continue;
        one = Ring{};
        one.alive = true;
        one.model = made++;
        for (int k = 0; k < 3; ++k) one.at[k] = caster[k];
        const bool four = one.model == 6;
        one.scale = four ? 1.0f : 0.9f;
        one.left = four ? 25.0f : 40.0f;
    }
    // **Ours**: one Pollution's ghosts at a time. Four Laguls are 5,020 triangles in the
    // transparent pass, and a second cast inside the first's 6.4 s ran it past its 8,192: the
    // new one takes the old one's place.
    for (Joint& joint : joints_) joint.alive = false;
    for (Spring& one : springs_) one.alive = false;
    for (Spring& one : springs_) {
        if (one.alive) continue;
        one = Spring{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = target[k];
        one.left = kLife;
        break;
    }
    // MU's quarters, a wide joint (and its Lagul) and a thin one each (kJointsACast).
    made = 0;
    for (Joint& joint : joints_) {
        if (made == kJointsACast) break;
        if (joint.alive) continue;
        const int quarter = made / 2;
        joint = Joint{};
        joint.alive = true;
        joint.wide = made % 2 == 0;
        joint.at[0] = target[0] + (unit() * 2.0f * kBorn - kBorn) * kUnit;
        joint.at[1] = target[1];
        joint.at[2] = target[2] + (unit() * 2.0f * kBorn - kBorn) * kUnit;
        joint.aim[0] = target[0];
        joint.aim[1] = target[1] + kAim * kUnit;
        joint.aim[2] = target[2];
        joint.yaw = float(quarter) * 90.0f;
        joint.key = unit() * 12.0f;
        ++made;
    }
}

void Pollution::puff(bool cloud, const float at[3], float size, const float tint[3]) {
    for (Puff& one : puffs_) {
        if (one.alive) continue;
        one = Puff{};
        one.alive = true;
        one.cloud = cloud;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = at[k];
            one.tint[k] = tint[k];
        }
        one.size = size;
        one.life = cloud ? 40.0f : 30.0f;
        one.spin = unit() * 6.2831853f;
        return;
    }
}

void Pollution::fly(Joint& joint) {
    for (int i = kTails - 1; i > 0; --i) {
        for (int k = 0; k < 3; ++k) joint.tail[i][k] = joint.tail[i - 1][k];
    }
    for (int k = 0; k < 3; ++k) joint.tail[0][k] = joint.at[k];
    joint.tails = std::min(joint.tails + 1, kTails);
    // MoveHumming toward the point over the target, then MU's wobble.
    const float dx = joint.aim[0] - joint.at[0], dz = joint.aim[2] - joint.at[2];
    const float flat = std::sqrt(dx * dx + dz * dz);
    joint.yaw = turnToward(joint.yaw, std::atan2(dz, dx) / kDegrees, kTurn);
    joint.pitch =
        turnToward(joint.pitch, std::atan2(joint.aim[1] - joint.at[1], flat) / kDegrees, kTurn);
    joint.turnPitch += float(int(unit() * 32.0f) - 16) * 0.2f;
    joint.turnYaw += float(int(unit() * 32.0f) - 16) * 0.8f;
    joint.pitch += joint.turnPitch;
    joint.yaw += joint.turnYaw;
    joint.turnPitch *= 0.6f;
    joint.turnYaw *= 0.8f;
    const float cp = std::cos(joint.pitch * kDegrees);
    joint.at[0] += cp * std::cos(joint.yaw * kDegrees) * kSpeed * kUnit;
    joint.at[1] += std::sin(joint.pitch * kDegrees) * kSpeed * kUnit;
    joint.at[2] += cp * std::sin(joint.yaw * kDegrees) * kSpeed * kUnit;
    joint.key = std::fmod(joint.key + kKeysAFrame, 12.0f);
    joint.left -= 1.0f;
    // The Lagul's smoke off it, one frame in five (rand_fps_check(5)).
    if (joint.wide && unit() < 0.2f) {
        const float tint[3] = {0.6f, 0.5f, 0.9f};
        const float at[3] = {joint.at[0], joint.at[1] + 0.5f, joint.at[2]};
        puff(false, at, 0.5f, tint);
    }
    if (joint.left <= 0.0f) joint.alive = false;
}

void Pollution::update(float seconds) {
    const float frames = seconds * kFps;
    for (Circle& one : circles_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        // Subtype 10's darkness, 44 frames; subtype 9's rings, 40 of them, ending together.
        one.darkAlpha += (one.left < 20.0f ? -0.03f : (one.darkAlpha < 1.0f ? 0.05f : 0.0f)) * frames;
        one.darkAlpha = std::clamp(one.darkAlpha, 0.0f, 1.0f);
        one.ringAlpha += (one.left < 24.0f ? -0.05f : 0.0f) * frames;
        one.ringAlpha = std::clamp(one.ringAlpha, 0.0f, 1.0f);
        one.ringTurn[0] -= 8.0f * frames;
        one.ringTurn[1] += 4.0f * frames;
    }
    for (Ring& one : ringsLive_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        if (one.left < 20.0f) {
            one.glow -= 0.03f * frames;
        } else if (one.glow < 0.5f) {
            one.glow += 0.05f * frames;
        }
        one.glow = std::clamp(one.glow, 0.0f, 0.5f);
        one.turn += kRings[one.model].turn * frames;
        if (one.model == 6) one.scale += 0.6f * frames;
    }
    for (Spring& one : springs_) {
        if (!one.alive) continue;
        one.owed += frames;
        while (one.owed >= 1.0f && one.left > 0.0f) {
            one.owed -= 1.0f;
            one.left -= 1.0f;
            // anEffectVolume[2]: one frame in three, the top tier's (ours: the joints are the
            // +7 book's, the spring kept as the top's).
            if (unit() >= 1.0f / 3.0f) continue;
            const float white[3] = {1.0f, 1.0f, 1.0f};
            const float smoke[3] = {one.at[0] + (unit() * 500.0f - 250.0f) * kUnit, one.at[1] + 0.3f,
                                    one.at[2] + (unit() * 500.0f - 250.0f) * kUnit};
            puff(false, smoke, 1.0f, white);
            const float violet[3] = {0.6f, 0.1f, 1.0f};
            const float cloud[3] = {one.at[0] + (unit() * 400.0f - 200.0f) * kUnit, one.at[1] + 0.3f,
                                    one.at[2] + (unit() * 400.0f - 200.0f) * kUnit};
            puff(true, cloud, 1.1f, violet);
        }
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Joint& joint : joints_) {
        if (!joint.alive) continue;
        joint.owed += frames;
        while (joint.owed >= 1.0f && joint.alive) {
            joint.owed -= 1.0f;
            fly(joint);
        }
    }
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        one.age += frames;
        one.size *= std::pow(1.01f, frames);
        one.at[1] += (one.cloud ? 0.3f : 0.6f) * kUnit * frames;
        if (one.age >= one.life) one.alive = false;
    }
}

float Pollution::lightOf(const Joint& joint) const {
    if (joint.left < 30.0f) return std::max(0.0f, joint.left / 30.0f);
    if (joint.left > 144.0f) return std::clamp((kLife - joint.left) / 15.0f, 0.0f, 1.0f);
    return 1.0f;
}

void Pollution::gatherGround(gfx::Effects& effects, bgfx::TextureHandle sheet, gfx::Blend blend,
                             const float at[3], float across, float turnDegrees,
                             const float colour[3]) const {
    if (!bgfx::isValid(sheet) || ground_ == nullptr || across <= 0.0f) return;
    if (colour[0] + colour[1] + colour[2] <= 0.0f) return;
    const int cells = std::clamp(int(std::ceil(across / kCell)), 1, 8);
    const float c = std::cos(turnDegrees * kDegrees), s = std::sin(turnDegrees * kDegrees);
    const auto place = [&](float u, float v, float* out) {
        const float x = at[0] + u * across;
        const float z = at[2] + v * across;
        out[0] = x;
        out[1] = ground_->heightAt(x, z) + kLift * kUnit;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = sheet;
    sprite.blend = blend;
    for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k];
    sprite.colour[3] = 1.0f;
    const float inv = 1.0f / float(cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float u0 = float(i) * inv - 0.5f, u1 = u0 + inv;
            const float v0 = float(j) * inv - 0.5f, v1 = v0 + inv;
            const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            for (int k = 0; k < 4; ++k) {
                place(uv[k][0], uv[k][1], sprite.corner[k]);
                // The sheet turned about its middle, as RenderTerrainAlphaBitmap's Rotation.
                const float ru = c * uv[k][0] - s * uv[k][1], rv = s * uv[k][0] + c * uv[k][1];
                sprite.cornerUv[k][0] = ru + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - rv;
            }
            place((u0 + u1) * 0.5f, (v0 + v1) * 0.5f, sprite.position);
            effects.add(sprite);
        }
    }
}

void Pollution::gatherTail(gfx::Effects& effects, const Joint& joint) const {
    if (!bgfx::isValid(spirit_) || joint.tails == 0) return;
    float points[kTails + 1][3];
    for (int k = 0; k < 3; ++k) points[0][k] = joint.at[k];
    for (int i = 0; i < joint.tails; ++i) {
        for (int k = 0; k < 3; ++k) points[i + 1][k] = joint.tail[i][k];
    }
    const int count = joint.tails + 1;
    const float light = lightOf(joint);
    const float half = (joint.wide ? kWide : kThin) * kUnit * 0.5f;
    for (int s = 0; s + 1 < count; ++s) {
        const float* a = points[s];
        const float* b = points[s + 1];
        const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
        if (length < 1e-4f) continue;
        float across[3] = {0.0f, 0.0f, 1.0f};
        if (flat > 1e-4f) {
            across[0] = -d[2] / flat;
            across[2] = d[0] / flat;
        }
        const float n[3] = {d[0] / length, d[1] / length, d[2] / length};
        const float up[3] = {n[1] * across[2] - n[2] * across[1], n[2] * across[0] - n[0] * across[2],
                             n[0] * across[1] - n[1] * across[0]};
        const float uHead = 1.0f - float(s) / float(count - 1);
        const float uTail = 1.0f - float(s + 1) / float(count - 1);
        const float* sides[2] = {across, up};
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
            for (int k = 0; k < 3; ++k) sprite.colour[k] = kTrail[k] * light;
            sprite.sheet = spirit_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
}

void Pollution::gatherLagul(gfx::Effects& effects, const Joint& joint) const {
    // Its alpha as Move_MODEL_SUMMONER_SUMMON_LAGUL lays it.
    const float alpha = joint.left > 144.0f ? (kLife - joint.left) / 20.0f
                        : joint.left < 30.0f ? joint.left / 40.0f
                                             : 1.0f;
    if (alpha <= 0.0f) return;
    // The pose: the key over two, blended between its neighbours.
    const float at = joint.key / 2.0f;
    const int p0 = int(at) % kPoses, p1 = (p0 + 1) % kPoses;
    const float t = at - std::floor(at);
    for (int g = 0; g < kGroups; ++g) {
        const std::vector<EffectCorner>& a = lagul_[p0][g];
        const std::vector<EffectCorner>& b = lagul_[p1][g];
        std::vector<EffectCorner>& out = blend_[g];
        for (size_t i = 0; i < out.size() && i < a.size() && i < b.size(); ++i) {
            out[i] = a[i];
            out[i].x = a[i].x + (b[i].x - a[i].x) * t;
            out[i].y = a[i].y + (b[i].y - a[i].y) * t;
            out[i].z = a[i].z + (b[i].z - a[i].z) * t;
        }
    }
    // Facing where it flies: the model's -Z (MU's -Y) along its heading.
    const float cp = std::cos(joint.pitch * kDegrees);
    const float f[3] = {cp * std::cos(joint.yaw * kDegrees), std::sin(joint.pitch * kDegrees),
                        cp * std::sin(joint.yaw * kDegrees)};
    const float z[3] = {-f[0], -f[1], -f[2]};
    float x[3] = {z[2], 0.0f, -z[0]};  // up x z
    const float xl = std::sqrt(x[0] * x[0] + x[2] * x[2]);
    if (xl < 1e-4f) return;
    x[0] /= xl;
    x[2] /= xl;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2],
                        z[0] * x[1] - z[1] * x[0]};
    const float white[3] = {1.0f, 1.0f, 1.0f};
    const gfx::Blend blends[kGroups] = {gfx::Blend::Alpha, gfx::Blend::Alpha, gfx::Blend::Additive};
    for (int g = 0; g < kGroups; ++g) {
        float colour[3];
        for (int k = 0; k < 3; ++k) colour[k] = blends[g] == gfx::Blend::Additive ? white[k] * alpha : white[k];
        submitEffectAlong(effects, blend_[g], lagulSheet_[g], blends[g], joint.at, x, y, z,
                          kLagulScale, colour, blends[g] == gfx::Blend::Additive ? 1.0f : alpha);
    }
    // Its six lit bones.
    if (!bgfx::isValid(shiny_)) return;
    for (const int index : kBoneCorners) {
        if (size_t(index) >= blend_[0].size()) continue;
        const EffectCorner& c = blend_[0][size_t(index)];
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] =
                joint.at[k] + (c.x * x[k] + c.y * y[k] + c.z * z[k]) * kLagulScale;
            sprite.colour[k] = kBoneBlue[k] * alpha;
        }
        sprite.halfWidth = sprite.halfHeight = kShinyPixels * kLagulScale * 0.3f * 0.5f * kUnit;
        sprite.sheet = shiny_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

void Pollution::gather(gfx::Effects& effects) const {
    for (const Circle& one : circles_) {
        if (!one.alive) continue;
        // Subtype 10: darkness under him, laid over itself up to five times.
        const int layers = one.left < 3.0f ? 1 : one.left < 6.0f ? 2 : one.left < 9.0f ? 3
                           : one.left < 12.0f ? 4 : 5;
        const float dark[3] = {one.darkAlpha, one.darkAlpha, one.darkAlpha};
        for (int i = 0; i < layers; ++i) {
            gatherGround(effects, light_, gfx::Blend::Minus, one.at, 12.0f, 0.0f, dark);
        }
        // Subtype 9: the two impact circles, turning against each other.
        if (one.left > 4.0f) {  // its 40 frames, the last of the darkness's 44
            float ring[3];
            const float pollution[3] = {0.6f, 0.6f, 0.9f};
            for (int k = 0; k < 3; ++k) ring[k] = clampOne(pollution[k] * one.ringAlpha);
            gatherGround(effects, impact_, gfx::Blend::Additive, one.at, 2.4f, one.ringTurn[0], ring);
            gatherGround(effects, impact_, gfx::Blend::Additive, one.at, 2.88f, one.ringTurn[1], ring);
        }
    }
    for (const Ring& one : ringsLive_) {
        if (!one.alive || rings_[one.model].empty() || one.glow <= 0.0f) continue;
        const float yaw = one.turn * kDegrees;
        const float c = std::cos(yaw), s = std::sin(yaw);
        const float x[3] = {c, 0.0f, -s}, y[3] = {0.0f, 1.0f, 0.0f}, z[3] = {s, 0.0f, c};
        float lit[3];
        for (int k = 0; k < 3; ++k) lit[k] = clampOne(kRings[one.model].light[k] * one.glow);
        submitEffectAlong(effects, rings_[one.model], ringSheet_[one.model], gfx::Blend::Additive,
                          one.at, x, y, z, one.scale, lit, 1.0f);
    }
    for (const Joint& joint : joints_) {
        if (!joint.alive) continue;
        gatherTail(effects, joint);
        if (joint.wide) gatherLagul(effects, joint);
    }
    for (const Puff& one : puffs_) {
        if (!one.alive) continue;
        const bgfx::TextureHandle sheet = one.cloud ? cloud_ : smoke_;
        if (!bgfx::isValid(sheet)) continue;
        const float open = std::min(1.0f, one.age / (one.life * 0.2f));
        const float out = std::clamp((one.life - one.age) / (one.life * 0.6f), 0.0f, 1.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * 0.5f;
        sprite.spin = one.spin;
        if (one.cloud) {
            for (int k = 0; k < 3; ++k) sprite.colour[k] = one.tint[k] * open * out * 0.6f;
            sprite.blend = gfx::Blend::Additive;
        } else {
            for (int k = 0; k < 3; ++k) sprite.colour[k] = one.tint[k] * 0.55f;
            sprite.colour[3] = 0.45f * open * out;
            sprite.blend = gfx::Blend::Smoke;
        }
        sprite.sheet = sheet;
        effects.add(sprite);
    }
}

}  // namespace mu::game
