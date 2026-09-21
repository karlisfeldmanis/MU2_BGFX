#include "game/world.h"

#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// MU's own camera, measured out of MU2's client/core/Walk.cs rather than chosen. The
// distance and the field of view set the framing together -- 8 metres back through a
// 74-degree lens is a far wider picture than 8 metres through a 55 -- so neither number
// means anything without the other, and both are the old client's.
constexpr float kFovDegrees = 55.0f;
constexpr float kPitchDegrees = -48.5f;
constexpr float kYawDegrees = 45.0f;
constexpr float kDistance = 8.0f;      // 800 of MU's units
// Where a played camera starts, and how near the wheel brings it. Invention: MU2's game had
// no zoom and stood at MU's 8 m, which on a 1080p frame draws the character 170 pixels tall.
// The measuring camera (no --play) stays at MU's 8 m, so every frame number keeps its meaning.
constexpr float kPlayDistance = 6.0f;
constexpr float kNearest = 3.5f;
constexpr float kFocusHeight = 1.5f;   // 150 units up the body

// The tile texture Lorencia floors its interiors with and uses nowhere outdoors: MuMain's
// HeroTile == 4, and MU2's World.IndoorTile.
constexpr int kIndoorFloor = 4;

// The played camera's follow, all three inventions -- MU's camera is nailed to the character.
// Nailed, it passes on every hitch in the drawn position, and it rises and falls with every
// bump in the ground under his feet, which on Lorencia's cobbles is a bob at walking pace. So
// a critically damped spring: no overshoot, no wobble, and it sets off and stops with the
// character instead of jerking. The times are how long it takes to close most of the gap --
// short across the ground, so he never drifts far off the middle of the frame (about 20 cm
// at a walk), and longer up and down, where there is nothing to catch up with but a bump.
constexpr float kFollowSeconds = 0.09f;
constexpr float kRiseSeconds = 0.25f;
// A jump further than this is a respawn or a gate, and the camera is simply there.
constexpr float kSnapMetres = 4.0f;
// The wheel eases to the distance it asked for, in log space so a notch reads the same near
// or far. About a sixth of a second to arrive.
constexpr float kZoomSeconds = 0.06f;

// Game Programming Gems 4's SmoothDamp, the closed form of a critically damped spring.
float smoothDamp(float current, float target, float& velocity, float time, float dt) {
    const float omega = 2.0f / time;
    const float x = omega * dt;
    const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = current - target;
    const float temp = (velocity + omega * change) * dt;
    velocity = (velocity - omega * temp) * decay;
    return target + (change + temp) * decay;
}

}  // namespace

void World::tileToMetres(float column, float row, float* x, float* z) const {
    // A tile's CENTRE, and in the world's own metres per tile. docs/conventions.md states
    // the centre as (column + 0.5, -(row + 0.5)); this dropped the half and hard-coded one
    // metre to the tile, so the camera stood on the corner of the tile it named and any map
    // whose units_per_tile is not 100 would have put it somewhere else entirely -- the very
    // division Ground::heightAt already reads from the world's json rather than assuming.
    const float metresPerTile = ground_.metresPerTile();
    *x = (column + 0.5f) * metresPerTile;
    *z = -(row + 0.5f) * metresPerTile;
}

void World::setFocusTile(float column, float row) {
    focusColumn_ = column;
    focusRow_ = row;
    focusSet_ = true;
}

