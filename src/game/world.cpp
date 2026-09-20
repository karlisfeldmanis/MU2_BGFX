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
constexpr float kFocusHeight = 1.5f;   // 150 units up the body

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

void World::update(double seconds, bool still) {
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
    const float groundY = ground_.heightAt(x, z);

    const float pitch = kPitchDegrees * 3.14159265f / 180.0f;
    const float yaw = kYawDegrees * 3.14159265f / 180.0f;

    camera_.target[0] = x;
    camera_.target[1] = groundY + kFocusHeight;
    camera_.target[2] = z;

    // Walk.cs's own back vector: sin(yaw)cos(pitch), -sin(pitch), cos(yaw)cos(pitch).
    const float back[3] = {std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                           std::cos(yaw) * std::cos(pitch)};
    for (int i = 0; i < 3; ++i) camera_.position[i] = camera_.target[i] + back[i] * kDistance;
}

void World::shutdown() {
    crowd_.shutdown();
    figures_.shutdown();
    town_.shutdown();
    ground_.shutdown();
}

}  // namespace mu::game
