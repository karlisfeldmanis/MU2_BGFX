#include "game/world/skull_ghosts.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"
#include "game/world/world_show.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;        // MU's reference frame, 25 a second
constexpr float kUnit = 0.01f;         // a MU unit, in metres
constexpr float kSheetMetres = 0.64f;  // flare01 is 64 texels: Scale 1 is 0.64 m
constexpr float kReach = 28.0f;        // metres from the camera's point a skull raises ghosts
constexpr size_t kMostGhosts = 16;     // five skulls at about one in seven seconds each
constexpr size_t kMostMotes = 16 * 48;
constexpr float kLight[3] = {0.1f, 0.1f, 0.25f};  // BlendMeshLight / 10, / 10, / 4
constexpr float kDegree = 3.14159265f / 180.0f;
// Ours, the head: Nova's soft round puff, this wide and at this many times the body light.
constexpr float kHaloMetres = 1.1f;
constexpr float kHaloLight = 1.5f;

// MU's AngleMatrix (ZzzMathLib.cpp:194-221): degrees, roll about x, pitch about y, yaw about z.
void angleMatrix(const float angle[3], float m[3][3]) {
    const float sy = std::sin(angle[2] * kDegree), cy = std::cos(angle[2] * kDegree);
    const float sp = std::sin(angle[1] * kDegree), cp = std::cos(angle[1] * kDegree);
    const float sr = std::sin(angle[0] * kDegree), cr = std::cos(angle[0] * kDegree);
    m[0][0] = cp * cy;
    m[1][0] = cp * sy;
    m[2][0] = -sp;
    m[0][1] = sr * sp * cy - cr * sy;
    m[1][1] = sr * sp * sy + cr * cy;
    m[2][1] = sr * cp;
    m[0][2] = cr * sp * cy + sr * sy;
    m[1][2] = cr * sp * sy - sr * cy;
    m[2][2] = cr * cp;
}

}  // namespace

float SkullGhosts::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void SkullGhosts::open(const std::string& assetDir, const std::string& world,
                       const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (!worldShow(world).skullGhosts) return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("light"))
            moteSheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        if (const content::EffectSheet* sheet = table.effect("nova_puff"))
            haloSheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model != content::TownEmitter::kWorld || one.kind != content::EmitterKind::SkullGhost)
            continue;
        Box box;
        for (int i = 0; i < 3; ++i) box.at[i] = one.at[i];
        box.scale = one.high;
        boxes_.push_back(box);
    }
    open_ = true;
    ghosts_.reserve(kMostGhosts);
    motes_.reserve(kMostMotes);
    core::logf("skull ghosts: %zu skulls; light %s, halo %s", boxes_.size(),
               bgfx::isValid(moteSheet_) ? "yes" : "NO", bgfx::isValid(haloSheet_) ? "yes" : "NO");
}

void SkullGhosts::shutdown() {
    if (open_ && !boxes_.empty()) core::logf("skull ghosts: %u raised", raised_);
    raised_ = 0;
    open_ = false;
    boxes_.clear();
    ghosts_.clear();
    motes_.clear();
    clock_ = 0.0f;
    moteSheet_ = haloSheet_ = BGFX_INVALID_HANDLE;
}

void SkullGhosts::raise(const Box& box) {
    if (ghosts_.size() >= kMostGhosts) return;
    Ghost one;
    // rand() % 200 - 100 units along each of MU's axes; the same spread along each of ours.
    for (int i = 0; i < 3; ++i) one.at[i] = box.at[i] + float(roll(200) - 100) * kUnit;
    one.angle[2] = float(roll(360));
    one.speed = float(roll(8)) * 0.1f - 5.0f;
    one.scale = box.scale + float(roll(5)) / 40.0f;
    for (int i = 0; i < 3; ++i) one.start[i] = one.was[i] = one.at[i];
    ghosts_.push_back(one);
    ++raised_;
}