bool World::open(const std::string& assetDir, const std::string& name,
                 content::Textures& textures, int crowd, bool figures) {
    const std::string dir = core::join(assetDir, "world/" + name);
    if (!ground_.load(dir, name, textures)) return false;
    // The town is not required: the land is a world on its own, and a cook that has not been
    // run yet says so in the log rather than failing the launch.
    town_.open(assetDir, name, textures);
    // And what burns in it. Nothing without a town, since every light hangs on a placement.
    if (town_.isOpen()) lamps_.open(assetDir, town_, ground_, textures);
    if (town_.isOpen()) sway_.open(assetDir, name, town_);
    // And what rides the swaying bones: the fountain's spray, the lanterns.
    if (town_.isOpen()) ornaments_.open(assetDir, town_, textures);

    if (!focusSet_) {
        // Lorencia's safe zone is around tile 142,126 -- the middle of the town rather than
        // the middle of the map, which is sea and empty grass.
        focusColumn_ = 142.0f;
        focusRow_ = 126.0f;
    } else {
        // A camera off the map fails the run rather than drawing the empty frame it would
        // otherwise draw. `--at 9999,9999` exited 0, showed nothing, and reported a *better*
        // frame time than any real camera can -- a supported way to publish a good number for
        // a picture of nothing. The map's own size is the thing to say in the message,
        // because it is what the caller has to know to fix the command.
        const float size = float(ground_.size());
        if (focusColumn_ < 0.0f || focusRow_ < 0.0f || focusColumn_ >= size ||
            focusRow_ >= size) {
            core::logError("--at %.0f,%.0f is off %s: the map is %d tiles a side, so a column "
                           "and a row run 0 to %d",
                           focusColumn_, focusRow_, name.c_str(), ground_.size(),
                           ground_.size() - 1);
            return false;
        }
    }
    // The figures are not required either, for the same reason the town is not: a cook that
    // has not been run says so in the log rather than failing the launch.
    if (figures && figures_.open(assetDir, name, textures)) {
        int monsters = crowd;
        if (monsters < 0) {
            monsters = 0;
            for (const Breed& breed : figures_.breeds()) monsters += breed.total;
        }
        crowd_.open(figures_, ground_, monsters, focusColumn_, focusRow_);
    }

    camera_.fovDegrees = kFovDegrees;
    camera_.nearPlane = 0.05f;
    camera_.farPlane = 1200.0f;
    float focusX = 0.0f, focusZ = 0.0f;
    tileToMetres(focusColumn_, focusRow_, &focusX, &focusZ);
    core::logf("world %s: looking at tile %.0f,%.0f, ground %.2f m up", name.c_str(),
               focusColumn_, focusRow_, ground_.heightAt(focusX, focusZ));
    return true;
}

bool World::play(const std::string& assetDir, const std::string& name, uint64_t seed, int kin,
                 int level, const std::string& weapon, const std::string& shield) {
    // What the character looks like, built here rather than taken off a cooked row: the naked
    // class body -- which is what a character IS before he has picked anything up -- holding
    // whatever the sim is about to be told he holds. mu.db's own class enumeration, which is
    // what `--kin` speaks: 0 Dark Wizard, 1 Fairy Elf, 2 Dark Knight.
    //
    // `FairyElf` is the elf's bare body and is named without the suffix the other two carry;
    // that is index.json's spelling and not a slip.
    const char* bare = kin == 0 ? "DarkWizardBare" : (kin == 1 ? "FairyElf" : "DarkKnightBare");
    const FigureBody* look = figures_.dress("Hero", bare, weapon, shield);
    // The character is put down where the camera was told to look, which is the town by
    // default and `--at` otherwise. The realm moves him to the nearest tile he may stand on.
    if (!play_.open(assetDir, name, &ground_, &figures_, seed, kin, level, int(focusColumn_),
                    int(focusRow_), weapon, shield, look, bare)) {
        return false;
    }
    // And the crowd stands down: the same monsters would otherwise be drawn twice, once where
    // the sim has them and once where the crowd chose to put them.
    crowd_.shutdown();
    return true;
}

