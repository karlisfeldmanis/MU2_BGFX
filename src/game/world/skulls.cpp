#include "game/world/skulls.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/world/town.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
// MoveObject's frame, which every number below is written per.
constexpr float kStepSeconds = 1.0f / 25.0f;
// CheckSkull's reach, its kick and its spin, and the damping both share.
constexpr float kReachUnits = 50.0f;
constexpr float kKick = 0.4f;
constexpr float kSpin = 4.0f;
constexpr float kDamp = 0.6f;
// The rest test, `Direction[0] < 0.1`, signed, as MU wrote it.
constexpr float kRest = 0.1f;
// Below this a piece is still, and is dropped from the moving list. Ours: MU damps forever.
constexpr float kStill = 0.001f;
// mBone2 is loaded with two voices (ZzzOpenData.cpp:4807).
constexpr size_t kVoices = 2;

}  // namespace

void Skulls::open(const std::string& world, const Town& town, float metresPerTile) {
    pieces_.clear();
    moving_.clear();
    clatters_.clear();
    clock_ = 0.0f;
    kicks_ = 0;
    metresPerUnit_ = metresPerTile / 100.0f;
    if (world != "losttower") return;
    const auto& models = town.cooked().models;
    const auto& instances = town.cooked().instances;
    int skulls = 0, chips = 0;
    for (uint32_t i = 0; i < instances.size(); ++i) {
        const content::TownInstance& at = instances[i];
        if (at.model >= models.size()) continue;
        const std::string& name = models[at.model].name;
        const bool skull = name == "Object39";
        if (!skull && name != "Object40") continue;
        Piece piece;
        piece.instance = i;
        // Our (x, y, z) is MU's (x, z, -y); the cook's pitch is MU's Angle[0] and its roll
        // MU's Angle[1] negated (tools/cook.py).
        piece.position[0] = at.position[0] / metresPerUnit_;
        piece.position[1] = -at.position[2] / metresPerUnit_;
        piece.angle[0] = at.pitch * 180.0f / kPi;
        piece.angle[1] = -at.roll * 180.0f / kPi;
        piece.height = at.position[1];
        piece.yaw = at.yaw;
        for (int a = 0; a < 2; ++a) {
            piece.wasPosition[a] = piece.position[a];
            piece.wasAngle[a] = piece.angle[a];
        }
        pieces_.push_back(piece);
        (skull ? skulls : chips)++;
    }
    if (!pieces_.empty()) {
        core::logf("skulls %s: %d skulls and %d stone chips to kick", world.c_str(), skulls,
                   chips);
    }
}

void Skulls::shutdown() {
    if (!pieces_.empty()) core::logf("skulls: %u kicks", kicks_);
    pieces_.clear();
    moving_.clear();
}

void Skulls::step(bool heroMoving, float heroX, float heroY) {
    for (uint32_t index : moving_) {
        Piece& piece = pieces_[index];
        for (int a = 0; a < 2; ++a) {
            piece.wasPosition[a] = piece.position[a];
            piece.wasAngle[a] = piece.angle[a];
        }
    }
    // The kicks: every piece at rest within reach of a walking hero.
    if (heroMoving) {
        for (uint32_t index = 0; index < pieces_.size(); ++index) {
            Piece& piece = pieces_[index];
            if (piece.direction[0] >= kRest) continue;
            const float dx = heroX - piece.position[0];
            const float dy = heroY - piece.position[1];
            if (std::hypot(dx, dy) >= kReachUnits) continue;
            piece.direction[0] = -dx * kKick;
            piece.direction[1] = -dy * kKick;
            piece.headAngle[1] = -dx * kSpin;
            piece.headAngle[0] = -dy * kSpin;
            ++kicks_;
            if (clatters_.size() < kVoices) {
                clatters_.push_back({{piece.position[0] * metresPerUnit_, piece.height,
                                      -piece.position[1] * metresPerUnit_}});
            }
            if (!piece.moving) {
                piece.moving = true;
                for (int a = 0; a < 2; ++a) {
                    piece.wasPosition[a] = piece.position[a];
                    piece.wasAngle[a] = piece.angle[a];
                }
                moving_.push_back(index);
            }
        }
    }
    // And the slide, kicked or not.
    for (uint32_t index : moving_) {
        Piece& piece = pieces_[index];
        for (int a = 0; a < 2; ++a) {
            piece.direction[a] *= kDamp;
            piece.headAngle[a] *= kDamp;
            piece.position[a] += piece.direction[a];
            piece.angle[a] += piece.headAngle[a];
        }
    }
}

void Skulls::write(Piece& piece, float blend, Town& town) const {
    float position[2], angle[2];
    for (int a = 0; a < 2; ++a) {
        position[a] = piece.wasPosition[a] + (piece.position[a] - piece.wasPosition[a]) * blend;
        angle[a] = piece.wasAngle[a] + (piece.angle[a] - piece.wasAngle[a]) * blend;
    }
    const float at[3] = {position[0] * metresPerUnit_, piece.height,
                         -position[1] * metresPerUnit_};
    town.posePlacement(piece.instance, angle[0] * kPi / 180.0f, piece.yaw,
                       -angle[1] * kPi / 180.0f, at);
}

void Skulls::update(float seconds, float heroX, float heroZ, bool heroMoving, Town& town) {
    clatters_.clear();
    if (pieces_.empty()) return;
    const float heroUnitsX = heroX / metresPerUnit_;
    const float heroUnitsY = -heroZ / metresPerUnit_;
    clock_ += seconds;
    while (clock_ >= kStepSeconds) {
        clock_ -= kStepSeconds;
        step(heroMoving, heroUnitsX, heroUnitsY);
    }
    const float blend = clock_ / kStepSeconds;
    // Each moving piece drawn between its last two steps; one that has come to rest is drawn
    // where it stopped and leaves the list.
    size_t kept = 0;
    for (uint32_t index : moving_) {
        Piece& piece = pieces_[index];
        const bool still = std::fabs(piece.direction[0]) < kStill &&
                           std::fabs(piece.direction[1]) < kStill &&
                           std::fabs(piece.headAngle[0]) < kStill &&
                           std::fabs(piece.headAngle[1]) < kStill;
        if (still) {
            piece.moving = false;
            piece.direction[0] = piece.direction[1] = 0.0f;
            piece.headAngle[0] = piece.headAngle[1] = 0.0f;
            write(piece, 1.0f, town);
            continue;
        }
        write(piece, blend, town);
        moving_[kept++] = index;
    }
    moving_.resize(kept);
}

}  // namespace mu::game
