// **The Dark Raven, drawn** (docs/plans/dark-lord-pets/client.md section 1): MuMain's CSPetSystem
// (CSPetSystem.cpp:245-792) for MODEL_DARK_SPIRIT, Skill/darkspirit.bmd at 0.7. It flies about
// him 250 over his feet (350 with the Dark Horse), perches on his left hand on a safe tile, dives
// on what it strikes (the dash) or hovers and lets its ball go after a 15-frame charge. Its body is
// one of three by its level (ZzzObject.cpp:981-1023). Stepped at MU's 25 Hz and drawn between its
// last two steps, as the Angel is.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "content/placement.h"
#include "core/maths.h"
#include "game/fx/mu_angles.h"
#include "game/pets.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kReference = 1.0f / 25.0f;
constexpr float kUnits = 100.0f;
constexpr float kRavenScale = 0.7f;      // CreatePetPointer (CSPetSystem.cpp:64-199)
constexpr float kRavenFlyRange = 150.0f;
constexpr float kRavenHigh = 250.0f, kRavenHighOnHorse = 350.0f;
constexpr int kRavenLostTicks = 90;      // then put beside him again (:597-609)
constexpr int kRavenCharge = 15;         // the ball's charge, AttackTime's cap (:584-591)
// Its perch in Bip01 L Hand's own frame, MU's (-10, 0, 10) (:554-562), and the heading off his.
constexpr float kPerch[3] = {-10.0f / kUnits, 0.0f, 10.0f / kUnits};
constexpr float kPerchTurn = -120.0f;
// Its four actions (CSPetSystem.cpp:390-403): the glide, the fast flap, perched, the dive.
constexpr int kGlide = 0, kFlap = 1, kSit = 2, kDive = 3;
// Ours: how long it takes to come in, as the Angel.
constexpr float kRavenFadeIn = 0.5f;

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
float turn(float angle, float target, float most) {
    float gap = std::fmod(target - angle + 540.0f, 360.0f) - 180.0f;
    gap = std::clamp(gap, -most, most);
    return std::fmod(angle + gap + 360.0f, 360.0f);
}
// The heading (degrees, forward local -Y turned by it) and the pitch that look from `from` at `to`.
float yawToward(const float from[3], const float to[3]) {
    return std::atan2(to[0] - from[0], -(to[1] - from[1])) * 180.0f / kPi;
}
float pitchToward(const float from[3], const float to[3]) {
    const float flat = std::hypot(to[0] - from[0], to[1] - from[1]);
    return std::atan2(to[2] - from[2], std::max(flat, 1.0f)) * 180.0f / kPi;
}

}  // namespace

