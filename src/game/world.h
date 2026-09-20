// A world raised: the land, what stands on it, and MU's own camera looking at both.
#pragma once

#include <string>

#include "content/ground.h"
#include "content/texture.h"
#include "game/town.h"
#include "gfx/renderer.h"

namespace mu::game {

class World {
public:
    bool open(const std::string& assetDir, const std::string& name, content::Textures& textures);
    void shutdown();

    // `seconds` moves the focus so the camera is not still: sprint 1 ran --still throughout
    // and a shadow that crawls with the camera cannot be seen from a fixed one.
    void update(double seconds, bool still);

    const gfx::Camera& camera() const { return camera_; }
    const content::Ground& ground() const { return ground_; }
    Town& town() { return town_; }
    const Town& town() const { return town_; }

    // Where the camera looks, in tiles. Set from --at, else the map's own middle.
    void setFocusTile(float column, float row);

private:
    // A tile to the centre of that tile in world metres, through the map's own scale.
    void tileToMetres(float column, float row, float* x, float* z) const;

    content::Ground ground_;
    Town town_;
    gfx::Camera camera_;
    float focusColumn_ = 0.0f;
    float focusRow_ = 0.0f;
    bool focusSet_ = false;
};

}  // namespace mu::game
