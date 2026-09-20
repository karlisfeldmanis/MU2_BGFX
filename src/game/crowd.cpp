#include "game/crowd.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <cmath>
#include <cstring>

#include "content/placement.h"
#include "core/log.h"
#include "core/maths.h"

namespace mu::game {
namespace {

// MU2's own BlendSeconds, traced. It is short because a walk cycle is under a second and a
// long fade would have a figure standing in two stances at once for a quarter of it.
constexpr float kBlendSeconds = 0.18f;

// A frustum, the same Gribb-Hartmann one game/town.cpp culls chunks with. A figure is not a
// chunk -- it moves, and chunk bounds are settled at cook time -- so it is tested by its own
// box. At thirty figures a per-figure test is nothing; at Lorencia's full 290 it is still
// under a microsecond, and the alternative would be re-chunking every time something walks.
struct Frustum {
    float plane[6][4];

    explicit Frustum(const float* m) {
        auto set = [&](int index, int column, float sign) {
            for (int row = 0; row < 4; ++row) {
                plane[index][row] = m[row * 4 + 3] + sign * m[row * 4 + column];
            }
        };
        set(0, 0, 1.0f);
        set(1, 0, -1.0f);
        set(2, 1, 1.0f);
        set(3, 1, -1.0f);
        set(4, 2, 1.0f);
        set(5, 2, -1.0f);
        if (!bgfx::getCaps()->homogeneousDepth) {
            for (int row = 0; row < 4; ++row) plane[4][row] = m[row * 4 + 2];
        }
    }

