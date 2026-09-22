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
#include "game/world/lamps.h"
#include "game/world/sway.h"
#include "game/world/town.h"
#include "gfx/renderer.h"

namespace mu::game {

// One thing the viewer can show. Either a cooked mesh on disk -- a world object or a figure's
// worn part, drawn as it sits -- or a whole body out of the figure tables, which stands with
// its gear on and plays a clip. The two are one list on purpose: what a person browsing
// wants is a name, and whether that name is a file or a table row is the viewer's problem.
struct BrowseEntry {
    std::string name;                  // what the list shows
    std::string path;                  // a .mum on disk, for the mesh categories
    const FigureBody* body = nullptr;  // a body out of the tables, for the figure categories
};

// A tab of the viewer: the label on it, what is in it, and where the eye was left the last
// time it was open. The index is per category rather than shared, so coming back to the
// monsters lands on the monster you were looking at and not on the 300th house.
struct BrowseCategory {
    std::string label;
    std::vector<BrowseEntry> entries;
    size_t at = 0;
};

class ModelBench {
public:
    // `modelPath` may be empty, in which case only the ground is raised. The world is raised
    // under it either way: a material is judged on the land it will live on, not on a plane.
    bool open(const std::string& assetDir, const std::string& world,
              const std::string& modelPath, content::Textures& textures);

    // The browser: everything the cook wrote, cut into categories -- the world's own objects,
    // the monsters, the people, and the figures' loose parts -- and stepped through with the
    // arrow keys, one category at a time.
    //
    // It walks the COOKED files rather than the glb they came from, which is the whole point
    // of it. `--model` reads a glb through cgltf and proves the art; this reads exactly what
    // the game loads -- the .mum the cook wrote, its BC7 and BC5 textures, its mip chains and
    // its material factors -- so a fault introduced by the cook shows here and nowhere else.
    //
    // The figure categories go further and load a body the way the game does: its parts on
    // one rig, its weapon in its hand, standing on the land and playing its own idle. A
    // monster looked at in bind pose is not the monster the game draws.
    bool openBrowser(const std::string& assetDir, const std::string& world,
                     content::Textures& textures);
    // The viewer's stage: the browser on its plot, with a few of the world's own placements
    // stood round the subject -- a bonfire on its right, a street lamp on its left, a house
    // wall and a railing behind it -- and the world's own lamps built from them. So a thing is
    // judged with a fire's light and flicker on it, with the town's objects in its reflection,
    // under MU's camera, at noon, dusk or night: every system the game draws a thing with.
    // `./viewer.sh`, which passes --stage.
    bool openStage(const std::string& assetDir, const std::string& world,
                   content::Textures& textures);
    // The studio: the browser standing its subject in the world the game draws -- the town
    // round it, its ground and baked light, its lamps and fires, the probe taken there as the
    // game takes it -- beside one of the map's own bonfires, which `fire` names. Nothing is
    // stood up by the bench itself: the caller owns the world and draws it, and gathers the
    // subject from here. `./tools/studio.py`, which passes --studio.
    bool openStudio(const std::string& assetDir, const std::string& world,
                    const float stand[3], const float fire[3], content::Textures& textures);
    bool inStudio() const { return studio_; }
    // The turntable: where the camera stands round the subject, in degrees on top of the
    // bench's own rest angle. The studio's sweep sets it; a drag still adds to it.
    void setTurn(float degrees) { turn_ = degrees * 3.14159265f / 180.0f; }
    bool hasStage() const { return stageTown_.isOpen(); }
    Town& stageTown() { return stageTown_; }
    Lamps& stageLamps() { return stageLamps_; }
    Sway& stageSway() { return stageSway_; }
    // Moves `by` places within the open category and loads what it lands on, clamped to the
    // list. Returns false only if that entry will not load, having already said why.
    bool step(int by, content::Textures& textures);
    // Opens another category and loads whatever it was left on. An empty one is refused --
    // a world with no cooked figures would otherwise offer two tabs with nothing behind them.
    bool setCategory(size_t category, content::Textures& textures);
    // The same two by name, for a run with nobody at the keyboard: the category whose label
    // holds `word`, and then the first entry whose name holds `needle`, both ignoring case.
    // Each says so in the log when it finds nothing, and leaves the browser where it was.
    bool openCategory(const std::string& word, content::Textures& textures);
    bool pick(const std::string& needle, content::Textures& textures);
    // The next or previous clip of whatever figure is standing, for the figure categories.
    // Does nothing where there is no figure or no library.
    bool stepClip(int by);
    bool browsing() const { return !categories_.empty(); }
    // "MONSTERS  17/105  Budge Dragon", for the log line once a second.
    std::string browseLine() const;
    // The list itself, for the viewer to put on the screen. Names only -- the directory a
    // model came out of is in the log and is not what anybody reads off a list.
    size_t browseCount() const;
    size_t browseIndex() const;
    std::string browseName(size_t index) const;
    size_t categoryCount() const { return categories_.size(); }
    size_t categoryIndex() const { return category_; }
    const BrowseCategory& category(size_t index) const { return categories_[index]; }

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
    // The subject alone, without the stage round it.
    const std::vector<gfx::Drawable>& gatherSubject(gfx::Renderer& renderer);
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
    // 40 and not 24: 24 was wide enough while the subject hung in the air over the middle of
    // it, and is not wide enough now it stands on it. A house is eight metres across and the
    // camera pulls back to about eleven to frame it, which put the far edge of the plot --
    // and the black nothing past it -- in the corner of every shot of a large object.
    static constexpr int kPlotTiles = 40;
    static constexpr int kPlotSurface = 0;

