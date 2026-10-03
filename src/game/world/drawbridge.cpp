#include "game/world/drawbridge.h"

#include "core/log.h"
#include "game/world/town.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kReference = 25.0f;  // MoveObject's frames a second

}  // namespace

void Drawbridge::open(const std::string& world, const Town& town, float metresPerTile) {
    shutdown();
    metresPerTile_ = metresPerTile;
    if (world != "bloodcastle") return;
    const auto& models = town.cooked().models;
    const auto& instances = town.cooked().instances;
    for (uint32_t i = 0; i < instances.size(); ++i) {
        const content::TownInstance& at = instances[i];
        if (at.model >= models.size()) continue;
        const std::string& name = models[at.model].name;
        if (name == "Object37" && door_ < 0) {
            door_ = int(i);
            for (int a = 0; a < 3; ++a) rest_[a] = at.position[a];
            yaw_ = at.yaw;
            roll_ = at.roll;
            degrees_ = was_ = at.pitch * 180.0f / kPi;
        } else if (name == "Object10" || name == "Object11") {
            deck_.push_back(i);
        }
    }
    if (door_ >= 0) {
        core::logf("drawbridge: Object37 raised at %.0f degrees, %zu deck pieces held back",
                   double(degrees_), deck_.size());
    }
}

void Drawbridge::shutdown() {
    state_ = State::Raised;
    door_ = -1;
    deck_.clear();
    time_ = 20;
    speed_ = 1.0f;
    frame_ = 0.0f;
    started_ = false;
    landed_ = false;
}

void Drawbridge::tip(float out[3]) const {
    // Six tiles from the hinge, down the bridge: MU's +y is our -z, and it falls to -y.
    out[0] = rest_[0];
    out[1] = rest_[1];
    out[2] = rest_[2] + 6.0f * metresPerTile_;
}

void Drawbridge::lower(Town& town) {
    state_ = State::Lowered;
    // MU hides the door here and its terrain's planks show in its place (HiddenMesh -2); ours
    // keeps it lying flat over the gap, the planks under it: the swap read as the bridge's
    // texture changing as it landed (the user, 2026-10-03: 'for some reason texture changes
    // when gate drop happens').
    town.posePlacement(uint32_t(door_), 90.0f * kPi / 180.0f, yaw_, roll_, rest_);
    for (uint32_t piece : deck_) town.setHidden(piece, false);
}

void Drawbridge::update(float seconds, bool falling, bool down, Town& town) {
    started_ = false;
    landed_ = false;
    if (door_ < 0 || state_ == State::Lowered) return;
    if (state_ == State::Raised) {
        if (down && !falling) {
            lower(town);
            return;
        }
        if (!falling) return;
        state_ = State::Falling;
        frame_ = 1.0f;  // the first frame is stepped now
    } else {
        frame_ += seconds * kReference;
    }
    while (frame_ >= 1.0f && state_ == State::Falling) {
        frame_ -= 1.0f;
        was_ = degrees_;
        if (time_ == 20) {
            degrees_ = was_ = 35.0f;
            started_ = true;
        }
        degrees_ += speed_;
        speed_ += 1.5f;
        // Ours: it lands flat and stays (the user, 2026-10-03: 'gate openiing has to happen
        // without bounce'). MU knocks it back by the frames still to run and swings it again.
        if (degrees_ >= 90.0f) {
            degrees_ = 90.0f;
            landed_ = true;
            lower(town);
            return;
        }
        --time_;
    }
    const float drawn = was_ + (degrees_ - was_) * frame_;
    town.posePlacement(uint32_t(door_), drawn * kPi / 180.0f, yaw_, roll_, rest_);
}

}  // namespace mu::game