    bool holds(const float* centre, float radius) const {
        for (const float* p : plane) {
            const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (length < 1e-8f) continue;
            const float distance =
                (p[0] * centre[0] + p[1] * centre[1] + p[2] * centre[2] + p[3]) / length;
            if (distance < -radius) return false;
        }
        return true;
    }
};

}  // namespace

void Figure::stand(const FigureBody* body, const float position[3], float yaw, float scale) {
    body_ = body;
    std::memcpy(position_, position, sizeof(position_));
    yaw_ = yaw;
    scale_ = scale;
    clip_ = -1;
    previous_ = -1;
    time_ = 0.0f;
    fade_ = 0.0f;
    if (body_) play(body_->idleClip);
}

void Figure::play(int clip, bool restart) {
    if (!body_ || !body_->library) return;
    if (clip < 0 || clip >= int(body_->library->clips.clips.size())) return;
    if (clip == clip_ && !restart) return;
    previous_ = clip_;
    previousTime_ = time_;
    fade_ = previous_ >= 0 ? kBlendSeconds : 0.0f;
    clip_ = clip;
    time_ = 0.0f;
}

float Figure::length() const {
    if (!body_ || !body_->library || clip_ < 0) return 0.0f;
    return body_->library->clips.clips[size_t(clip_)].duration;
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

void Figure::update(float seconds) {
    if (!body_ || !body_->library || clip_ < 0) return;
    const content::CookedClip& clip = body_->library->clips.clips[size_t(clip_)];
    time_ += seconds;
    if (clip.hold) {
        time_ = std::min(time_, clip.duration);
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
            previousTime_ += seconds;
            if (!before.hold && before.duration > 0.0f) {
                previousTime_ = std::fmod(previousTime_, before.duration);
            }
        }
        if (fade_ <= 0.0f) {
            fade_ = 0.0f;
            previous_ = -1;
        }
    }
}

void Figure::sample(int clip, float time, float* rotations, float* translations) const {
    const content::CookedClips& clips = body_->library->clips;
    const content::CookedClip& one = clips.clips[size_t(clip)];
    const uint32_t bones = clips.bones;

    float where = 0.0f;
    if (one.frames > 1 && one.duration > 0.0f) {
        where = time / one.duration * float(one.frames - 1);
    }
    uint32_t frame = uint32_t(where);
    if (frame + 1 >= one.frames) frame = one.frames > 1 ? one.frames - 2 : 0;
    const float t = one.frames > 1 ? where - float(frame) : 0.0f;

    const float* a = &clips.rows[(size_t(one.firstRow) + size_t(frame) * bones) * 7];
    const float* b = one.frames > 1 ? a + size_t(bones) * 7 : a;

    const size_t count = body_->clipBoneOf.size();
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
    // that allocates is a pose that allocates thirty-one times a frame.
    constexpr size_t kMaxBones = 128;
    if (count > kMaxBones) return 0;
    float rotations[kMaxBones * 4];
    float translations[kMaxBones * 3];
    sample(clip_, time_, rotations, translations);

    if (fade_ > 0.0f && previous_ >= 0) {
        float wasRotations[kMaxBones * 4];
        float wasTranslations[kMaxBones * 3];
        sample(previous_, previousTime_, wasRotations, wasTranslations);
        // 0 at the start of the fade and 1 at its end: the new clip arrives rather than
        // starting whole.
        const float t = 1.0f - fade_ / kBlendSeconds;
        for (size_t i = 0; i < count; ++i) {
            float blended[4];
            core::nlerpQuat(&wasRotations[i * 4], &rotations[i * 4], t, blended);
            std::memcpy(&rotations[i * 4], blended, sizeof(blended));
            float moved[3];
            core::lerpVec3(&wasTranslations[i * 3], &translations[i * 3], t, moved);
            std::memcpy(&translations[i * 3], moved, sizeof(moved));
        }
    }

    // One walk of the hierarchy, parents first -- which the cook guarantees and the reader
    // checks -- then the inverse bind, then the transpose the shader reads.
    world_.resize(count * 16);
    float* world = world_.data();
    for (size_t i = 0; i < count; ++i) {
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
    return int(count);
}

void Figure::gather(int row, std::vector<gfx::Drawable>& out) const {
    if (!body_) return;
    float transform[16];
    // The same transform MU builds for a placement, and by the same code: a figure standing
    // in the town is placed exactly as a barrel is. Pitch and roll are zero -- a figure
    // stands upright whatever the ground does, which is MU's own behaviour.
    content::placementTransform(0.0f, yaw_, 0.0f, scale_, position_, transform);

    for (const content::Mesh* part : body_->parts) {
        gfx::Drawable drawable;
        drawable.mesh = part;
        std::memcpy(drawable.transform, transform, sizeof(transform));
        drawable.paletteRow = part->isSkinned() ? row : -1;
        out.push_back(drawable);
    }

    // A held item is rigid and rides its bone: the bone's own world matrix -- the pose's,
    // before the inverse bind, which is where the bone actually IS -- times where the figure
    // stands. The grip bones sit where a grip belongs rather than at the wrist, so there is
    // no correction to derive, which is the whole reason MU names a bone.
    if (world_.empty()) return;
    for (const HeldItem& item : body_->held) {
        if (!item.mesh || item.bone < 0) continue;
        if (size_t(item.bone) * 16 + 16 > world_.size()) continue;
        gfx::Drawable drawable;
        drawable.mesh = item.mesh;
        // No row: a rigid item needs no palette, and a bow -- which carries a 12-bone rig of
        // its own -- takes the renderer's bind row and is drawn in its own bind pose. Its
        // clip is owed with the items.
        drawable.paletteRow = -1;
        core::mulMatrix(&world_[size_t(item.bone) * 16], transform, drawable.transform);
        out.push_back(drawable);
    }
}

// ---- the crowd ------------------------------------------------------------------------

void Crowd::open(const Figures& figures, const content::Ground& ground, int monsters,
                 float focusColumn, float focusRow, const std::string& player) {
    figures_.clear();
    bones_ = 0;

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
        figure.stand(body, position, 0.0f, body->scale);
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
        figure.stand(body, one.position, one.yaw, one.scale);
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

        // The sun's list is every figure, as the town's is: a figure behind the camera still
        // casts into the frame, and culling the shadow pass with the camera's frustum is the
        // bug foundation 7 names.
        if (casters) figure.gather(row, *casters);

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