void SkullGhosts::step() {
    worldMs_ += uint32_t(kFrame * 1000.0f);
    // The motes: Scale down 0.011 and light down by 1.05 a frame (their LifeTime starts under
    // 75, so they never brighten first), dead at the end of their life or of their Scale.
    for (Mote& one : motes_) {
        one.life -= 1.0f;
        one.scale -= 0.011f;
        for (float& c : one.light) c /= 1.05f;
    }
    motes_.erase(std::remove_if(motes_.begin(), motes_.end(),
                                [](const Mote& m) { return m.life <= 0.0f || m.scale <= 0.0f; }),
                 motes_.end());
    const float swing = std::sin(float(worldMs_ % 10000u) * 0.0001f);
    for (Ghost& one : ghosts_) {
        for (int i = 0; i < 3; ++i) one.was[i] = one.at[i];
        if (roll(40) == 1) {
            one.velocity = -one.velocity;
            one.gravity = one.velocity * float(roll(3) + 5);
        }
        if (roll(40) == 1) {
            one.velocity = -one.velocity;
            one.distance = one.velocity * float(roll(3) + 1);
        }
        const float dx = one.at[0] - one.start[0], dy = one.at[1] - one.start[1],
                    dz = one.at[2] - one.start[2];
        if (!one.cut && std::sqrt(dx * dx + dy * dy + dz * dz) >= 500.0f * kUnit) {
            one.life = 50;
            one.cut = true;
        }
        one.angle[2] += swing * one.gravity;
        one.angle[0] += swing * one.distance / 3.0f;
        if (motes_.size() < kMostMotes) {
            Mote mote;
            for (int i = 0; i < 3; ++i) {
                mote.at[i] = one.at[i];
                mote.light[i] = kLight[i];
            }
            mote.scale = one.scale;
            mote.life = 40.0f * one.scale;
            motes_.push_back(mote);
        }
        // MoveParticle: Direction (0, speed, 0) turned by the Angle, in MU's frame; our
        // (x, y, z) is MU's (x, z, -y).
        float m[3][3];
        angleMatrix(one.angle, m);
        one.at[0] += one.speed * m[0][1] * kUnit;
        one.at[1] += one.speed * m[2][1] * kUnit;
        one.at[2] -= one.speed * m[1][1] * kUnit;
        --one.life;
    }
    ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(),
                                 [](const Ghost& g) { return g.life <= 0; }),
                  ghosts_.end());
    // The raising: in the first 30 ms of every 1024, one frame in five (rand_fps_check(5)).
    if (worldMs_ % 1024u >= 30u) return;
    for (const Box& box : boxes_) {
        if (box.near && roll(5) == 0) raise(box);
    }
}

void SkullGhosts::update(float seconds, const float near[3]) {
    if (!open_ || boxes_.empty()) return;
    for (Box& box : boxes_) {
        const float dx = box.at[0] - near[0], dz = box.at[2] - near[2];
        box.near = dx * dx + dz * dz <= kReach * kReach;
    }
    // A long hitch would run a second of frames at once; MU's own clock drops them too.
    clock_ += std::min(seconds, 0.25f);
    while (clock_ >= kFrame) {
        clock_ -= kFrame;
        step();
    }
}

void SkullGhosts::gather(gfx::Effects& effects) const {
    if (!open_) return;
    if (bgfx::isValid(moteSheet_)) {
        for (const Mote& one : motes_) {
            gfx::Sprite sprite;
            for (int i = 0; i < 3; ++i) {
                sprite.position[i] = one.at[i];
                sprite.colour[i] = one.light[i];
            }
            sprite.colour[3] = 1.0f;
            sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * one.scale;
            sprite.sheet = moteSheet_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    if (!bgfx::isValid(haloSheet_)) return;
    const float blend = clock_ / kFrame;
    for (const Ghost& one : ghosts_) {
        gfx::Sprite halo;
        for (int i = 0; i < 3; ++i) {
            halo.position[i] = one.was[i] + (one.at[i] - one.was[i]) * blend;
            halo.colour[i] = kLight[i] * kHaloLight;
        }
        halo.colour[3] = 1.0f;
        halo.halfWidth = halo.halfHeight = 0.5f * kHaloMetres * one.scale;
        halo.sheet = haloSheet_;
        halo.blend = gfx::Blend::Additive;
        effects.add(halo);
    }
}

}  // namespace mu::game
