#include "game/pets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "content/placement.h"
#include "core/maths.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
// MU's reference frame: the client steps a mount 25 times a second (FPS_ANIMATION_FACTOR).
constexpr float kReference = 1.0f / 25.0f;
// MU units to a metre.
constexpr float kUnits = 100.0f;
// GOBoid.cpp:629: how near the Angel keeps to the owner, on the ground's two axes.
constexpr float kFlyRange = 150.0f;
// ZzzCharacter.cpp:15446: where the Imp sits in the clavicle's frame, in metres.
constexpr float kImpOffset[3] = {20.0f / kUnits, 0.0f, 0.0f};
// CreateMount's own `o->Scale = 0.7f` (GOBoid.cpp:88). RenderMount then draws a player's mount at
// 1.0 (:686-691), in the branch that also names Season 6's Fenrir; at 1.0 the Angel read too
// big beside the character (the user, 2026-09-29), so it keeps the scale it was made at.
constexpr float kAngelScale = 0.7f;
// Ours: how long the Angel takes to come in. CreateMount starts it at Alpha 0 with AlphaTarget 1.
constexpr float kAngelFadeIn = 0.5f;
// Ours: an owner this far off -- a warp, a gate -- has the Angel put down beside him again
// rather than flying across the map to catch up.
constexpr float kAngelLost = 1500.0f;
// Ours: how fast the drawn heading follows the flown one, a share a second. MoveMount's heading
// snaps -- to a new random one about every 32 frames, and 20 degrees a frame when it turns back --
// which read at 25 fps as a flutter and at 180 as a twitch. The flight itself is untouched.
constexpr float kHeadingEase = 10.0f;
// How long the horse takes to come and go at a safe zone's edge. MU sets its Alpha to 0 on the
// tile and back (GOBoid.cpp:498-502), a cut; **ours**, a short fade, as a pop on screen is
// closed (docs/mount.md).
constexpr float kHorseFade = 0.25f;

// This engine's world against MU's: x east alike, MU's y north is -z, MU's z up is y.
void toMu(const float world[3], float mu[3]) {
    mu[0] = world[0] * kUnits;
    mu[1] = -world[2] * kUnits;
    mu[2] = world[1] * kUnits;
}
void fromMu(const float mu[3], float world[3]) {
    world[0] = mu[0] / kUnits;
    world[1] = mu[2] / kUnits;
    world[2] = -mu[1] / kUnits;
}

// MU's TurnAngle2: `angle` toward `target` by at most `most` degrees, the short way round.
float turn(float angle, float target, float most) {
    float gap = std::fmod(target - angle + 540.0f, 360.0f) - 180.0f;
    gap = std::clamp(gap, -most, most);
    return std::fmod(angle + gap + 360.0f, 360.0f);
}

}  // namespace

void Pets::open(const Figures& figures) {
    angelBody_ = figures.body("Helper01");
    impBody_ = figures.body("Helper02");
    horseBody_ = figures.body("Rider01");
}

float Pets::roll() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ >> 8) / float(1u << 24);
}

void Pets::spawnAngel(const float owner[3]) {
    // CreateMount's MODEL_HELPER case: anywhere within 256 of him and 128-256 above.
    at_[0] = owner[0] + float(dice(512) - 256);
    at_[1] = owner[1] + float(dice(512) - 256);
    at_[2] = owner[2] + float(dice(128) + 128);
    direction_[0] = direction_[1] = direction_[2] = 0.0f;
    heading_ = float(dice(360));
    drawnHeading_ = heading_;
    std::memcpy(was_, at_, sizeof(was_));
    frames_ = 0.0f;
    angelIn_ = 0.0f;
    float world[3];
    fromMu(at_, world);
    angel_.stand(angelBody_, world, heading_ * kPi / 180.0f, kAngelScale);
    if (angelBody_ && angelBody_->idleClip >= 0) angel_.play(angelBody_->idleClip, true, 0.0f);
    angelUp_ = true;
}

void Pets::stepAngel(const float owner[3]) {
    // MoveMount, one reference frame (GOBoid.cpp:629-659).
    const float range[2] = {owner[0] - at_[0], owner[1] - at_[1]};
    const float distance = range[0] * range[0] + range[1] * range[1];
    const bool far = distance >= kFlyRange * kFlyRange;
    if (far) {
        // CreateAngle toward him, read in the frame this heading moves in: forward is local -Y
        // turned by the heading, so a heading h goes along (sin h, -cos h).
        const float toward = std::atan2(range[0], -range[1]) * 180.0f / kPi;
        heading_ = turn(heading_, toward, 20.0f);
    }
    const float h = heading_ * kPi / 180.0f;
    // VectorRotate of Direction by the heading about z.
    const float moved[3] = {direction_[0] * std::cos(h) - direction_[1] * std::sin(h),
                            direction_[0] * std::sin(h) + direction_[1] * std::cos(h),
                            direction_[2]};
    for (int i = 0; i < 3; ++i) at_[i] += moved[i];
    at_[2] += float(dice(16) - 8);
    if (dice(32) == 0) {
        float speed = 0.0f;
        if (far) {
            speed = -float(dice(64) + 128) * 0.1f;
        } else {
            speed = -float(dice(64) + 16) * 0.1f;
            heading_ = float(dice(360));
        }
        direction_[0] = 0.0f;
        direction_[1] = speed;
        direction_[2] = float(dice(64) - 32) * 0.1f;
    }
    if (at_[2] < owner[2] + 100.0f) direction_[2] += 1.5f;
    if (at_[2] > owner[2] + 200.0f) direction_[2] -= 1.5f;
}

