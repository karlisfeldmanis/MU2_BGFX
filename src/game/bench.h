// The benches. No editor UI: a bench draws one thing under the game's own light, reloads
// its sheet when it changes, and is reviewed by its shot and its log. This is what replaced
// MU2's Godot studio for judging the picture.
#pragma once

#include <string>
#include <vector>

#include "content/ground.h"
#include "content/mesh.h"
#include "content/texture.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/renderer.h"

namespace mu::game {

class ModelBench {
public:
    // `modelPath` may be empty, in which case only the ground is raised. The world is raised
    // under it either way: a material is judged on the land it will live on, not on a plane.
    bool open(const std::string& assetDir, const std::string& world,
              const std::string& modelPath, content::Textures& textures);

    // The browser: every cooked mesh in the world's own directory and in the figures', in one
    // sorted list, stepped through with the arrow keys.
    //
    // It walks the COOKED files rather than the glb they came from, which is the whole point
    // of it. `--model` reads a glb through cgltf and proves the art; this reads exactly what
    // the game loads -- the .mum the cook wrote, its BC7 and BC5 textures, its mip chains and
    // its material factors -- so a fault introduced by the cook shows here and nowhere else.
    bool openBrowser(const std::string& assetDir, const std::string& world,
                     content::Textures& textures);
    // Moves `by` places and loads what it lands on, clamped to the list. Returns false only
    // if that file will not parse, having already said why.
    bool step(int by, content::Textures& textures);
    bool browsing() const { return !browse_.empty(); }
    // "17/105  House01.mum", for the log line once a second.
    std::string browseLine() const;
    // The list itself, for the viewer to put on the screen. Names only -- the directory a
    // model came out of is in the log and is not what anybody reads off a list.
    size_t browseCount() const { return browse_.size(); }
    size_t browseIndex() const { return browseAt_; }
    std::string browseName(size_t index) const;

    // The ground the renderer should draw, or null when this bench is standing its model on
    // its own plane instead. See makeGround.
    const content::Ground* ground() const { return haveWorldGround_ ? &worldGround_ : nullptr; }

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

    // The hand on the camera. A drag turns it and the wheel pulls it in and out; both are
    // remembered across a step of the browser, so walking the list keeps the angle you chose
    // instead of snapping back to the default view on every model.
    void orbit(float dYawPixels, float dPitchPixels);
    void zoom(float notches);

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
    void setDistance(float distance) {
        distance_ = distance;
        wantsFixedDistance_ = distance > 0.0f;
    }
    float distance() const { return distance_; }

private:
    // The plot's size and which of the world's surfaces it wears. 24 tiles is wide enough
    // that the land fills a shot behind anything this bench holds and small enough to build
    // in no time at all. Surface 0 is Lorencia's meadow, TileGrass01 on both halves: one
    // material, evenly, so the ground behind a model is quiet and does not compete with it.
    // Surface 1 was tried and is grass against sand -- two materials and the bite between
    // them, which is the right plot for judging the GROUND and the wrong one for judging a
    // thing standing on it.
    static constexpr int kPlotTiles = 24;
    static constexpr int kPlotSurface = 0;

    bool makeGround(content::Textures& textures, float halfSize);
    // Lorencia's own land under the bench, which is what a material is finally judged against.
    // Falls back to makeGround's plane and says so when the world will not load.
    bool raiseWorldGround(const std::string& assetDir, const std::string& world,
                          content::Textures& textures);
    // Frames the camera and builds the draw list, once the mesh is in hand. Shared by open
    // and by every step of the browser.
    bool place(content::Textures& textures);
    void frameOn(float radius, const content::Bounds& bounds);

    content::Mesh model_;
    content::Mesh ground_;
    content::Ground worldGround_;
    bool haveWorldGround_ = false;
    // Where on the map the bench stands its subject, in metres. The middle of Lorencia's
    // paved square: flat, lit the way the town is lit, and the place a shot of a model is
    // worth comparing against a shot of the town.
    float stand_[3] = {0.0f, 0.0f, 0.0f};
    // How far above the land the subject hangs. See kFloatRadii.
    float lift_ = 0.0f;
    std::vector<std::string> browse_;   // absolute paths to .mum, sorted
    std::string browseDir_;             // what the paths are relative to, for the textures
    size_t browseAt_ = 0;
    bool haveModel_ = false;
    bool haveFigure_ = false;
    Figures figures_;
    Figure figure_;
    std::vector<float> scratch_;
    std::vector<gfx::Drawable> drawables_;
    gfx::Camera camera_;
    float distance_ = 0.0f;
    // Where the hand has put the camera, in radians, on top of the bench's own default.
    float yawOffset_ = 0.0f;
    float pitchOffset_ = 0.0f;
    // What the wheel has done to the framing distance, as a multiplier.
    float zoom_ = 1.0f;
    // --dist was given, so the browser must not re-frame on each model.
    bool wantsFixedDistance_ = false;
    float height_ = 0.0f;
    float focus_[3] = {0.0f, 0.0f, 0.0f};
};

}  // namespace mu::game
