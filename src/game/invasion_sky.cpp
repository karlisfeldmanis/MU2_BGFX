#include "game/invasion_sky.h"

#include <algorithm>
#include <cmath>

namespace mu::game {
namespace {

constexpr float kReferenceFps = 25.0f;
// MU's flight, in metres: 300 units over the ground and a bob of up to 100 more
// (`Position[2] += -|sin(Timer)| * 100 + 100`), the timer stepping `Scale * 0.05` a frame, the
// pace `Scale * 40` units a frame (GOBoid.cpp:1398-1403).
constexpr float kCruiseHeight = 3.0f;
constexpr float kBob = 1.0f;
// Its size: MU's `rand() % 3 + 6` tenths was a dragon in the frame; seen only as a shadow it is
// drawn larger, 1.0 to 1.4, so the shape crossing the ground is a dragon's (ours).
constexpr float kSmallest = 1.0f;
constexpr float kLargest = 1.4f;
// Ours (see the header): where a crossing starts behind him along its line, how far either side
// of him it runs, and how far past him it goes before it is retired.
constexpr float kStartBack = 24.0f;
constexpr float kStartSpread = 10.0f;
constexpr float kAcrossLeft = -10.0f;
constexpr float kAcrossRight = 14.0f;
constexpr float kRetire = 40.0f;
// Ours: a slot refilled 0.6 to 1.4 s after the last launch.
constexpr float kLaunchLow = 0.6f, kLaunchHigh = 1.4f;
// The heat trails, ours: a puff of flare01 a reference frame off each wingtip and one between,
// born 0.35 m across and swelling to 1.4 m as it fades over 0.6 to 0.8 s, ember orange at half
// of full -- added, so where they overlap they glow and where they thin they are gone.
const char* const kTrailBoneNames[2] = {"Bip01 L Finger02", "Bip01 R Finger02"};
constexpr int kTrailFill = 1;
constexpr float kHazeBorn = 0.35f, kHazeGrown = 1.4f;
constexpr float kHazeLow = 0.6f, kHazeHigh = 0.8f;
constexpr float kHazeColour[3] = {1.0f, 0.42f, 0.12f};
constexpr float kHazeLevel = 0.5f;
constexpr size_t kMostHazes = 1200;
// The dive (ours): four seconds from 26 m out and 8 m up, the last half-second fading the flight
// into the roar it lands on. The landed dragon's own scale, its recipe's 0.9.
constexpr float kDiveSeconds = 4.0f;
constexpr float kDiveReach = 26.0f;
constexpr float kDiveHeight = 8.0f;
constexpr float kRoarFade = 0.5f;
constexpr float kLandedScale = 0.9f;
// The breath: bone 11 (attack01), 50 units out along its -y (GOBoid.cpp:1549), a spark a frame.
constexpr float kMouth[3] = {0.0f, -0.5f, 0.0f};
// BITMAP_LIGHTNING + 1 at Scale 1 over its 128-texel sheet, red.
constexpr float kGlowHalf = 0.64f;
// SOUND_MONSTER_BULLATTACK1 one frame in 128 a dragon, for MU's five: unplaced, so ten would be
// twice as loud a sky.
constexpr float kCallChance = 1.0f / 128.0f * 5.0f / float(InvasionSky::kDragons);

int mouthBone(const FigureBody* body) {
    if (body == nullptr || body->skeletonMesh == nullptr) return -1;
    const auto& bones = body->skeletonMesh->bones();
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == "attack01") return int(i);
    }
    return 11;
}

}  // namespace

float InvasionSky::random01() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ & 0xFFFFFFu) / float(0x1000000u);
}

void InvasionSky::open(const FigureBody* dragon, bgfx::TextureHandle glow,
                       bgfx::TextureHandle haze) {
    body_ = dragon;
    glow_ = glow;
    haze_ = haze;
    hazes_.clear();
    hazes_.reserve(kMostHazes);
    flightClip_ = roarClip_ = -1;
    if (body_ && body_->library) {
        flightClip_ = body_->library->find(7);  // MONSTER01_DIE + 1
        roarClip_ = body_->library->find(1);    // MONSTER01_STOP2
    }
    if (flightClip_ < 0) body_ = nullptr;
    mouth_ = mouthBone(body_);
    for (int r = 0; r < kTrails; ++r) {
        trailBones_[r] = -1;
        if (body_ == nullptr || body_->skeletonMesh == nullptr) continue;
        const auto& bones = body_->skeletonMesh->bones();
        for (size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].name == kTrailBoneNames[r]) trailBones_[r] = int(i);
        }
    }
    on_ = false;
    for (Wyrm& one : wyrms_) one.live = false;
    diver_.live = false;
}

void InvasionSky::begin(float x, float z, float yaw, float rainSeconds, float landSeconds) {
    if (!body_) return;
    on_ = true;
    clock_ = 0.0f;
    rainSeconds_ = rainSeconds;
    landSeconds_ = landSeconds;
    landAt_[0] = x;
    landAt_[1] = z;
    landYaw_ = yaw;
    nextLaunch_ = rainSeconds;
    diver_.live = false;
}