void Pets::ride(float seconds, const Figure& hero, bool riding, int action) {
    // The Horn of Uniria's horse: GOBoid.cpp's MODEL_UNICON, on the rider's own spot and facing
    // (:515, :524) at scale 1.0 (:684-691). No bone joins them; the seat is in his ride clips.
    if (shown_ != 2 || !horseBody_) {
        horseIn_ = 0.0f;
        horseUp_ = false;
        return;
    }
    horseIn_ = std::clamp(horseIn_ + (riding ? seconds : -seconds) / kHorseFade, 0.0f, 1.0f);
    if (!horseUp_) {
        horse_.stand(horseBody_, hero.position(), hero.yaw(), hero.scale());
        horseUp_ = true;
    }
    // Where it stands while it fades out at the zone's edge is where he stepped off it: the
    // rider walks on, the horse does not follow him in.
    if (riding) horse_.place(hero.position(), hero.yaw(), false);
    // Its clip off the rider's (GOBoid.cpp:525-595): 2 while he rides on, 3 while he swings,
    // 0 otherwise -- held where it is through anything else, as SetAction refuses the 6 this
    // four-action model lacks (ZzzAI.cpp:424).
    const int clip = horseBody_->library ? horseBody_->library->find(action) : -1;
    if (clip >= 0) horse_.play(clip, false, -1.0f);
    horse_.update(seconds);
    // And in step with him. MU runs the two on two clocks at one rate, and its run ride and the
    // horse's leap are both 7 keys at 0.34, so they hold together there; drawn here they part
    // on a crossfade, a hitch or a resumed walk phase, and the rider bounces off the saddle's
    // beat. So the leap and the stand take his clip's own place, as a fraction: one bound of
    // the horse is one bounce of the rider, whatever the frame did. **ours**.
    if (action != 3) horse_.setClock(hero.through() * horse_.length());
}

void Pets::update(float seconds, const Figure& hero, int pet, bool alive) {
    if (pet != shown_) {
        shown_ = pet;
        angelUp_ = false;
        if (pet == 1 && impBody_) {
            imp_.stand(impBody_, hero.position(), 0.0f, hero.scale());
            if (impBody_->idleClip >= 0) imp_.play(impBody_->idleClip, true, 0.0f);
        }
    }
    if (hero.body() != heroBody_) {
        heroBody_ = hero.body();
        clavicle_ = -1;
        if (heroBody_ && heroBody_->skeletonMesh) {
            const auto& bones = heroBody_->skeletonMesh->bones();
            for (size_t i = 0; i < bones.size(); ++i) {
                if (bones[i].name == "Bip01 L Clavicle") clavicle_ = int(i);
            }
        }
    }
    if (shown_ == 1) imp_.update(seconds);
    if (shown_ != 0 || !angelBody_) return;

    // The Angel: gone while he is down, and back beside him when he stands again.
    if (!alive) {
        angelUp_ = false;
        return;
    }
    float owner[3];
    toMu(hero.position(), owner);
    if (!angelUp_ || std::fabs(owner[0] - at_[0]) > kAngelLost ||
        std::fabs(owner[1] - at_[1]) > kAngelLost) {
        spawnAngel(owner);
    }
    frames_ += seconds / kReference;
    // Bounded, so a long hitch is not a burst of steps the eye sees as a jump.
    frames_ = std::min(frames_, 8.0f);
    while (frames_ >= 1.0f) {
        std::memcpy(was_, at_, sizeof(was_));
        stepAngel(owner);
        frames_ -= 1.0f;
    }
    angelIn_ += seconds;
    // Between the last two steps, by how far into the next one this frame is.
    float between[3];
    for (int i = 0; i < 3; ++i) between[i] = was_[i] + (at_[i] - was_[i]) * frames_;
    const float gap = std::fmod(heading_ - drawnHeading_ + 540.0f, 360.0f) - 180.0f;
    drawnHeading_ += gap * std::min(1.0f, seconds * kHeadingEase);
    float world[3];
    fromMu(between, world);
    angel_.place(world, drawnHeading_ * kPi / 180.0f, false);
    angel_.update(seconds);
}

void Pets::gather(gfx::Renderer& renderer, const Figure& hero, std::vector<float>& scratch,
                  std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters) {
    Figure* figure = nullptr;
    float fade = 1.0f;
    if (shown_ == 1 && impBody_) {
        float bone[16];
        if (clavicle_ < 0 || !hero.boneWorld(clavicle_, bone)) return;
        // In the clavicle's own frame, moved along it by MU's offset.
        float local[16];
        content::placementTransform(0.0f, 0.0f, 0.0f, 1.0f, kImpOffset, local);
        float parent[16];
        core::mulMatrix(local, bone, parent);
        imp_.mount(parent);
        figure = &imp_;
    } else if (shown_ == 0 && angelUp_) {
        figure = &angel_;
        fade = std::clamp(angelIn_ / kAngelFadeIn, 0.0f, 1.0f);
    } else if (shown_ == 2 && horseUp_ && horseIn_ > 0.0f) {
        figure = &horse_;
        fade = horseIn_;
    }
    if (!figure) return;
    const int bones = figure->pose(scratch.data());
    const int palette = bones > 0 ? renderer.addPalette(scratch.data(), bones) : -1;
    const size_t from = out.size();
    if (casters) figure->gather(palette, *casters);
    figure->gather(palette, out);
    for (size_t i = from; i < out.size(); ++i) out[i].fade = fade;
}

}  // namespace mu::game