void Pets::raven(float seconds, const Figure& hero, int level, bool safe, bool horse) {
    // Moved and drawn with him whether he stands or lies dead, as MU's MoveCharacter and
    // RenderCharacter call its MovePet and RenderPet for every character (ZzzCharacter.cpp:5339,
    // :11401).
    cueCount_ = 0;
    if (level <= 0 || !ravenBody_) {
        ravenUp_ = false;
        ravenLevel_ = 0;
        return;
    }
    ravenLevel_ = level;
    if (hero.body() != heroBody_ || hand_ < 0) {
        hand_ = perch_ = -1;
        if (hero.body() && hero.body()->skeletonMesh) {
            const auto& bones = hero.body()->skeletonMesh->bones();
            for (size_t i = 0; i < bones.size(); ++i) {
                if (bones[i].name == "Bip01 L Hand") hand_ = int(i);
                if (bones[i].name == "hand_bofdgne01") perch_ = int(i);
            }
        }
    }
    float owner[3];
    toMu(hero.position(), owner);
    float handWorld[3] = {hero.position()[0], hero.position()[1] + 1.2f, hero.position()[2]};
    if (hand_ >= 0) hero.pointOn(hand_, kPerch, handWorld);
    float hand[3];
    toMu(handWorld, hand);
    // It flies in at bone 42, hand_bofdgne01, and sits at Bip01 L Hand (:563-583, :554-562).
    float perch[3];
    std::memcpy(perch, hand, sizeof(perch));
    if (perch_ >= 0) {
        float perchWorld[3];
        const float none[3] = {0.0f, 0.0f, 0.0f};
        hero.pointOn(perch_, none, perchWorld);
        toMu(perchWorld, perch);
    }
    if (!ravenUp_) {
        // Over his tile, 300 up (:300); on his hand when he stands in town.
        std::memcpy(bat_, owner, sizeof(bat_));
        bat_[2] += 300.0f;
        if (safe) std::memcpy(bat_, hand, sizeof(bat_));
        std::memcpy(batWas_, bat_, sizeof(batWas_));
        batDir_[0] = batDir_[1] = batDir_[2] = 0.0f;
        batYaw_ = batYawDrawn_ = hero.yaw() * 180.0f / kPi;
        batPitch_ = 0.0f;
        bird_ = safe ? Bird::Perched : Bird::Fly;
        batTicks_ = batLost_ = 0;
        batFrames_ = 0.0f;
        ravenIn_ = 0.0f;
        float world[3];
        fromMu(bat_, world);
        raven_.stand(ravenBody_, world, batYaw_ * kPi / 180.0f, kRavenScale);
        clipShown_ = -1;
        ravenUp_ = true;
    }
    batFrames_ = std::min(batFrames_ + seconds / kReference, 8.0f);
    while (batFrames_ >= 1.0f) {
        std::memcpy(batWas_, bat_, sizeof(batWas_));
        stepRaven(owner, hand, perch, safe, horse);
        batFrames_ -= 1.0f;
    }
    ravenIn_ += seconds;
    // Its clip off what it does: the glide, the flap above a speed of 12, perched, the dive; the
    // charge, which MU's switch does not name, glides (:390-403).
    const int action = bird_ == Bird::Perched                             ? kSit
                       : bird_ == Bird::Dash || bird_ == Bird::Away       ? kDive
                       : bird_ == Bird::Flying || bird_ == Bird::Perching ? kFlap
                                                                          : kGlide;
    if (action != clipShown_ && ravenBody_->library) {
        const int clip = ravenBody_->library->find(action);
        if (clip >= 0) raven_.play(clip, false, 0.15f);
        clipShown_ = action;
    }
    float between[3];
    if (bird_ == Bird::Perched) {
        std::memcpy(between, hand, sizeof(between));
    } else {
        for (int i = 0; i < 3; ++i) between[i] = batWas_[i] + (bat_[i] - batWas_[i]) * batFrames_;
    }
    const float gap = std::fmod(batYaw_ - batYawDrawn_ + 540.0f, 360.0f) - 180.0f;
    batYawDrawn_ += gap * std::min(1.0f, seconds * 10.0f);
    if (bird_ == Bird::Perched) batYawDrawn_ = hero.yaw() * 180.0f / kPi + kPerchTurn;
    float world[3];
    fromMu(between, world);
    raven_.place(world, batYawDrawn_ * kPi / 180.0f, false);
    raven_.tilt(bird_ == Bird::Perched ? 0.0f : -batPitch_ * kPi / 180.0f, 0.0f);
    raven_.update(seconds);
}

