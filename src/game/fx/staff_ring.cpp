#include "game/fx/staff_ring.h"

#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kUnit = 0.01f;
constexpr float kFps = 25.0f;
constexpr float kDegrees = 3.14159265f / 180.0f;
constexpr float kBorn = 280.0f;   // units over the boss
constexpr float kPitch = 20.0f;   // degrees, nose down
constexpr float kAhead = 80.0f;   // Direction (0, -80, -10): forward
constexpr float kDown = 10.0f;    // and down, both turned by the pitch
constexpr float kFrames = 30.0f;  // LifeTime

}  // namespace

bool StaffRing::open(const std::string& assetDir, content::Textures& textures,
                     const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/tarkan/";
    mesh_.clear();
    loadEffectObj(dir + "Staff09.obj", kUnit, "wand10", mesh_);
    if (core::fileExists(dir + "wand10.png")) {
        sheet_ = textures.load(dir + "wand10.png", content::TextureRole::Albedo);
    }
    core::logf("staff ring: Staff09 %zu triangles, sheet %s", mesh_.size() / 3,
               bgfx::isValid(sheet_) ? "yes" : "NO");
    return !mesh_.empty() && bgfx::isValid(sheet_);
}

void StaffRing::shutdown() {
    mesh_.clear();
    for (Staff& one : staffs_) one.alive = false;
}

void StaffRing::cast(const float feet[3], float yaw) {
    if (mesh_.empty()) return;
    const float pitch = kPitch * kDegrees;
    // (0, -80, -10) pitched down 20 degrees: MU's forward and down, in metres a frame.
    const float flat = (kAhead * std::cos(pitch) - kDown * std::sin(pitch)) * kUnit;
    const float up = -(kAhead * std::sin(pitch) + kDown * std::cos(pitch)) * kUnit;
    int made = 0;
    for (Staff& one : staffs_) {
        if (made == kStaffs) break;
        if (one.alive) continue;
        const float turn = yaw + float(made) * 20.0f * kDegrees;
        one = Staff{};
        one.alive = true;
        one.at[0] = feet[0];
        one.at[1] = feet[1] + kBorn * kUnit;
        one.at[2] = feet[2];
        // A body faces (sin yaw, cos yaw) on the ground plane.
        one.travel[0] = std::sin(turn) * std::cos(pitch);
        one.travel[1] = -std::sin(pitch);
        one.travel[2] = std::cos(turn) * std::cos(pitch);
        one.speed[0] = flat;
        one.speed[1] = up;
        one.left = kFrames;
        ++made;
    }
}

void StaffRing::update(float seconds, std::vector<Landing>& landings) {
    const float frames = seconds * kFps;
    for (Staff& one : staffs_) {
        if (!one.alive) continue;
        const float across = std::sqrt(one.travel[0] * one.travel[0] +
                                       one.travel[2] * one.travel[2]);
        if (across > 1e-5f) {
            one.at[0] += one.travel[0] / across * one.speed[0] * frames;
            one.at[2] += one.travel[2] / across * one.speed[0] * frames;
        }
        one.at[1] += one.speed[1] * frames;
        one.left -= frames;
        const float floor = ground_ ? ground_->heightAt(one.at[0], one.at[2]) : 0.0f;
        if (one.at[1] < floor) {
            landings.push_back({one.at[0], floor, one.at[2]});
            one.alive = false;
        } else if (one.left <= 0.0f) {
            one.alive = false;
        }
    }
}

void StaffRing::gather(gfx::Effects& effects) const {
    if (mesh_.empty() || !bgfx::isValid(sheet_)) return;
    constexpr float kWhite[3] = {1.0f, 1.0f, 1.0f};
    for (const Staff& one : staffs_) {
        if (!one.alive) continue;
        // The staff lies along its model's Z: laid along the way it flies, its Y kept up.
        const float* z = one.travel;
        float x[3] = {z[2], 0.0f, -z[0]};
        const float n = std::sqrt(x[0] * x[0] + x[2] * x[2]);
        if (n < 1e-5f) continue;
        x[0] /= n;
        x[2] /= n;
        const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2],
                            z[0] * x[1] - z[1] * x[0]};
        submitEffectAlong(effects, mesh_, sheet_, gfx::Blend::Additive, one.at, x, y, z, 1.0f,
                          kWhite, 1.0f);
    }
}

}  // namespace mu::game