    bool makeGround(content::Textures& textures, float halfSize);
    // The categories, once the ground under them is settled. Shared by both openers.
    bool fillBrowser(const std::string& assetDir, const std::string& world,
                     content::Textures& textures);
    // In a world, the camera is MU's: framed from 1.5 m above the feet at 8 m, pulled back only
    // for a thing too big to fit. See world.cpp for where those numbers come from.
    void frameAsGame(float radius);
    // Loads whatever the open category is pointing at: a cooked mesh, or a body stood up on
    // the land with its clip running.
    bool loadCurrent(content::Textures& textures);
    bool standFigure(const FigureBody* body);
    // Lorencia's own land under the bench, which is what a material is finally judged against.
    // Falls back to makeGround's plane and says so when the world will not load.
    bool raiseWorldGround(const std::string& assetDir, const std::string& world,
                          content::Textures& textures);
    // Frames the camera and builds the draw list, once the mesh is in hand. Shared by open
    // and by every step of the browser.
    bool place(content::Textures& textures);
    void frameOn(float radius, const content::Bounds& bounds);
    float framingDistance(float radius) const;

    content::Mesh model_;
    content::Mesh ground_;
    content::Ground worldGround_;
    bool haveWorldGround_ = false;
    // The stage round the subject, when there is one, and the list gather() hands back then:
    // the stage's placements, then the subject kept out of the reflection probe.
    Town stageTown_;
    Lamps stageLamps_;
    Sway stageSway_;
    std::vector<gfx::Drawable> staged_;
    // MU's camera rather than the bench's, on the stage.
    bool gameFrame_ = false;
    // Where on the map the bench stands its subject, in metres. The middle of Lorencia's
    // paved square: flat, lit the way the town is lit, and the place a shot of a model is
    // worth comparing against a shot of the town.
    float stand_[3] = {0.0f, 0.0f, 0.0f};
    // How far above the land the subject hangs. See kFloatRadii.
    float lift_ = 0.0f;
    std::vector<BrowseCategory> categories_;
    size_t category_ = 0;
    std::string browseDir_;             // what the paths are relative to, for the textures
    // How many of drawables_ were made once and belong to no subject: the fallback plane, and
    // nothing else. A figure's parts are appended after them every frame and truncated back
    // to this on the next -- which is why it is a count and not a bool. It was `resize(1)`,
    // and a figure standing on the world's own land has no plane in the list at all, so that
    // kept the first of the previous frame's parts and dropped the ground it was standing on.
    size_t fixed_ = 0;
    bool haveModel_ = false;
    bool haveFigure_ = false;
    // A weapon is shown in the hand that holds it -- the grip and the stance are the pose --
    // but the body holding it is not drawn, and the camera follows the weapon rather than
    // the figure: a sword judged past a shoulder is judged against the shoulder.
    bool bearerHidden_ = false;
    // The studio's, and where a fire stands that a weapon's framing keeps in the picture: the
    // stage's bonfire, or the map's own in the studio.
    bool studio_ = false;
    bool haveFire_ = false;
    float fire_[3] = {0.0f, 0.0f, 0.0f};
    float turn_ = 0.0f;
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