void InvasionSky::end() {
    on_ = false;
    diver_.live = false;
}

bool InvasionSky::flying() const {
    if (diver_.live) return true;
    for (const Wyrm& one : wyrms_) {
        if (one.live) return true;
    }
    return false;
}

void InvasionSky::launch(Wyrm& one, const float hero[3], const content::Ground& ground) {
    one.live = true;
    one.diving = false;
    one.scale = kSmallest + random01() * (kLargest - kSmallest);
    one.timer = random01();
    one.yaw = 0.0f;  // along +z, MU's -y
    one.at[0] = hero[0] + kAcrossLeft + random01() * (kAcrossRight - kAcrossLeft);
    one.at[2] = hero[2] - kStartBack - random01() * kStartSpread;
    one.at[1] = ground.heightAt(one.at[0], one.at[2]) + kCruiseHeight + kBob;
    one.sparkDue = 0.0f;
    one.trailDue = 0.0f;
    one.trailFed = false;
    one.figure.stand(body_, one.at, one.yaw, one.scale);
    one.figure.play(flightClip_, true, 0.0f);
    one.figure.setClock(random01() * std::max(one.figure.length(), 0.001f));
}

void InvasionSky::breathe(Wyrm& one, float seconds, Asks& asks) {
    one.sparkDue -= seconds;
    while (one.sparkDue <= 0.0f) {
        one.sparkDue += 1.0f / kReferenceFps;
        float at[3];
        if (mouth_ < 0 || !one.figure.pointOn(mouth_, kMouth, at)) return;
        asks.sparks.insert(asks.sparks.end(),
                           {at[0], at[1], at[2], std::sin(one.yaw), std::cos(one.yaw)});
    }
}

void InvasionSky::update(float seconds, const float hero[3], const content::Ground& ground,
                         Asks& asks) {
    if (!body_) return;
    clock_ += seconds;
    const float diveFrom = landSeconds_ - kDiveSeconds;
    // The crossings, while it is on and before the dive.
    if (on_ && clock_ >= nextLaunch_ && clock_ < diveFrom - 1.0f) {
        // Three at once as the rain is in, then one a slot as it comes free.
        int launched = 0;
        for (Wyrm& one : wyrms_) {
            if (one.live) continue;
            launch(one, hero, ground);
            if (++launched >= (clock_ < rainSeconds_ + 0.5f ? 3 : 1)) break;
        }
        nextLaunch_ = clock_ + kLaunchLow + random01() * (kLaunchHigh - kLaunchLow);
    }
    // The dive.
    if (on_ && !diver_.live && clock_ >= diveFrom && clock_ < landSeconds_) {
        diver_.live = true;
        diver_.diving = true;
        diver_.scale = kLandedScale;
        diver_.yaw = landYaw_;
        diver_.sparkDue = 0.0f;
        diver_.trailDue = 0.0f;
        diver_.trailFed = false;
        diver_.at[0] = landAt_[0] - std::sin(landYaw_) * kDiveReach;
        diver_.at[2] = landAt_[1] - std::cos(landYaw_) * kDiveReach;
        diver_.at[1] = ground.heightAt(diver_.at[0], diver_.at[2]) + kDiveHeight;
        diver_.figure.stand(body_, diver_.at, diver_.yaw, diver_.scale);
        diver_.figure.play(flightClip_, true, 0.0f);
    }
    if (diver_.live) {
        const float t = std::clamp((clock_ - diveFrom) / kDiveSeconds, 0.0f, 1.0f);
        const float left = 1.0f - t;
        // Slowing as it comes: the ground covered eases out, the height comes down on the square
        // of what is left, so it levels out over the tile rather than striking it.
        const float along = 1.0f - left * left;
        diver_.at[0] = landAt_[0] - std::sin(landYaw_) * kDiveReach * (1.0f - along);
        diver_.at[2] = landAt_[1] - std::cos(landYaw_) * kDiveReach * (1.0f - along);
        diver_.at[1] = ground.heightAt(diver_.at[0], diver_.at[2]) + kDiveHeight * left * left;
        diver_.figure.place(diver_.at, diver_.yaw, false);
        if (roarClip_ >= 0 && clock_ >= landSeconds_ - kRoarFade &&
            diver_.figure.clip() != roarClip_) {
            diver_.figure.play(roarClip_, true, kRoarFade, true);
        }
        diver_.figure.update(seconds);
        if (diver_.figure.clip() != roarClip_) breathe(diver_, seconds, asks);
        trail(diver_, seconds);
        if (clock_ >= landSeconds_) {
            // Handed to the realm's dragon, which rises on this tick (Play's What::Rose).
            diver_.live = false;
            asks.landed = true;
        }
    }
    for (Wyrm& one : wyrms_) {
        if (!one.live) continue;
        const float speed = one.scale * 40.0f / 100.0f * kReferenceFps;
        one.at[0] += std::sin(one.yaw) * speed * seconds;
        one.at[2] += std::cos(one.yaw) * speed * seconds;
        one.timer += one.scale * 0.05f * kReferenceFps * seconds;
        one.at[1] = ground.heightAt(one.at[0], one.at[2]) + kCruiseHeight -
                    std::fabs(std::sin(one.timer)) * kBob + kBob;
        one.figure.place(one.at, one.yaw, false);
        one.figure.update(seconds);
        // Unseen: no breath and no heat to give it away (InvasionSky::gather).
        const float dx = one.at[0] - hero[0], dz = one.at[2] - hero[2];
        if (dz > 0.0f && dx * dx + dz * dz > kRetire * kRetire) one.live = false;
    }
    // The heat, swelling and fading where it was left.
    for (Haze& haze : hazes_) haze.age += seconds;
    hazes_.erase(std::remove_if(hazes_.begin(), hazes_.end(),
                                [](const Haze& haze) { return haze.age >= haze.life; }),
                 hazes_.end());
    // The cries, one frame in 128 a dragon in the air.
    callDue_ -= seconds;
    if (callDue_ <= 0.0f) {
        callDue_ += 1.0f / kReferenceFps;
        const auto cry = [&](const Wyrm& one) {
            if (one.live && random01() < kCallChance) ++asks.calls;
        };
        for (const Wyrm& one : wyrms_) cry(one);
        cry(diver_);
    }
}

