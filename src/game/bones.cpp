#include "game/bones.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

}  // namespace

uint32_t Bones::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

float Bones::unit() { return float(roll() & 0xFFFFFFu) / float(0x1000000u); }

float Bones::between(float a, float b) { return a + unit() * (b - a); }

bool Bones::open(const std::string& assetDir, content::Textures& textures,
                 const content::Ground* ground) {
    ground_ = ground;
    // The cooked missiles, which is where every thrown or flung model in the game now lives:
    // the table is read here rather than handed in, because two meshes and two lifts is the
    // whole of what this wants out of eighteen rows.
    content::Missiles missiles;
    if (!missiles.read(core::join(assetDir, "cooked/showing/missiles.mup"))) {
        core::logError("bones: no cooked missiles (tools/cook.py --only missiles)");
        return false;
    }
    const auto take = [&](const char* name, Model& into) {
        const content::MissileRow* row = missiles.find(name);
        if (row == nullptr) {
            core::logError("bones: no cooked missile named '%s'", name);
            return;
        }
        // The lift is the number most easily lost, so it is read from the row and never
        // written here: MU gets Bone01's 1.5 m by falling THROUGH `case MODEL_BONE1` into
        // `case MODEL_BONE2` -- 50 units and then 100 -- because the first case has no break
        // (ZzzEffect.cpp:2647-2650). Read the first case alone and the large bone is given 50
        // and starts buried inside the small ones.
        into.lift = row->lift * kUnit;
        into.mesh = nullptr;
        auto made = std::make_unique<content::Mesh>();
        const std::vector<uint8_t> bytes = core::readFile(core::join(assetDir, row->mesh));
        content::CookedMesh cooked;
        std::string error;
        if (!bytes.empty() && content::parseCookedMesh(bytes, cooked, error) &&
            made->buildFromCooked(cooked, name, assetDir, textures)) {
            owned_.push_back(std::move(made));
            into.mesh = owned_.back().get();
        } else {
            core::logError("bones: %s would not load (%s)", name, error.c_str());
        }
    };
    take("Bone01", large_);
    take("Bone02", small_);
    core::logf("bones: Bone01 %s at %.2f m, Bone02 %s at %.2f m",
               large_.mesh ? "loaded" : "MISSING", large_.lift,
               small_.mesh ? "loaded" : "MISSING", small_.lift);
    return isOpen();
}

void Bones::shutdown() {
    for (auto& mesh : owned_) {
        if (mesh) mesh->shutdown();
    }
    owned_.clear();
    large_ = Model{};
    small_ = Model{};
    for (auto& piece : pieces_) piece.alive = false;
}

void Bones::throwOne(const Model& model, float x, float z, float floorY, float bodyScale) {
    if (model.mesh == nullptr) return;
    Piece* slot = nullptr;
    for (auto& piece : pieces_) {
        if (!piece.alive) { slot = &piece; break; }
    }
    if (slot == nullptr) {
        ++refused_;
        return;
    }
    slot->alive = true;
    slot->landed = false;
    slot->fade = 0.0f;
    slot->mesh = model.mesh;
    slot->left = kLeastFrames + float(roll() % uint32_t(kMoreFrames));
    slot->size = between(kSmallestPiece, kLargestPiece) * bodyScale;
    // An ACCELERATION -- `HeadAngle[2] -= Gravity` every frame -- so units a frame SQUARED and
    // two factors of 25, which comes out at five to fifteen g. That is why MU's debris snaps
    // down instead of floating; read as a speed the whole burst plays in slow motion.
    slot->gravity = (kLeastGravity + float(roll() % uint32_t(kMoreGravity))) * kUnit *
                    kReferenceFps * kReferenceFps;
    // Flat, and thrown OUT: the scatter has no vertical part at all. What puts a piece in the
    // air is the lift it is born at and nothing else.
    const float scatter = (kLeastScatter + float(roll() % uint32_t(kMoreScatter))) * 0.1f *
                          kUnit * kReferenceFps;
    slot->yaw = unit() * kTwoPi;
    slot->position[0] = x;
    slot->position[1] = floorY + model.lift;
    slot->position[2] = z;
    slot->velocity[0] = std::sin(slot->yaw) * scatter;
    slot->velocity[1] = 0.0f;
    slot->velocity[2] = std::cos(slot->yaw) * scatter;
    slot->lean[0] = unit() * kTwoPi;
    slot->lean[1] = unit() * kTwoPi;
}

void Bones::burst(float x, float z, float floorY, float bodyScale) {
    for (int i = 0; i < kLarge; ++i) throwOne(large_, x, z, floorY, bodyScale);
    for (int i = 0; i < kSmall; ++i) throwOne(small_, x, z, floorY, bodyScale);
}