void Pets::stepRaven(const float owner[3], const float hand[3], const float perch[3], bool safe, bool horse) {
    // Into town it goes to his hand and sits; out of it, it flies off (:369-388).
    if (safe && bird_ != Bird::Perching && bird_ != Bird::Perched) {
        bird_ = Bird::Perching;
        batSpeed_ = 3.0f;
    } else if (!safe && (bird_ == Bird::Perching || bird_ == Bird::Perched)) {
        bird_ = Bird::Flying;
    }
    const auto forward = [&](float speed) {
        const float h = batYaw_ * kPi / 180.0f, p = batPitch_ * kPi / 180.0f;
        bat_[0] += speed * std::sin(h) * std::cos(p);
        bat_[1] -= speed * std::cos(h) * std::cos(p);
        bat_[2] += speed * std::sin(p);
    };
    switch (bird_) {
        case Bird::Perching: {
            // On along its heading, then MoveHumming onto his hand: yaw and pitch each turned at
            // most its speed, its speed up one a frame from 3, so it swings in wide (:563-583).
            forward(batSpeed_);
            batYaw_ = turn(batYaw_, yawToward(bat_, perch), batSpeed_);
            batPitch_ += std::clamp(pitchToward(bat_, perch) - batPitch_, -batSpeed_, batSpeed_);
            batSpeed_ += 1.0f;
            const float d[3] = {perch[0] - bat_[0], perch[1] - bat_[1], perch[2] - bat_[2]};
            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 50.0f * 50.0f) bird_ = Bird::Perched;
            return;
        }
        case Bird::Perched:
            std::memcpy(bat_, hand, sizeof(bat_));
            batPitch_ = 0.0f;
            return;
        case Bird::Dash: {
            // Its streaks the first two steps and its flash the first: AttackTime 1 and 2, MovePet
            // raising it before AttackEffect draws (:592-596, :729-745).
            ++dashTime_;
            if (dashTime_ <= 2) cue(RavenCue::Streaks);
            if (dashTime_ == 1) cue(RavenCue::Flash);
            // At the target 50 up, turning by its speed, its speed up by its pull and the pull by
            // 0.2 a frame; off again within 20 or after 20 frames (:506-531).
            const float aim[3] = {batPrey_[0], batPrey_[1], batPrey_[2] + 50.0f};
            batYaw_ = turn(batYaw_, yawToward(bat_, aim), batSpeed_);
            batPitch_ += std::clamp(pitchToward(bat_, aim) - batPitch_, -batSpeed_, batSpeed_);
            forward(batSpeed_);
            batSpeed_ += batPull_;
            batPull_ += 0.2f;
            const float d[3] = {aim[0] - bat_[0], aim[1] - bat_[1], aim[2] - bat_[2]};
            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 20.0f * 20.0f || ++batTicks_ > 20) {
                bird_ = Bird::Away;
                batPitch_ = 45.0f;
            }
            return;
        }
        case Bird::Away: {
            // Up and away, a speed less a frame, until 250 from him (:532-540).
            forward(std::max(batSpeed_, 4.0f));
            batSpeed_ = std::max(4.0f, batSpeed_ - 1.0f);
            if (std::hypot(bat_[0] - owner[0], bat_[1] - owner[1]) > 250.0f || bat_[2] > owner[2] + 300.0f) {
                bird_ = Bird::Flying;
                batPitch_ = 0.0f;
            }
            return;
        }
        case Bird::Charge:
            // Hovering, turning on its target at 40 a frame, and let go after the charge (:543-553).
            batYaw_ = turn(batYaw_, yawToward(bat_, batPrey_), 40.0f);
            // Lit on every other bone while it charges (:759-775).
            cue(RavenCue::Glow);
            if (++batTicks_ >= kRavenCharge) {
                letGo_ = true;
                bird_ = Bird::Flying;
            }
            return;
        case Bird::Fly:
        case Bird::Flying:
            break;
    }
    // Its flight about him (:421-505), a blue spark off one of its bones each step (:425-437).
    cue(RavenCue::Spark);
    const float range[2] = {owner[0] - bat_[0], owner[1] - bat_[1]};
    const bool far = range[0] * range[0] + range[1] * range[1] >= kRavenFlyRange * kRavenFlyRange;
    if (far) batYaw_ = turn(batYaw_, yawToward(bat_, owner), float(dice(15) + 5));
    const float goal = owner[2] + (horse ? kRavenHighOnHorse : kRavenHigh);
    // Its nose up below the goal and down 100 over it, held between: it climbs or sinks across
    // the whole band, MU's slow metre's swell.
    int odds = 28;
    if (bat_[2] < goal) {
        batPitch_ = std::min(batPitch_ + 2.0f, 15.0f);
        odds = 10;
    } else if (bat_[2] > goal + 100.0f) {
        batPitch_ = std::max(batPitch_ - 2.0f, -15.0f);
        odds = 20;
    }
    if (dice(odds) == 0) {
        float speed = far ? -(float(dice(64)) * 0.1f + 12.8f) : -(float(dice(8)) * 0.1f + 3.2f);
        if (!far) batYaw_ = std::fmod(batYaw_ + float(dice(60)) + 360.0f, 360.0f);
        speed = (speed + batDir_[1]) * 0.5f;
        batDir_[0] = 0.0f;
        batDir_[1] = speed;
        batDir_[2] = float(dice(64) - 32) * 0.1f;
    }
    // Direction rotated by Angle (AngleMatrix, VectorRotate): along -Y turned by its heading and
    // pitched, so it climbs by its speed -- MU's Angle[0] is ours negated.
    const float angles[3] = {-batPitch_, 0.0f, batYaw_};
    float m[3][3];
    angleMatrix(angles, m);
    for (int k = 0; k < 3; ++k) bat_[k] += m[k][0] * batDir_[0] + m[k][1] * batDir_[1] + m[k][2] * batDir_[2];
    bird_ = batDir_[1] < -12.0f ? Bird::Flying : Bird::Fly;
    // Fallen far behind: put back over him after 90 frames (:597-609).
    const float dx = bat_[0] - owner[0], dy = bat_[1] - owner[1];
    if (bat_[2] < owner[2] - 200.0f || dx * dx + dy * dy > 640.0f * 640.0f) {
        if (++batLost_ > kRavenLostTicks) {
            std::memcpy(bat_, owner, sizeof(bat_));
            bat_[2] += kRavenHigh;
            std::memcpy(batWas_, bat_, sizeof(batWas_));
            batLost_ = 0;
        }
    } else {
        batLost_ = 0;
    }
}

