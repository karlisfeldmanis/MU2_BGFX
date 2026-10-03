#include "game/crowd.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <cmath>
#include <cstring>

#include "content/placement.h"
#include "core/log.h"
#include "core/maths.h"
#include "game/frustum.h"

namespace mu::game {
namespace {

// MU2's own BlendSeconds, traced. It is short because a walk cycle is under a second and a
// long fade would have a figure standing in two stances at once for a quarter of it.
constexpr float kBlendSeconds = 0.18f;

// MU marks a safe zone per TILE, in the 0x0001 bit of the attribute grid -- 3.9% of
// Lorencia -- and the client reads it off the figure's own tile to decide whether the weapon
// is in the hand or on the back. `Terrain.Safe` in MU2's shared code is the same line. The
// world's `gates.safe` rectangle is the gate's and is NOT this bit.
constexpr uint8_t kSafeAttribute = 0x01;

constexpr float kOrigin[3] = {0.0f, 0.0f, 0.0f};
constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

}  // namespace

void Figure::stand(const FigureBody* body, const float position[3], float yaw, float scale,
                   bool safe) {
    body_ = body;
    safe_ = safe;
    std::memcpy(position_, position, sizeof(position_));
    yaw_ = yaw;
    scale_ = scale;
    clip_ = -1;
    previous_ = -1;
    time_ = 0.0f;
    fade_ = 0.0f;
    if (body_) play(safe_ ? body_->idleSafeClip : body_->idleClip);
}

void Figure::place(const float position[3], float yaw, bool safe) {
    std::memcpy(position_, position, sizeof(position_));
    yaw_ = yaw;
    safe_ = safe;
}

namespace {

// Where a clip played `once` stops: on its last authored key, and not on the closing key the
// cook gives every looping action, which holds key 0's pose again. That interval is inside the
// clip, so clamping at its end did not save it: MU's Skill hand 1 turns the right upper arm 82
// degrees and the forearm 60 back toward its opening key in the last seventh of the clip, and
// the user saw an Energy Ball's hand "stoped and starts again".
float onceEnd(const content::CookedClip& clip) {
    if (clip.hold || clip.frames < 3) return clip.duration;
    return clip.duration * float(clip.frames - 2) / float(clip.frames - 1);
}

}  // namespace

void Figure::play(int clip, bool restart, float fade, bool once) {
    if (!body_ || !body_->library) return;
    if (clip < 0 || clip >= int(body_->library->clips.clips.size())) return;
    if (clip == clip_ && !restart) return;
    // A change made before the last one has finished fading fades from the pose on screen,
    // held still, and not from the clip under it: the fade keeps one clip behind it, and
    // dropping the half-faded one jumped the body. A wizard casting again 0.15 s into his 0.28 s
    // fade home moved both hands 0.6 m in a frame (the user: "missing motion to go back to idle
    // or cast again", "sudden change").
    frozen_ = fade_ > 0.0f && previous_ >= 0 && !shown_.empty();
    if (frozen_) held_.assign(shown_.begin(), shown_.end());
    previous_ = clip_;
    previousTime_ = time_;
    previousOnce_ = once_;
    once_ = once;
    fadeLength_ = fade >= 0.0f ? fade : kBlendSeconds;
    fade_ = previous_ >= 0 ? fadeLength_ : 0.0f;
    clip_ = clip;
    time_ = 0.0f;
}

void Figure::setClock(float seconds) {
    if (!body_ || !body_->library || clip_ < 0) return;
    const content::CookedClip& clip = body_->library->clips.clips[size_t(clip_)];
    if (clip.duration <= 0.0f) return;
    time_ = clip.hold ? std::min(seconds, clip.duration) : std::fmod(seconds, clip.duration);
    if (time_ < 0.0f) time_ += clip.duration;
}

float Figure::length() const {
    if (!body_ || !body_->library || clip_ < 0) return 0.0f;
    return body_->library->clips.clips[size_t(clip_)].duration;
}

float Figure::travel() const {
    if (!body_ || !body_->library || clip_ < 0) return 0.0f;
    // Scaled, because a body drawn at 1.2 covers 1.2 times the ground its clip was measured
    // at. MU2's `Strode` multiplies by `Sized` for the same reason, and a Budge Dragon is the
    // figure that proves it.
    return body_->library->clips.clips[size_t(clip_)].travel * scale_;
}

float Figure::played() const {
    if (!body_ || !body_->library || clip_ < 0) return 0.0f;
    const content::CookedClip& clip = body_->library->clips.clips[size_t(clip_)];
    return once_ ? onceEnd(clip) : clip.duration;
}

float Figure::through() const {
    const float duration = length();
    return duration > 0.0f ? time_ / duration : 0.0f;
}

float Figure::radius() const {
    if (!body_) return 1.0f;
    // The bind box of every part together, widened by half again and scaled. A skinned mesh
    // leaves its bind box the moment it moves -- an arm swings outside it -- and a figure
    // culled by the box it was measured in pops at the edge of the frame. One part's box is
    // not the figure's: a Dark Knight's first part is his helmet.
    return body_->radius * scale_ * 1.5f;
}

void Figure::seat(int clip, bool handsOnly) {
    seatHands_ = handsOnly;
    if (clip == seat_) return;
    seat_ = clip;
    seatTime_ = 0.0f;
}

void Figure::upper(int clip) {
    if (clip == upper_) return;
    upper_ = clip;
    upperTime_ = 0.0f;
}

void Figure::update(float seconds, float clipRate) {
    if (!body_ || !body_->library || clip_ < 0) return;
    if (upper_ >= 0 && size_t(upper_) < body_->library->clips.clips.size()) {
        const float length = body_->library->clips.clips[size_t(upper_)].duration;
        upperTime_ = length > 0.0f ? std::fmod(upperTime_ + seconds, length) : 0.0f;
    }
    if (seat_ >= 0 && size_t(seat_) < body_->library->clips.clips.size()) {
        const float length = body_->library->clips.clips[size_t(seat_)].duration;
        seatTime_ = length > 0.0f ? std::fmod(seatTime_ + seconds, length) : 0.0f;
    }
    const content::CookedClip& clip = body_->library->clips.clips[size_t(clip_)];
    // The clip's own clock runs at the rate the caller asked for; the fade below runs in real
    // seconds. See the note on this function in crowd.h.
    const float clipSeconds = seconds * clipRate;
    time_ += clipSeconds;
    if (clip.hold || once_) {
        time_ = std::min(time_, once_ ? onceEnd(clip) : clip.duration);
    } else if (clip.duration > 0.0f) {
        // The clock wraps, not the frame index. The extra key a looping clip carries holds
        // the first pose again, so the last interval IS the wrap and interpolating across it
        // is what makes a cycle continuous; wrapping the frame index instead plays the first
        // pose twice and the idle stutters once a cycle.
        time_ = std::fmod(time_, clip.duration);
    }
    if (fade_ > 0.0f) {
        fade_ -= seconds;
        if (previous_ >= 0) {
            const content::CookedClip& before = body_->library->clips.clips[size_t(previous_)];
            // The clip being faded OUT is on the same clock as the one coming in: when a walk
            // is fading out into a stop, the ground has stopped moving under both of them, and
            // a walk that kept running through its own fade would slide the feet for exactly
            // as long as the fade lasts -- which is the last step, the one that is looked at.
            previousTime_ += clipSeconds;
            if (before.hold || previousOnce_) {
                // Clamped, exactly as the clip being played is. Without this the clock of a
                // death being faded OUT of runs past its own end, and `sample` then hands
                // nlerp a t well above 1 -- extrapolation, which throws a limb somewhere the
                // clip never goes. The played clip was clamped from the first draft and this
                // one was not, which is the kind of asymmetry a crossfade hides for months.
                previousTime_ = std::min(previousTime_,
                                         previousOnce_ ? onceEnd(before) : before.duration);
            } else if (before.duration > 0.0f) {
                previousTime_ = std::fmod(previousTime_, before.duration);
            }
        }
        if (fade_ <= 0.0f) {
            fade_ = 0.0f;
            previous_ = -1;
            frozen_ = false;
        }
    }
}

void Figure::sample(int clip, float time, size_t limit, float* rotations,
                    float* translations) const {
    const content::CookedClips& clips = body_->library->clips;
    const content::CookedClip& one = clips.clips[size_t(clip)];
    const uint32_t bones = clips.bones;

    float where = 0.0f;
    if (one.frames > 1 && one.duration > 0.0f) {
        where = time / one.duration * float(one.frames - 1);
    }
    uint32_t frame = uint32_t(where);
    if (frame + 1 >= one.frames) frame = one.frames > 1 ? one.frames - 2 : 0;
    // Clamped, and not only for tidiness: `frame` is clamped above, so a clock past the end
    // would otherwise leave `t` above 1 and every blend below would extrapolate rather than
    // interpolate.
    const float t = one.frames > 1 ? std::min(std::max(where - float(frame), 0.0f), 1.0f)
                                   : 0.0f;

    const float* a = &clips.rows[(size_t(one.firstRow) + size_t(frame) * bones) * 7];
    const float* b = one.frames > 1 ? a + size_t(bones) * 7 : a;

    const size_t count = std::min(limit, body_->clipBoneOf.size());
    if (smooth_ && one.frames >= 4 && !one.hold) {
        // A looping clip's last key is its first again (CookedClip), so the cycle is
        // frames - 1 keys long and the neighbours either side of a key wrap round it.
        const int period = int(one.frames) - 1;
        const auto key = [&](int k) {
            k = ((k % period) + period) % period;
            return &clips.rows[(size_t(one.firstRow) + size_t(k) * bones) * 7];
        };
        const float* p0 = key(int(frame) - 1);
        const float* p3 = key(int(frame) + 2);
        const float t2 = t * t, t3 = t2 * t;
        // Catmull-Rom's weights on the four keys: through the middle two, and with the slope
        // at each taken from its neighbours, so the speed is continuous across a key.
        const float w0 = 0.5f * (-t3 + 2.0f * t2 - t);
        const float w1 = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
        const float w2 = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
        const float w3 = 0.5f * (t3 - t2);
        for (size_t i = 0; i < count; ++i) {
            const int32_t from = body_->clipBoneOf[i];
            if (from < 0) {
                rotations[i * 4 + 0] = rotations[i * 4 + 1] = rotations[i * 4 + 2] = 0.0f;
                rotations[i * 4 + 3] = 1.0f;
                translations[i * 3 + 0] = translations[i * 3 + 1] = translations[i * 3 + 2] = 0.0f;
                continue;
            }
            const float* q[4] = {p0 + size_t(from) * 7, a + size_t(from) * 7,
                                 b + size_t(from) * 7, p3 + size_t(from) * 7};
            // Each quaternion on the same side of the sphere as its neighbour, or the curve
            // runs the long way round.
            float sign[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            for (int k : {0, 2, 3}) {
                const float* near = q[k == 3 ? 2 : 1];
                float dot = 0.0f;
                for (int c = 0; c < 4; ++c) dot += q[k][c] * near[c];
                sign[k] = (dot < 0.0f ? -1.0f : 1.0f) * (k == 3 ? sign[2] : 1.0f);
            }
            float* out = &rotations[i * 4];
            float length = 0.0f;
            for (int c = 0; c < 4; ++c) {
                out[c] = w0 * sign[0] * q[0][c] + w1 * q[1][c] + w2 * sign[2] * q[2][c] +
                         w3 * sign[3] * q[3][c];
                length += out[c] * out[c];
            }
            length = std::sqrt(length);
            if (length < 1e-8f) {
                core::nlerpQuat(q[1], q[2], t, out);
            } else {
                for (int c = 0; c < 4; ++c) out[c] /= length;
            }
            for (int c = 0; c < 3; ++c) {
                translations[i * 3 + c] = w0 * q[0][4 + c] + w1 * q[1][4 + c] +
                                          w2 * q[2][4 + c] + w3 * q[3][4 + c];
            }
        }
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const int32_t from = body_->clipBoneOf[i];
        if (from < 0) {
            // A bone the library does not carry keeps its bind pose, which is the identity
            // once the inverse bind has undone it.
            rotations[i * 4 + 0] = rotations[i * 4 + 1] = rotations[i * 4 + 2] = 0.0f;
            rotations[i * 4 + 3] = 1.0f;
            translations[i * 3 + 0] = translations[i * 3 + 1] = translations[i * 3 + 2] = 0.0f;
            continue;
        }
        const float* ra = a + size_t(from) * 7;
        const float* rb = b + size_t(from) * 7;
        core::nlerpQuat(ra, rb, t, &rotations[i * 4]);
        core::lerpVec3(ra + 4, rb + 4, t, &translations[i * 3]);
    }
}

int Figure::pose(float* rows12) {
    if (!body_ || !body_->skeletonMesh || !body_->library || clip_ < 0) return 0;
    const std::vector<content::Bone>& bones = body_->skeletonMesh->bones();
    const size_t count = bones.size();
    if (count == 0) return 0;

    // Stack-sized for the rigs this content has: 60 bones at 7 floats is 1.7 kB, and a pose
    // that allocates is a pose that allocates thirty-one times a frame. A rig over the
    // palette's own width is posed as far as it fits and SAYS SO -- returning 0 here put the
    // whole figure in bind pose with nothing in the log, which is what a missing clip looks
    // like, and it made the renderer's own clamp unreachable.
    constexpr size_t kMaxBones = size_t(gfx::Renderer::kMaxBones);
    size_t posed = count;
    if (posed > kMaxBones) {
        core::logError("%s has %zu bones and the palette holds %zu; the rest stay in bind pose",
                       body_->name.c_str(), count, kMaxBones);
        posed = kMaxBones;
    }
    float rotations[kMaxBones * 4];
    float translations[kMaxBones * 3];
    sample(clip_, time_, posed, rotations, translations);

    if (fade_ > 0.0f && previous_ >= 0) {
        float wasRotations[kMaxBones * 4];
        float wasTranslations[kMaxBones * 3];
        if (frozen_ && held_.size() >= posed * 7) {
            for (size_t i = 0; i < posed; ++i) {
                std::memcpy(&wasRotations[i * 4], &held_[i * 7], 4 * sizeof(float));
                std::memcpy(&wasTranslations[i * 3], &held_[i * 7 + 4], 3 * sizeof(float));
            }
        } else {
            sample(previous_, previousTime_, posed, wasRotations, wasTranslations);
        }
        // 0 at the start of the fade and 1 at its end: the new clip arrives rather than
        // starting whole. Against THIS fade's own length, not the default one -- a transition
        // given a shorter fade would otherwise begin part-blended and one given a longer fade
        // would finish before it ended.
        const float t = fadeLength_ > 0.0f ? 1.0f - fade_ / fadeLength_ : 1.0f;
        for (size_t i = 0; i < posed; ++i) {
            float blended[4];
            core::nlerpQuat(&wasRotations[i * 4], &rotations[i * 4], t, blended);
            std::memcpy(&rotations[i * 4], blended, sizeof(blended));
            float moved[3];
            core::lerpVec3(&wasTranslations[i * 3], &translations[i * 3], t, moved);
            std::memcpy(&translations[i * 3], moved, sizeof(moved));
        }
    }
    // What this frame shows, before the seat and the upper layer (which are drawn over any
    // fade), kept for a change that arrives while it is still fading (`frozen_`).
    if (shown_.size() != posed * 7) shown_.resize(posed * 7);
    for (size_t i = 0; i < posed; ++i) {
        std::memcpy(&shown_[i * 7], &rotations[i * 4], 4 * sizeof(float));
        std::memcpy(&shown_[i * 7 + 4], &translations[i * 3], 3 * sizeof(float));
    }

    // The seat over it: the root, the pelvis and every bone from a thigh down out of the seat's
    // clip, so the hips sit the saddle and the spine swings from them. Bones are parents first,
    // so a leg is a thigh or a child of one.
    const size_t clipCount = body_->library->clips.clips.size();
    const bool seating = seat_ >= 0 && size_t(seat_) < clipCount;
    const bool uppering = upper_ >= 0 && size_t(upper_) < clipCount;
    if (seating || uppering) {
        if (seatBody_ != body_) {
            seatBody_ = body_;
            seated_.assign(count, 0);
            arm_.assign(count, 0);
            std::vector<uint8_t> leg(count, 0);
            for (size_t i = 0; i < count; ++i) {
                const std::string& name = bones[i].name;
                const int32_t parent = bones[i].parent;
                leg[i] = name.find("Thigh") != std::string::npos ||
                         (parent >= 0 && leg[size_t(parent)]);
                arm_[i] = name.find("Clavicle") != std::string::npos ||
                          (parent >= 0 && arm_[size_t(parent)]);
                const bool pelvis = name.size() >= 6 && name.compare(name.size() - 6, 6, "Pelvis") == 0;
                seated_[i] = leg[i] || parent < 0 || pelvis;
            }
        }
        // The layer's bones out of its own clip: the seated ones for a seat, the rest for an
        // upper body.
        // A hands-only seat takes every bone but the arms.
        const auto layer = [&](int clip, float time, bool below) {
            float layerRotations[kMaxBones * 4];
            float layerTranslations[kMaxBones * 3];
            sample(clip, time, posed, layerRotations, layerTranslations);
            for (size_t i = 0; i < posed && i < seated_.size(); ++i) {
                const bool covered = below && seatHands_ ? !arm_[i] : bool(seated_[i]) == below;
                if (!covered) continue;
                std::memcpy(&rotations[i * 4], &layerRotations[i * 4], 4 * sizeof(float));
                std::memcpy(&translations[i * 3], &layerTranslations[i * 3], 3 * sizeof(float));
            }
        };
        if (seating) layer(seat_, seatTime_, true);
        if (uppering) layer(upper_, upperTime_, false);
    }

    // One walk of the hierarchy, parents first -- which the cook guarantees and the reader
    // checks -- then the inverse bind, then the transpose the shader reads.
    world_.resize(posed * 16);
    float* world = world_.data();
    for (size_t i = 0; i < posed; ++i) {
        float local[16];
        core::composeMatrix(&rotations[i * 4], &translations[i * 3], local);
        const int32_t parent = bones[i].parent;
        if (parent < 0) {
            std::memcpy(&world[i * 16], local, sizeof(local));
        } else {
            core::mulMatrix(local, &world[size_t(parent) * 16], &world[i * 16]);
        }
        float skin[16];
        core::mulMatrix(bones[i].inverseBind, &world[i * 16], skin);
        core::writePaletteRows(skin, &rows12[i * 12]);
    }
    return int(posed);
}

void Figure::poseHeld(gfx::Renderer& renderer, float* rows12) {
    heldRows_.assign(body_ ? body_->held.size() : 0, -1);
    if (!body_) return;

    // Where the body is in a shot, in its own keys. MU plays a bow's action 0 at the attack's
    // own PlaySpeed from the moment the attack starts (the weapon's frame is held at 0 until
    // then, ZzzCharacter.cpp:10160-10166), so the weapon's key IS the body's key, wrapped
    // round the weapon's seven: the string draws with the arm and looses at key 4 with it.
    float key = -1.0f;
    if (body_->library && clip_ >= 0 && size_t(clip_) < body_->library->clips.clips.size()) {
        const content::CookedClip& one = body_->library->clips.clips[size_t(clip_)];
        // PLAYER_ATTACK_BOW, _CROSSBOW, _FLY_BOW, _FLY_CROSSBOW: MuMain's enum, 50 to 53.
        if (one.slot >= 50 && one.slot <= 53 && one.duration > 0.0f && one.frames > 1) {
            key = time_ / one.duration * float(one.frames - 1);
        }
    }

    for (size_t i = 0; i < body_->held.size(); ++i) {
        const HeldItem& item = body_->held[i];
        if (!item.clip || !item.mesh || !item.mesh->isSkinned()) continue;
        const content::CookedClips& clips = *item.clip;
        const content::CookedClip& one = clips.clips.front();
        const std::vector<content::Bone>& bones = item.mesh->bones();
        const size_t count = std::min({bones.size(), size_t(clips.bones), size_t(16),
                                       size_t(gfx::Renderer::kMaxBones)});
        if (count == 0 || one.frames == 0) continue;

        // A looping clip's last key is its first again (content::CookedClip), so the cycle
        // is frames - 1 keys long.
        float where = 0.0f;
        if (key >= 0.0f && item.onShot && one.frames > 1) {
            const float period = float(one.frames - 1);
            where = std::fmod(key, period);
        }
        uint32_t frame = uint32_t(where);
        if (frame + 1 >= one.frames) frame = one.frames > 1 ? one.frames - 2 : 0;
        const float t = one.frames > 1 ? std::min(std::max(where - float(frame), 0.0f), 1.0f)
                                       : 0.0f;
        const float* a = &clips.rows[(size_t(one.firstRow) + size_t(frame) * clips.bones) * 7];
        const float* b = one.frames > 1 ? a + size_t(clips.bones) * 7 : a;

        float world[16 * 16];
        for (size_t j = 0; j < count; ++j) {
            float rotation[4];
            float translation[3];
            core::nlerpQuat(a + j * 7, b + j * 7, t, rotation);
            core::lerpVec3(a + j * 7 + 4, b + j * 7 + 4, t, translation);
            float local[16];
            core::composeMatrix(rotation, translation, local);
            const int32_t parent = bones[j].parent;
            if (parent < 0 || size_t(parent) >= j) {
                std::memcpy(&world[j * 16], local, sizeof(local));
            } else {
                core::mulMatrix(local, &world[size_t(parent) * 16], &world[j * 16]);
            }
            float skin[16];
            core::mulMatrix(bones[j].inverseBind, &world[j * 16], skin);
            core::writePaletteRows(skin, &rows12[j * 12]);
        }
        heldRows_[i] = renderer.addPalette(rows12, int(count));
    }
}

void Figure::placement(float transform[16]) const {
    // The same transform MU builds for a placement, and by the same code: a figure standing
    // in the town is placed exactly as a barrel is. Pitch and roll are zero -- a figure
    // stands upright whatever the ground does, which is MU's own behaviour.
    content::placementTransform(0.0f, yaw_, 0.0f, scale_, position_, transform);
    if (mounted_) std::memcpy(transform, mount_, sizeof(float) * 16);
}

bool Figure::muzzle(float at[3], float along[3]) const {
    if (!body_ || world_.empty() || safe_) return false;
    for (const HeldItem& item : body_->held) {
        if (!item.mesh || item.muzzleBone < 0 || item.alwaysSlung) continue;
        const int bone = item.bone;
        if (bone < 0 || size_t(bone) * 16 + 16 > world_.size()) return false;
        float transform[16];
        placement(transform);
        float hand[16];
        core::mulMatrix(&world_[size_t(bone) * 16], transform, hand);
        const auto& rig = item.mesh->bones();
        // A point in a bone's rest frame, through the rest into the item's space, then the
        // hand's: row vectors, as every matrix here is.
        const auto carry = [&](int b, float out[3]) {
            float rest[16], whole[16];
            bx::mtxInverse(rest, rig[size_t(b)].inverseBind);
            core::mulMatrix(rest, hand, whole);
            const float* p = item.muzzleOffset;
            for (int k = 0; k < 3; ++k) {
                out[k] = p[0] * whole[k] + p[1] * whole[4 + k] + p[2] * whole[8 + k] + whole[12 + k];
            }
        };
        carry(item.muzzleBone, at);
        along[0] = along[1] = along[2] = 0.0f;
        if (item.muzzleAxis >= 0) {
            float tail[3];
            carry(item.muzzleAxis, tail);
            float length = 0.0f;
            for (int k = 0; k < 3; ++k) {
                along[k] = at[k] - tail[k];
                length += along[k] * along[k];
            }
            length = std::sqrt(length);
            if (length > 1e-5f) {
                for (int k = 0; k < 3; ++k) along[k] /= length;
            } else {
                along[0] = along[1] = along[2] = 0.0f;
            }
        }
        return true;
    }
    return false;
}

float Figure::toRelease() const {
    if (released() || !body_ || !body_->library || clip_ < 0) return 0.0f;
    const content::CookedClip& one = body_->library->clips.clips[size_t(clip_)];
    const float perKey = one.duration / float(one.frames - 1);
    const float key = time_ / perKey;
    for (const HeldItem& item : body_->held) {
        if (!item.clip || !item.onShot || item.release <= 0.0f) continue;
        const float period = float(item.clip->clips.front().frames - 1);
        return std::max(0.0f, item.release * period - std::fmod(key, period)) * perKey;
    }
    return 0.0f;
}

bool Figure::released() const {
    if (!body_ || !body_->library || clip_ < 0 ||
        size_t(clip_) >= body_->library->clips.clips.size()) {
        return true;
    }
    const content::CookedClip& one = body_->library->clips.clips[size_t(clip_)];
    if (one.slot < 50 || one.slot > 53 || one.duration <= 0.0f || one.frames < 2) return true;
    const float key = time_ / one.duration * float(one.frames - 1);
    for (const HeldItem& item : body_->held) {
        if (!item.clip || !item.onShot || item.release <= 0.0f) continue;
        const content::CookedClip& own = item.clip->clips.front();
        if (own.frames < 2) continue;
        // Wrapped as poseHeld wraps it: the weapon's key is the body's, round its cycle.
        const float period = float(own.frames - 1);
        if (key >= period) return true;
        // `release` is of the cycle, key 4 of seven (0.571): a looping clip's last key is its
        // first again, so the cycle is frames - 1 long.
        return std::fmod(key, period) >= item.release * period - 0.02f;
    }
    return true;
}

void Figure::gather(int row, std::vector<gfx::Drawable>& out) const {
    if (!body_) return;
    float transform[16];
    placement(transform);

    for (size_t i = 0; i < body_->parts.size(); ++i) {
        const content::Mesh* part = body_->parts[i];
        gfx::Drawable drawable;
        drawable.mesh = part;
        std::memcpy(drawable.transform, transform, sizeof(transform));
        drawable.paletteRow = part->isSkinned() ? row : -1;
        if (i < body_->partShine.size()) wear(body_->partShine[i], drawable);
        // MU's Level 3 (Selection.cpp:106, body light +1.5): the Ice Queen is drawn at her
        // sheet's own colour in any light. Its w is the flag vs_skinned reads.
        if (body_->name == "IceQueen01") drawable.light[3] = 2.0f;
        out.push_back(drawable);
    }

    // A held item rides a bone: the bone's own world matrix -- the pose's, before the
    // inverse bind, which is where the bone actually IS -- times where the figure stands.
    //
    // **In the hand there is nothing to correct.** The rig's grip bones sit where a grip
    // belongs rather than at the wrist, which is the whole reason MU names a bone, and MU2's
    // Model.cs says it in one line: "Identity in the hand, and MU's own numbers on the back."
    //
    // **On the back there is everything to correct**, because Bone05 is a bare point between
    // the shoulders. Inside a safe zone that is where the weapon goes, and the arrangement is
    // MU's own, per kind of thing.
    if (world_.empty()) return;
    for (size_t index = 0; index < body_->held.size(); ++index) {
        const HeldItem& item = body_->held[index];
        if (!item.mesh) continue;
        const bool slung = (safe_ || item.alwaysSlung) && body_->backBone >= 0;
        if (emptyHand_ && !slung && item.kind == "weapon" && inRightHand(item)) continue;
        const int bone = slung ? body_->backBone : item.bone;
        if (bone < 0 || size_t(bone) * 16 + 16 > world_.size()) continue;

        gfx::Drawable drawable;
        drawable.mesh = item.mesh;
        wear(item.shine, drawable);
        // No row for a rigid item. A bow or a crossbow -- a 12- or 13-bone rig of its own --
        // takes the row poseHeld posed it into, its string drawn on the shot; without one
        // (no clip cooked, or poseHeld not called) the renderer's bind row, its own rest.
        drawable.paletteRow = index < heldRows_.size() ? heldRows_[index] : -1;
        // Its arrow gone from the string between the loose and the next draw (nock).
        if (nockGone_ && !slung) drawable.hiddenMaterial = item.nockedMaterial;

        float local[16];
        if (slung) {
            float offset[3] = {item.backOffset[0], item.backOffset[1], item.backOffset[2]};
            if (item.centred) {
                // Placed by its middle rather than by its origin: MU hangs a shield by a
                // point inside its mesh and the disc then occupies the same space as the
                // armour. The centre is taken in the item's own frame and turned by the
                // rotation already set on it, because the offset it is subtracted from is in
                // the socket's frame.
                float turn[16];
                content::placementTransform(item.backRotation[0] * 3.14159265f / 180.0f,
                                            item.backRotation[1] * 3.14159265f / 180.0f,
                                            item.backRotation[2] * 3.14159265f / 180.0f, 1.0f,
                                            kOrigin, turn);
                const content::Bounds& box = item.mesh->bounds();
                for (int axis = 0; axis < 3; ++axis) {
                    offset[axis] -= box.centre[0] * turn[0 * 4 + axis] +
                                    box.centre[1] * turn[1 * 4 + axis] +
                                    box.centre[2] * turn[2 * 4 + axis];
                }
            }
            content::placementTransform(item.backRotation[0] * 3.14159265f / 180.0f,
                                        item.backRotation[1] * 3.14159265f / 180.0f,
                                        item.backRotation[2] * 3.14159265f / 180.0f, 1.0f,
                                        offset, local);
            // Mirrored across the socket's axis that runs across his back (a column, the turn
            // and the offset both), and the item across its own thinnest axis (a row) -- the
            // flat of the blade, where a reflection changes nothing one can see. Two
            // reflections, so it stays a rotation and the head stays at its own end.
            if (item.mirrorAxis >= 0 && item.mirrorAxis < 3) {
                const int m = item.mirrorAxis;
                const content::Bounds& box = item.mesh->bounds();
                int thin = 0;
                for (int axis = 1; axis < 3; ++axis) {
                    if (box.max[axis] - box.min[axis] < box.max[thin] - box.min[thin]) thin = axis;
                }
                for (int i = 0; i < 4; ++i) local[i * 4 + m] = -local[i * 4 + m];
                for (int j = 0; j < 3; ++j) local[thin * 4 + j] = -local[thin * 4 + j];
            }
        } else {
            std::memcpy(local, kIdentity, sizeof(local));
        }

        float atBone[16];
        core::mulMatrix(local, &world_[size_t(bone) * 16], atBone);
        core::mulMatrix(atBone, transform, drawable.transform);
        out.push_back(drawable);
    }
}

bool Figure::pointOn(int bone, const float local[3], float out[3]) const {
    if (!body_ || bone < 0 || size_t(bone) * 16 + 16 > world_.size()) return false;
    float transform[16];
    content::placementTransform(pitch_, yaw_, roll_, scale_, position_, transform);
    float placed[16];
    core::mulMatrix(&world_[size_t(bone) * 16], transform, placed);
    for (int j = 0; j < 3; ++j) {
        out[j] = local[0] * placed[0 * 4 + j] + local[1] * placed[1 * 4 + j] +
                 local[2] * placed[2 * 4 + j] + placed[3 * 4 + j];
    }
    return true;
}

bool Figure::boneWorld(int bone, float out[16]) const {
    if (!body_ || bone < 0 || size_t(bone) * 16 + 16 > world_.size()) return false;
    float transform[16];
    content::placementTransform(pitch_, yaw_, roll_, scale_, position_, transform);
    core::mulMatrix(&world_[size_t(bone) * 16], transform, out);
    return true;
}

void Figure::mount(const float parent[16]) {
    std::memcpy(mount_, parent, sizeof(mount_));
    mounted_ = true;
}

bool Figure::pointOnBind(int bone, const float model[3], float out[3]) const {
    if (!body_ || !body_->skeletonMesh || bone < 0 ||
        size_t(bone) >= body_->skeletonMesh->bones().size()) {
        return false;
    }
    // Into the bone's own frame through its inverse bind, then out along its pose.
    const float* inverse = body_->skeletonMesh->bones()[size_t(bone)].inverseBind;
    float local[3];
    for (int j = 0; j < 3; ++j) {
        local[j] = model[0] * inverse[0 * 4 + j] + model[1] * inverse[1 * 4 + j] +
                   model[2] * inverse[2 * 4 + j] + inverse[3 * 4 + j];
    }
    return pointOn(bone, local, out);
}

void Figure::pointInModel(const float model[3], float out[3]) const {
    float transform[16];
    content::placementTransform(pitch_, yaw_, roll_, scale_, position_, transform);
    for (int j = 0; j < 3; ++j) {
        out[j] = model[0] * transform[0 * 4 + j] + model[1] * transform[1 * 4 + j] +
                 model[2] * transform[2 * 4 + j] + transform[3 * 4 + j];
    }
}

// ---- the crowd ------------------------------------------------------------------------

void Crowd::open(const Figures& figures, const content::Ground& ground, int monsters,
                 float focusColumn, float focusRow, const std::string& player) {
    figures_.clear();
    bones_ = 0;

    // Whether the tile a figure stands on is one of MU's safe ones.
    auto safeAt = [&ground](const float position[3]) {
        const float per = ground.metresPerTile();
        const int column = int(position[0] / per);
        const int row = int(-position[2] / per);
        return (ground.attributesAt(column, row) & kSafeAttribute) != 0;
    };


    // The character the sprint is judged by, standing where the camera looks: five worn
    // parts and two things in his hands, against one palette row. He is not the player yet
    // -- nothing moves him and nothing asks him to -- but he is a dressed figure on the
    // player rig, which is what the sentence wants seen.
    size_t players = 0;
    if (const FigureBody* body = figures.body(player)) {
        const float x = (focusColumn + 0.5f) * ground.metresPerTile();
        const float z = -(focusRow + 0.5f) * ground.metresPerTile();
        const float position[3] = {x, ground.heightAt(x, z), z};
        Figure figure;
        figure.stand(body, position, 0.0f, body->scale, safeAt(position));
        figures_.push_back(figure);
        ++players;
    } else if (!player.empty()) {
        core::logError("no character called %s to stand in the town", player.c_str());
    }

    // The fourteen the town's own placement list carries.
    size_t townsfolk = 0;
    for (const FigurePlacement& one : figures.placements()) {
        const FigureBody* body = figures.body(one.figure);
        if (!body) continue;
        Figure figure;
        figure.stand(body, one.position, one.yaw, one.scale, safeAt(one.position));
        figures_.push_back(figure);
        ++townsfolk;
    }

    // And a crowd of monsters, in Lorencia's own mix. Every breed's share is its share of
    // the map's spawn counts, so the picture is the town's rather than a chosen one; they
    // stand in a ring about the camera's focus because a monster the camera cannot see
    // proves nothing about the frame.
    int total = 0;
    for (const Breed& breed : figures.breeds()) total += breed.total;
    if (total > 0 && monsters > 0) {
        int placed = 0;
        for (size_t i = 0; i < figures.breeds().size() && placed < monsters; ++i) {
            const Breed& breed = figures.breeds()[i];
            int share = int(float(breed.total) / float(total) * float(monsters) + 0.5f);
            if (i + 1 == figures.breeds().size()) share = monsters - placed;
            for (int n = 0; n < share && placed < monsters; ++n, ++placed) {
                // A spiral rather than a grid: a grid of thirty monsters seen from MU's
                // camera is a wall, and half of them are behind the other half.
                const float angle = float(placed) * 2.39996f;  // the golden angle
                const float reach = 2.0f + 0.9f * std::sqrt(float(placed));
                const float column = focusColumn + std::cos(angle) * reach;
                const float row = focusRow + std::sin(angle) * reach;
                const float x = (column + 0.5f) * ground.metresPerTile();
                const float z = -(row + 0.5f) * ground.metresPerTile();
                const float position[3] = {x, ground.heightAt(x, z), z};
                Figure figure;
                figure.stand(breed.body, position, angle, breed.body->scale);
                figures_.push_back(figure);
            }
        }
    }

    for (const Figure& figure : figures_) bones_ += figure.body()->boneCount();
    // 128 bones and not the palette's 64: Figure::pose writes a rig's full bone count and
    // the palette clamps afterwards, so the scratch has to hold the larger of the two or a
    // big rig writes past it. Nothing in this content is over 60; the lobby's faces, at 100
    // to 115, would be the first to find out.
    scratch_.assign(size_t(128) * 12, 0.0f);
    core::logf("crowd: %zu figures, %zu bones: %zu player, %zu of the town's own and %zu "
               "monsters", figures_.size(), bones_, players, townsfolk,
               figures_.size() - townsfolk - players);
}

void Crowd::update(float seconds) {
    for (Figure& figure : figures_) figure.update(seconds);
}

void Crowd::gather(gfx::Renderer& renderer, const float* viewProj,
                   std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters) {
    drawn_ = culled_ = 0;
    const int64_t started = bx::getHPCounter();
    // Built once, not once a figure: it is thirty-odd square roots either way, and the cull
    // is not what this loop is timing.
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Frustum frustum(viewProj ? viewProj : identity);

    for (Figure& figure : figures_) {
        const int bones = figure.pose(scratch_.data());
        const int row = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        figure.poseHeld(renderer, scratch_.data());

        // The sun's list is every figure, as the town's is: a figure behind the camera still
        // casts into the frame, and culling the shadow pass with the camera's frustum is the
        // bug foundation 7 names.
        // Save the Ice Queen, whom MU leaves out of its shadow block (ZzzCharacter.cpp:8666).
        const bool shadowless = figure.body() && figure.body()->name == "IceQueen01";
        if (casters && !shadowless) figure.gather(row, *casters);

        if (viewProj) {
            float centre[3] = {figure.position()[0],
                               figure.position()[1] + figure.radius() * 0.5f,
                               figure.position()[2]};
            if (!frustum.holds(centre, figure.radius())) {
                ++culled_;
                continue;
            }
        }
        ++drawn_;
        figure.gather(row, out);
    }
    poseMs_ = double(bx::getHPCounter() - started) * 1000.0 / double(bx::getHPFrequency());
}

void Crowd::shutdown() {
    figures_.clear();
    scratch_.clear();
    bones_ = 0;
}

}  // namespace mu::game
