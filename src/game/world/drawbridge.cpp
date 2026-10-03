#include "game/world/drawbridge.h"

#include "core/log.h"
#include "game/world/town.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
// When eDownGate's thud comes: its chain rattles from 0.12 s to 0.8 s, goes quiet, and the
// board strikes at 1.18 s (measured off the wav's loudness in 20 ms windows). The fall lands
// on it (the user, 2026-10-03: 'gates drop has to be sync with sound perfeftly').
constexpr float kLandSeconds = 1.18f;

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
            restDegrees_ = at.pitch * 180.0f / kPi;
        } else if (name == "Object10" || name == "Object11") {
            deck_.push_back(i);
        }
    }
    if (door_ >= 0) {
        core::logf("drawbridge: Object37 raised at %.0f degrees, %zu deck pieces held back",
                   double(restDegrees_), deck_.size());
    }
}

void Drawbridge::shutdown() {
    state_ = State::Raised;
    door_ = -1;
    deck_.clear();
    elapsed_ = 0.0f;
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
        // The sound starts with the fall, on this frame (World::update plays it).
        state_ = State::Falling;
        started_ = true;
        elapsed_ = 0.0f;
    } else {
        elapsed_ += seconds;
    }
    // Ours, timed to the sound: from where it stands to flat as a body falls, slow while the
    // chain lets it out and fastest as it strikes, landing on the thud and staying there (the
    // user: 'gate openiing has to happen without bounce'). MU's swing -- from 35 by a speed
    // growing 1.5 a frame, knocked back past 90 -- lands in 0.36 s, mid-rattle.
    if (elapsed_ >= kLandSeconds) {
        landed_ = true;
        lower(town);
        return;
    }
    const float t = elapsed_ / kLandSeconds;
    const float drawn = restDegrees_ + (90.0f - restDegrees_) * t * t;
    town.posePlacement(uint32_t(door_), drawn * kPi / 180.0f, yaw_, roll_, rest_);
}

}  // namespace mu::game