void Pets::ravenStrike(bool ball, const float target[3]) {
    if (!ravenUp_ || bird_ == Bird::Perched || bird_ == Bird::Perching) return;
    toMu(target, batPrey_);
    batTicks_ = 0;
    if (ball) {
        bird_ = Bird::Charge;
    } else {
        // Velocity 20..29, Gravity 0.5 (SetAttack, :245-286).
        bird_ = Bird::Dash;
        dashTime_ = 0;
        batSpeed_ = float(20 + dice(10));
        batPull_ = 0.5f;
    }
}

bool Pets::ravenLetGo(float from[3], float to[3], float& yaw) {
    if (!letGo_) return false;
    letGo_ = false;
    yaw = batYaw_;
    fromMu(bat_, from);
    fromMu(batPrey_, to);
    return true;
}

bool Pets::ravenCry(float seconds) {
    if (!ravenUp_) return false;
    // rand_fps_check(100) && rand_fps_check(60): one in 6000 a reference frame.
    return roll() < seconds / kReference / 6000.0f;
}

int Pets::ravenBones() const {
    if (!ravenUp_ || !ravenBody_ || !ravenBody_->skeletonMesh) return 0;
    return int(ravenBody_->skeletonMesh->bones().size());
}

bool Pets::ravenBone(int bone, float out[3]) const {
    if (bone < 0 || bone >= ravenBones()) return false;
    const float none[3] = {0.0f, 0.0f, 0.0f};
    raven_.pointOn(bone, none, out);
    return true;
}

bool Pets::ravenAt(float out[3]) const {
    if (!ravenUp_) return false;
    fromMu(bat_, out);
    return true;
}

void Pets::drawRaven(gfx::Renderer& renderer, std::vector<float>& scratch,
                     std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters) {
    if (!ravenUp_ || !ravenBody_) return;
    const int bones = raven_.pose(scratch.data());
    const int palette = bones > 0 ? renderer.addPalette(scratch.data(), bones) : -1;
    const float fade = std::clamp(ravenIn_ / kRavenFadeIn, 0.0f, 1.0f);
    // Its feathers always, and one body by its level: under 20 dkonebody, to 39 dkswing, then
    // dkthreebody (ZzzObject.cpp:981-1023). Mesh 4, foot, is never drawn.
    const int body = ravenLevel_ < 20 ? 0 : ravenLevel_ < 40 ? 1 : 2;
    for (int material : {ravenMaterials_[3], ravenMaterials_[body]}) {
        if (material < 0) continue;
        raven_.onlyBodyMaterial(material);
        const size_t from = out.size();
        if (casters) raven_.gather(palette, *casters);
        raven_.gather(palette, out);
        for (size_t i = from; i < out.size(); ++i) out[i].fade = fade;
    }
    raven_.onlyBodyMaterial(-1);
}

}  // namespace mu::game
