// The benches. No editor UI: a bench draws one thing under the game's own light, reloads
// its sheet when it changes, and is reviewed by its shot and its log. This is what replaced
// MU2's Godot studio for judging the picture.
#pragma once

#include <string>
#include <vector>

#include "content/mesh.h"
#include "content/texture.h"
#include "gfx/renderer.h"

namespace mu::game {

class ModelBench {
public:
    // `modelPath` may be empty, in which case only the ground is raised.
    bool open(const std::string& modelPath, content::Textures& textures);
    void shutdown();

    // Turns the camera around the subject. `seconds` is the time since the bench opened.
    void update(double seconds, bool spin);

    const gfx::Camera& camera() const { return camera_; }
    const std::vector<gfx::Drawable>& drawables() const { return drawables_; }

    // How far away the camera sits, in world units. Set from --dist, else framed on the
    // model's own radius.
    void setDistance(float distance) { distance_ = distance; }
    float distance() const { return distance_; }

private:
    bool makeGround(content::Textures& textures, float halfSize);

    content::Mesh model_;
    content::Mesh ground_;
    bool haveModel_ = false;
    std::vector<gfx::Drawable> drawables_;
    gfx::Camera camera_;
    float distance_ = 0.0f;
    float height_ = 0.0f;
    float focus_[3] = {0.0f, 0.0f, 0.0f};
};

}  // namespace mu::game
