// The benches. No editor UI: a bench draws one thing under the game's own light, reloads
// its sheet when it changes, and is reviewed by its shot and its log. This is what replaced
// MU2's Godot studio for judging the picture.
#pragma once

#include <string>
#include <vector>

#include "content/mesh.h"
#include "content/texture.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/renderer.h"

namespace mu::game {

class ModelBench {
public:
    // `modelPath` may be empty, in which case only the ground is raised.
    bool open(const std::string& modelPath, content::Textures& textures);

    // The monster bench: one figure on the same ground, under the game's own light, playing
    // one clip named out of the right table of the two. `clip` is MU's own action number, or
    // -1 for the breed's idle.
    bool openFigure(const std::string& assetDir, const std::string& world,
                    const std::string& name, int clip, bool safe,
                    content::Textures& textures);
    void shutdown();

    // Turns the camera around the subject. `seconds` is the time since the bench opened and
    // `delta` the frame's own, which is what a clip is advanced by.
    void update(double seconds, double delta, bool spin);

    const gfx::Camera& camera() const { return camera_; }
    // The ground and whatever stands on it. A figure is posed here rather than at open,
    // because its pose is a frame's worth of work and takes a palette row of the renderer.
    const std::vector<gfx::Drawable>& gather(gfx::Renderer& renderer);
    bool hasFigure() const { return haveFigure_; }
    const Figure& figure() const { return figure_; }
    // What the bench prints once a second: which clip, where its clock stands, and how long
    // it is. A clip that froze on its first frame reports the same name as one that runs.
    std::string clipLine() const;

    // How far away the camera sits, in world units. Set from --dist, else framed on the
    // model's own radius.
    void setDistance(float distance) { distance_ = distance; }
    float distance() const { return distance_; }

private:
    bool makeGround(content::Textures& textures, float halfSize);

    content::Mesh model_;
    content::Mesh ground_;
    bool haveModel_ = false;
    bool haveFigure_ = false;
    Figures figures_;
    Figure figure_;
    std::vector<float> scratch_;
    std::vector<gfx::Drawable> drawables_;
    gfx::Camera camera_;
    float distance_ = 0.0f;
    float height_ = 0.0f;
    float focus_[3] = {0.0f, 0.0f, 0.0f};
};

}  // namespace mu::game