void Bones::update(float seconds) {
    const float refFrames = seconds * kReferenceFps;
    for (auto& piece : pieces_) {
        if (!piece.alive) continue;
        piece.left -= refFrames;
        // It dies where it is, wherever that is: the life is the whole of it, and a piece
        // still in the air when it runs out simply stops being drawn.
        if (piece.left <= 0.0f) { piece.alive = false; continue; }

        // Gravity into the velocity FIRST, then the position, which is MU's order.
        piece.velocity[1] -= piece.gravity * seconds;
        for (int a = 0; a < 3; ++a) piece.position[a] += piece.velocity[a] * seconds;

        // `Angle[0] += 0.5 * LifeTime` and the same on `Angle[1]`, with the life counting
        // DOWN -- so a piece spins fast while it is young and slows as it ages. At a constant
        // rate the bones are still spinning as they lie there.
        const float tumble = kTumble * piece.left * kReferenceFps * kPi / 180.0f * seconds;
        piece.lean[0] += tumble;
        piece.lean[1] += tumble;

        const float floor = ground_ ? ground_->heightAt(piece.position[0], piece.position[2])
                                    : 0.0f;
        if (piece.position[1] <= floor) {
            piece.position[1] = floor;
            piece.landed = true;
            piece.velocity[0] *= std::pow(kBounceDrag, refFrames);
            piece.velocity[2] *= std::pow(kBounceDrag, refFrames);
            // A VELOCITY -- one factor of 25 -- and taken from what is left of the life, so
            // the pop shrinks every time the piece comes down.
            const float bounce = piece.left * kUnit * kReferenceFps;
            piece.velocity[1] = bounce < kRestUnder * kUnit * kReferenceFps ? 0.0f : bounce;
        }
        // And only once it has landed: a bone lands, pops, skids and dissolves where it lies.
        // It is not taken away at the landing.
        if (piece.landed) piece.fade = std::min(1.0f, piece.fade + kFade * refFrames);
    }
}

void Bones::gather(std::vector<gfx::Drawable>& out) const {
    for (const auto& piece : pieces_) {
        if (!piece.alive || piece.mesh == nullptr) continue;
        gfx::Drawable drawable;
        drawable.mesh = piece.mesh;
        // Row-vector matrices, as the rest of this engine keeps them: the translation is the
        // last row. Three rotations composed with two plain 3x3 multiplies and the scale
        // applied once at the end -- written this way rather than expanded by hand because
        // the hand-expanded version of this had the scale on two of its nine entries and on
        // none of the other seven, which is a piece that stretches as it turns.
        const float cy = std::cos(piece.yaw), sy = std::sin(piece.yaw);
        const float cx = std::cos(piece.lean[0]), sx = std::sin(piece.lean[0]);
        const float cz = std::cos(piece.lean[1]), sz = std::sin(piece.lean[1]);
        const float rz[9] = {cz, sz, 0.0f, -sz, cz, 0.0f, 0.0f, 0.0f, 1.0f};
        const float rx[9] = {1.0f, 0.0f, 0.0f, 0.0f, cx, sx, 0.0f, -sx, cx};
        const float ry[9] = {cy, 0.0f, -sy, 0.0f, 1.0f, 0.0f, sy, 0.0f, cy};
        const auto times = [](const float a[9], const float b[9], float out[9]) {
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    out[r * 3 + c] = a[r * 3 + 0] * b[c] + a[r * 3 + 1] * b[3 + c] +
                                     a[r * 3 + 2] * b[6 + c];
                }
            }
        };
        float zx[9], m[9];
        times(rz, rx, zx);
        times(zx, ry, m);
        float* t = drawable.transform;
        t[0] = m[0] * piece.size; t[1] = m[1] * piece.size; t[2] = m[2] * piece.size; t[3] = 0.0f;
        t[4] = m[3] * piece.size; t[5] = m[4] * piece.size; t[6] = m[5] * piece.size; t[7] = 0.0f;
        t[8] = m[6] * piece.size; t[9] = m[7] * piece.size; t[10] = m[8] * piece.size; t[11] = 0.0f;
        t[12] = piece.position[0];
        t[13] = piece.position[1];
        t[14] = piece.position[2];
        t[15] = 1.0f;
        // Not in the probe, as nothing that is not the town is: the cube is taken at the
        // player's chest and a bone at his feet would be reflected out of it.
        drawable.inProbe = false;
        drawable.fade = 1.0f - piece.fade;
        if (drawable.fade > 0.0f) out.push_back(drawable);
    }
}

uint32_t Bones::live() const {
    uint32_t count = 0;
    for (const auto& piece : pieces_) if (piece.alive) ++count;
    return count;
}

}  // namespace mu::game