void InvasionSky::trail(Wyrm& one, float seconds) {
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    one.trailDue -= seconds;
    while (one.trailDue <= 0.0f) {
        one.trailDue += 1.0f / kReferenceFps;
        for (int r = 0; r < kTrails; ++r) {
            float at[3];
            if (trailBones_[r] < 0 || !one.figure.pointOn(trailBones_[r], origin, at)) continue;
            const float* was = one.trailWas[r];
            const int steps = one.trailFed ? kTrailFill + 1 : 1;
            for (int s = steps - 1; s >= 0 && hazes_.size() < kMostHazes; --s) {
                const float f = float(s) / float(steps);
                Haze haze;
                for (int k = 0; k < 3; ++k) haze.at[k] = at[k] + (was[k] - at[k]) * f;
                haze.age = 0.0f;
                haze.life = kHazeLow + random01() * (kHazeHigh - kHazeLow);
                haze.spin = random01() * 6.2831853f;
                haze.scale = one.scale / 0.7f;
                hazes_.push_back(haze);
            }
            for (int k = 0; k < 3; ++k) one.trailWas[r][k] = at[k];
        }
        one.trailFed = true;
    }
}

void InvasionSky::gather(gfx::Renderer& renderer, float* scratch,
                         std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters) {
    if (!body_) return;
    const auto draw = [&](Wyrm& one) {
        if (!one.live) return;
        const int bones = one.figure.pose(scratch);
        one.palette = bones > 0 ? renderer.addPalette(scratch, bones) : -1;
        if (one.palette < 0) return;
        if (casters) one.figure.gather(one.palette, *casters);
        one.figure.gather(one.palette, out);
    };
    // The crossings into the sun's list alone: he never sees them, only their shadows sweeping
    // over the ground (the user, 2026-10-06: 'character never sees dragons but sees just
    // shadows'). The diver is drawn, the one he sees come down.
    for (Wyrm& one : wyrms_) {
        if (!one.live) continue;
        const int bones = one.figure.pose(scratch);
        one.palette = bones > 0 ? renderer.addPalette(scratch, bones) : -1;
        if (one.palette >= 0 && casters) one.figure.gather(one.palette, *casters);
    }
    draw(diver_);
}

void InvasionSky::glow(gfx::Effects& effects) const {
    if (!body_) return;
    if (bgfx::isValid(haze_)) {
        for (const Haze& haze : hazes_) {
            const float t = haze.age / haze.life;
            // In over the first tenth, so a puff is never born at full; out on a smooth fall.
            const float fade = t < 0.1f ? t / 0.1f : (1.0f - t) * (1.0f - t) / 0.81f;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = haze.at[k];
            sprite.halfWidth = sprite.halfHeight =
                0.5f * haze.scale * (kHazeBorn + (kHazeGrown - kHazeBorn) * t);
            sprite.spin = haze.spin;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = kHazeColour[k] * kHazeLevel * fade;
            sprite.sheet = haze_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) break;
        }
    }
    if (!bgfx::isValid(glow_) || mouth_ < 0) return;
    const auto shine = [&](const Wyrm& one) {
        if (!one.live || one.palette < 0) return;
        gfx::Sprite sprite;
        if (!one.figure.pointOn(mouth_, kMouth, sprite.position)) return;
        sprite.halfWidth = sprite.halfHeight = kGlowHalf;
        sprite.colour[0] = 1.0f;
        sprite.colour[1] = sprite.colour[2] = 0.0f;
        sprite.sheet = glow_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };
    shine(diver_);
}

}  // namespace mu::game