void World::update(double seconds, bool still) {
    // A world being played follows its character rather than the sine walk below: the camera
    // is MU's, fixed over the man, and where he is standing is the sim's answer and not the
    // frame's.
    if (play_.isOpen()) {
        play_.focus(&focusColumn_, &focusRow_);
        still = true;
    }
    // A slow walk across the town when the camera is not held. The point is not the walk:
    // it is that a still camera cannot show a shadow edge crawling, which is the one defect
    // sprint 1's bench was structurally unable to catch.
    float column = focusColumn_;
    float row = focusRow_;
    if (!still) {
        column += float(std::sin(seconds * 0.15) * 12.0);
        row += float(std::cos(seconds * 0.11) * 12.0);
    }

    // Column is +x and row is -z. docs/conventions.md.
    float x = 0.0f, z = 0.0f;
    tileToMetres(column, row, &x, &z);
    float groundY = ground_.heightAt(x, z);
    // The roofs, on the feet the camera is framing -- the character's own, not the eased
    // point, so a roof lifts the frame he steps under it.
    const float feetX = x, feetZ = z;

    // The frame's own seconds, which this is not handed: `seconds` is the run's clock.
    const float dt = lastSeconds_ < 0.0 ? 0.0f
                                         : float(std::fmin(0.1, std::fmax(0.0, seconds - lastSeconds_)));
    lastSeconds_ = seconds;
    if (play_.isOpen()) {
        const float jump = std::hypot(x - eased_[0], z - eased_[2]);
        if (!easedSet_ || jump > kSnapMetres || dt <= 0.0f) {
            eased_[0] = x;
            eased_[1] = groundY;
            eased_[2] = z;
            easing_[0] = easing_[1] = easing_[2] = 0.0f;
            easedSet_ = true;
        } else {
            eased_[0] = smoothDamp(eased_[0], x, easing_[0], kFollowSeconds, dt);
            eased_[1] = smoothDamp(eased_[1], groundY, easing_[1], kRiseSeconds, dt);
            eased_[2] = smoothDamp(eased_[2], z, easing_[2], kFollowSeconds, dt);
        }
        x = eased_[0];
        groundY = eased_[1];
        z = eased_[2];
    }

    const float pitch = kPitchDegrees * 3.14159265f / 180.0f;
    const float yaw = kYawDegrees * 3.14159265f / 180.0f;

    // The roofs, on the feet the camera is framing: the character when one is played, the
    // focus when not, so `--at` inside a house shows the room as walking into it would.
    town_.setRoofsHidden(indoors(feetX, feetZ));

    camera_.target[0] = x;
    camera_.target[1] = groundY + kFocusHeight;
    camera_.target[2] = z;

    // Walk.cs's own back vector: sin(yaw)cos(pitch), -sin(pitch), cos(yaw)cos(pitch).
    const float back[3] = {std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                           std::cos(yaw) * std::cos(pitch)};
    if (distance_ <= 0.0f) distance_ = wantDistance_ = play_.isOpen() ? kPlayDistance : kDistance;
    if (wantDistance_ <= 0.0f) wantDistance_ = distance_;
    if (dt > 0.0f && distance_ != wantDistance_) {
        const float blend = 1.0f - std::exp(-dt / kZoomSeconds);
        distance_ = std::exp(std::log(distance_) + (std::log(wantDistance_) - std::log(distance_)) * blend);
        if (std::fabs(distance_ - wantDistance_) < 1e-3f) distance_ = wantDistance_;
    }
    for (int i = 0; i < 3; ++i) camera_.position[i] = camera_.target[i] + back[i] * distance_;
}

void World::zoom(float notches) {
    if (notches == 0.0f || distance_ <= 0.0f) return;
    // From where the wheel was last sent, not from where the camera has got to, so a quick
    // spin of several notches adds up rather than being eaten by the easing.
    if (wantDistance_ <= 0.0f) wantDistance_ = distance_;
    const float wanted = wantDistance_ * std::pow(0.92f, notches);
    wantDistance_ = wanted < kNearest ? kNearest : (wanted > kDistance ? kDistance : wanted);
}

bool World::characterAt(float* x, float* z) const {
    if (!play_.isOpen()) return false;
    float column = focusColumn_, row = focusRow_;
    play_.focus(&column, &row);
    tileToMetres(column, row, x, z);
    return true;
}

bool World::indoors(float x, float z) const {
    // Column is +x and row is -z. docs/conventions.md.
    const float metresPerTile = ground_.metresPerTile();
    const int column = int(std::floor(x / metresPerTile));
    const int row = int(std::floor(-z / metresPerTile));
    return ground_.floorAt(column, row) == kIndoorFloor;
}

void World::shutdown() {
    play_.shutdown();
    crowd_.shutdown();
    figures_.shutdown();
    lamps_.shutdown();
    sway_.shutdown();
    ornaments_.shutdown();
    town_.shutdown();
    ground_.shutdown();
}

}  // namespace mu::game
