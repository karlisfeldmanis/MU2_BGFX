#include "game/bench.h"

#include <bx/math.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "content/cooked.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {

bool ModelBench::makeGround(content::Textures& textures, float halfSize) {
    // The fallback plane, for when the world will not load. It was the bench's only ground
    // and the default until the land was put under it; see raiseWorldGround for why that was
    // wrong. Kept because a bench that cannot raise Lorencia should still draw something and
    // say so, rather than show nothing and look like a broken renderer.
    std::vector<content::Vertex> vertices(4);
    const float uvTiles = halfSize / 100.0f;
    const float xs[4] = {-halfSize, halfSize, halfSize, -halfSize};
    const float zs[4] = {-halfSize, -halfSize, halfSize, halfSize};
    const float us[4] = {0.0f, uvTiles, uvTiles, 0.0f};
    const float vs[4] = {0.0f, 0.0f, uvTiles, uvTiles};
    for (int i = 0; i < 4; ++i) {
        vertices[size_t(i)] = content::Vertex{{xs[i], 0.0f, zs[i]},
                                              {0.0f, 1.0f, 0.0f},
                                              {1.0f, 0.0f, 0.0f, 1.0f},
                                              {us[i], vs[i]}};
    }
    // Wound so the right-hand rule gives (0, +1, 0), matching the vertex normals above and
    // glTF's counter-clockwise front face. It was briefly flipped to work around a plane
    // that would not appear; the plane was fine and the projection was left-handed. Every
    // material in MU2's build is double sided, so this is the one surface here whose winding
    // anything checks at all.
    std::vector<uint32_t> indices = {0, 2, 1, 0, 3, 2};

    content::Material material;
    material.name = "bench ground";
    material.albedo = textures.white();
    material.normal = textures.flatNormal();
    // Not white. White's blue is metal 1.0, and with no diffuse a metal surface returns
    // only what it reflects -- so this plane was a fully rough white *metal*, which looks
    // plausible enough in a shot to survive three reviews. neutralOrm is occlusion 1,
    // roughness 1, metal 0.
    material.orm = textures.neutralOrm();
    material.emissive = textures.black();

    content::Part part;
    part.firstIndex = 0;
    part.indexCount = 6;
    part.material = 0;

    return ground_.build("bench ground", std::move(vertices), std::move(indices), {material},
                         {part});
}

bool ModelBench::raiseWorldGround(const std::string& assetDir, const std::string& world,
                                  content::Textures& textures) {
    // The bench's plane used to be the only ground here, and the comment above makeGround
    // argued for it: untextured, so nothing argues with the model. That was the wrong trade
    // and it was costing the thing the bench exists for. A white plane at albedo 1.0 under a
    // sun at x3 is brighter and flatter than any ground in the game, so every material read
    // lighter on the bench than it does in the town, and MU's baked terrain light -- which
    // every placement in Lorencia carries and which is half of how the town looks -- was not
    // on the model at all. A material judged there was judged somewhere the game never goes.
    const std::string dir = core::join(core::join(assetDir, "world"), world);
    // A plot rather than the map. Raising Lorencia put the subject on real ground but cost
    // the whole 256-square world -- its height, attribute and light grids and its 27 sheets
    // -- to stand one candle on it, and pinned every shot to whatever tile the bench picked.
    // buildPlot keeps what mattered (the real ground shader and a real surface pair out of
    // the same ground_surfaces.json) and drops what did not.
    if (!worldGround_.buildPlot(dir, world, kPlotTiles, kPlotSurface, textures)) {
        core::logError("the bench could not build its plot from %s; standing on its own plane "
                       "instead, which is brighter and flatter than any ground in the game "
                       "and is not what a material should be judged on", world.c_str());
        return false;
    }
    haveWorldGround_ = true;
    // Where the model stands. The middle of the map in tiles, moved to the nearest place the
    // land is walkable-flat, is not worth the machinery: Lorencia's centre is its paved
    // square, which is flat, and that is the shot worth comparing against the town's own.
    // NEGATIVE z, and this was +z for one run: the column is +x and the row is -z, so the map
    // lies over x in [0, size] and z in [-size, 0]. Standing the model at +128 put it off the
    // map, where heightAt answers 0 by its out-of-bounds rule rather than by measuring, so the
    // subject sat below a terrain it was not on and the camera looked at its underside --
    // which, the land being the one single-sided surface in the engine, is nothing at all.
    // A black shot with the model floating in it, and no error anywhere. See
    // docs/conventions.md and Ground::heightAt.
    const float middle = float(worldGround_.size()) * 0.5f * worldGround_.metresPerTile();
    stand_[0] = middle;
    stand_[2] = -middle;
    stand_[1] = worldGround_.heightAt(stand_[0], stand_[2]);
    core::logf("bench ground: %s surfaces, standing at %.0f,%.0f m, %.2f m up", world.c_str(),
               stand_[0], stand_[2], stand_[1]);
    return true;
}

// How far back the camera has to sit for a ball of that radius to fit the frame, out of the
// camera's OWN field of view rather than out of a number that once looked right. It was
// `radius * 3`, which is what a 37-degree fov would ask for; this camera is 30, and every
// subject taller than it was wide came out with its feet cut off -- the Dark Knight with his
// shield stood a head above the top of the shot and half a boot below the bottom of it. The
// margin is a tenth, so a thing whose bounds are a little optimistic still has air around it.
float ModelBench::framingDistance(float radius) const {
    const float halfFov = camera_.fovDegrees * 0.5f * 3.14159265f / 180.0f;
    return radius / std::sin(halfFov) * 1.1f;
}

// The subject stands ON the land, and this used to float it clear of it by 2.2 of its own
// radii. The argument for floating was that a viewer is for turning a thing over and looking
// at its underside; the argument against it, which is the user's and is the one kept, is that
// a house hanging in the air over a town is not the object the game draws. What a world
// object looks like is inseparable from where it meets the earth -- its contact shadow, the
// occlusion in the gap, the baked light climbing its base -- and all three are absent on a
// floating one. The wheel and the drag reach the underside of anything worth seeing.
void ModelBench::frameOn(float radius, const content::Bounds& bounds) {
    lift_ = 0.0f;
    height_ = bounds.max[1] - bounds.min[1];
    // The middle of the thing's own height above where its feet are, which is the point a
    // turn about it keeps still. Its bounds' CENTRE is not used for the height: `place` puts
    // the model's minimum y on the land, so the centre of the box is half a height up from
    // there whatever the model's own origin happens to be.
    focus_[0] = stand_[0];
    focus_[1] = stand_[1] + height_ * 0.5f;
    focus_[2] = stand_[2];
    if (distance_ <= 0.0f) distance_ = framingDistance(radius);
}

bool ModelBench::open(const std::string& assetDir, const std::string& world,
                      const std::string& modelPath, content::Textures& textures) {
    if (!modelPath.empty()) {
        haveModel_ = model_.load(modelPath, textures);
        if (!haveModel_) return false;
    }
    raiseWorldGround(assetDir, world, textures);
    return place(textures);
}

// Everything after the mesh is in hand, shared by open and by the browser's every step.
bool ModelBench::place(content::Textures& textures) {
    float radius = 100.0f;
    if (haveModel_) {
        const content::Bounds& b = model_.bounds();
        radius = b.radius > 0.0f ? b.radius : 100.0f;
        // Stood on the ground rather than centred above it: what this bench is for is the
        // contact -- the shadow under it and the occlusion where it meets the land.
        frameOn(radius, b);
    } else if (distance_ <= 0.0f) {
        distance_ = framingDistance(radius);
    }

    drawables_.clear();
    if (!haveWorldGround_) {
        if (!makeGround(textures, std::max(radius * 8.0f, 400.0f))) return false;
        gfx::Drawable groundDraw;
        groundDraw.mesh = &ground_;
        bx::mtxIdentity(groundDraw.transform);
        drawables_.push_back(groundDraw);
    }
    fixed_ = drawables_.size();

    if (haveModel_) {
        gfx::Drawable modelDraw;
        modelDraw.mesh = &model_;
        // Stood on the land: its own minimum y goes to the ground's height here, and its
        // centre over the spot in x and z. The minimum rather than the centre for the
        // vertical, so a model authored about its feet and one authored about its middle
        // both stand rather than one of them sinking half its height into the earth.
        const content::Bounds& b = model_.bounds();
        bx::mtxTranslate(modelDraw.transform, stand_[0] - b.centre[0],
                         stand_[1] + lift_ - b.min[1], stand_[2] - b.centre[2]);
        drawables_.push_back(modelDraw);
    }

    core::logf("bench: %s, radius %.1f, camera at %.1f units", haveModel_ ? model_.name().c_str()
                                                                         : "the ground alone",
               radius, distance_);
    return true;
}

namespace {

// One directory of cooked meshes, as a category's worth of entries, sorted by the name that
// will be on the screen. A directory that is not there is not an error here: a world cooked
// without figures has no figures directory, and the category is then simply empty.
std::vector<BrowseEntry> meshesIn(const std::string& dir) {
    std::vector<BrowseEntry> found;
    std::error_code error;
    std::filesystem::directory_iterator walk(dir, error);
    if (error) {
        core::logf("browser: nothing cooked in %s", dir.c_str());
        return found;
    }
    for (const std::filesystem::directory_entry& entry : walk) {
        if (entry.path().extension() != ".mum") continue;
        BrowseEntry one;
        one.name = entry.path().stem().string();
        one.path = entry.path().string();
        found.push_back(one);
    }
    std::sort(found.begin(), found.end(), [](const BrowseEntry& a, const BrowseEntry& b) {
        return a.name < b.name;
    });
    return found;
}

std::vector<BrowseEntry> bodiesIn(const Figures& figures, BodyKind kind) {
    std::vector<BrowseEntry> found;
    for (const FigureBody* body : figures.bodiesOf(kind)) {
        BrowseEntry one;
        one.name = body->label.empty() ? body->name : body->label;
        one.body = body;
        found.push_back(one);
    }
    return found;
}

}  // namespace

bool ModelBench::openBrowser(const std::string& assetDir, const std::string& world,
                             content::Textures& textures) {
    browseDir_ = assetDir;
    const std::string cooked = core::join(assetDir, "cooked");
    // The land first, because everything in every category is going to stand on it.
    raiseWorldGround(assetDir, world, textures);

    // The figure tables, which are what makes a monster a monster here rather than a mesh in
    // bind pose. A world cooked without them is not an error: the mesh categories still fill
    // and the figure ones come out empty.
    figures_.open(assetDir, world, textures);
    // And the wardrobe on top of them, which is where the armour and the weapons come from.
    // It needs the bare class bodies the line above loaded, so it is second and not first.
    figures_.openWardrobe(assetDir, textures);

    categories_.clear();
    categories_.push_back({"World objects", meshesIn(core::join(core::join(cooked, world),
                                                                "meshes")), 0});
    categories_.push_back({"Monsters", bodiesIn(figures_, BodyKind::Monster), 0});
    categories_.push_back({"People", bodiesIn(figures_, BodyKind::Character), 0});
    // The townsfolk cooked whole -- a model with its own clips inside it -- go with the
    // people: they are people, and a fourth tab holding three names is chrome.
    for (BrowseEntry& one : bodiesIn(figures_, BodyKind::Townsfolk)) {
        categories_.back().entries.push_back(one);
    }
    std::sort(categories_.back().entries.begin(), categories_.back().entries.end(),
              [](const BrowseEntry& a, const BrowseEntry& b) { return a.name < b.name; });
    // The wardrobe's two: a suit worn by the class that may wear it, and a weapon held by
    // one, standing in the stance that weapon puts a body in. Neither is a list of meshes --
    // five plates laid on the grass is not a suit of armour and a sword on its side is not
    // how anybody holds one. The loose meshes are still a tab of their own, below.
    categories_.push_back({"Armour sets", bodiesIn(figures_, BodyKind::Armour), 0});
    categories_.push_back({"Weapons", bodiesIn(figures_, BodyKind::Weapon), 0});
    // And the figures' loose meshes, which is what this viewer showed before there were
    // categories: one armour plate, one helmet, one sword, as the cook wrote it. Still worth
    // a tab -- a part is where a material fault is read -- but not the first one.
    categories_.push_back({"Figure parts",
                           meshesIn(core::join(core::join(cooked, "figures"), "meshes")), 0});

    // An empty category stays in the list so the tabs do not shuffle about between worlds;
    // it simply cannot be opened.
    size_t total = 0;
    for (const BrowseCategory& one : categories_) total += one.entries.size();
    if (total == 0) {
        categories_.clear();
        core::logError("browser: nothing cooked under %s. Run tools/cook.py first: this "
                       "walks what the cook wrote, not the glb it was made from",
                       cooked.c_str());
        return false;
    }

    category_ = 0;
    while (category_ < categories_.size() && categories_[category_].entries.empty()) ++category_;
    for (const BrowseCategory& one : categories_) {
        core::logf("browser: %s, %zu entries", one.label.c_str(), one.entries.size());
    }
    core::logf("browser: %zu in all; left and right step one, up and down ten, tab changes "
               "category, [ and ] change the clip", total);
    return loadCurrent(textures);
}

size_t ModelBench::browseCount() const {
    return categories_.empty() ? 0 : categories_[category_].entries.size();
}

size_t ModelBench::browseIndex() const {
    return categories_.empty() ? 0 : categories_[category_].at;
}

bool ModelBench::setCategory(size_t category, content::Textures& textures) {
    if (category >= categories_.size() || categories_[category].entries.empty()) return false;
    if (category == category_) return true;
    category_ = category;
    return loadCurrent(textures);
}

namespace {

// Case-insensitive "does the haystack hold the needle". Small enough to write out; the point
// is that --category monsters and --pick budge are typed by a person and neither the labels
// nor the model names are in the case they would type.
bool holds(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    auto lower = [](unsigned char c) { return char(std::tolower(c)); };
    std::string a, b;
    a.reserve(haystack.size());
    b.reserve(needle.size());
    for (char c : haystack) a.push_back(lower((unsigned char)c));
    for (char c : needle) b.push_back(lower((unsigned char)c));
    return a.find(b) != std::string::npos;
}

}  // namespace

bool ModelBench::openCategory(const std::string& word, content::Textures& textures) {
    for (size_t i = 0; i < categories_.size(); ++i) {
        if (!holds(categories_[i].label, word)) continue;
        if (categories_[i].entries.empty()) {
            core::logError("browser: the %s category is empty in this world",
                           categories_[i].label.c_str());
            return false;
        }
        category_ = i;
        return loadCurrent(textures);
    }
    core::logError("browser: no category called %s; there are world, monsters, people, "
                   "armour, weapons and parts", word.c_str());
    return false;
}

bool ModelBench::pick(const std::string& needle, content::Textures& textures) {
    // The open category first, then every other one: --pick budge alone should find the Budge
    // Dragon without also being told which tab it is on, but --category monsters --pick bull
    // must not wander off into the world's objects looking for a bull.
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t c = 0; c < categories_.size(); ++c) {
            if ((pass == 0) != (c == category_)) continue;
            const BrowseCategory& one = categories_[c];
            for (size_t i = 0; i < one.entries.size(); ++i) {
                if (!holds(one.entries[i].name, needle)) continue;
                category_ = c;
                categories_[c].at = i;
                return loadCurrent(textures);
            }
        }
    }
    core::logError("browser: nothing called %s in any category", needle.c_str());
    return false;
}

bool ModelBench::step(int by, content::Textures& textures) {
    if (categories_.empty()) return false;
    BrowseCategory& open = categories_[category_];
    if (open.entries.empty()) return false;
    // Clamped rather than wrapped. A list this long is walked to look for something, and a
    // wrap at the end quietly puts you back at the start with nothing to say it happened.
    const long long wanted = (long long)open.at + by;
    const long long last = (long long)open.entries.size() - 1;
    open.at = size_t(wanted < 0 ? 0 : (wanted > last ? last : wanted));
    return loadCurrent(textures);
}

bool ModelBench::loadCurrent(content::Textures& textures) {
    if (categories_.empty()) return false;
    const BrowseCategory& open = categories_[category_];
    if (open.entries.empty()) return false;
    const BrowseEntry& entry = open.entries[open.at];

    // The camera is re-framed on every subject, so --dist is honoured once and then the
    // list's own sizes take over; a cannon and a candle cannot share one distance.
    if (!wantsFixedDistance_) distance_ = 0.0f;

    if (entry.body) return standFigure(entry.body);

    haveFigure_ = false;
    const std::vector<uint8_t> bytes = core::readFile(entry.path);
    content::CookedMesh cooked;
    std::string error;
    if (bytes.empty() || !content::parseCookedMesh(bytes, cooked, error)) {
        core::logError("browser: %s did not parse: %s", entry.path.c_str(),
                       bytes.empty() ? "unreadable" : error.c_str());
        haveModel_ = false;
        return false;
    }
    const std::string name = std::filesystem::path(entry.path).filename().string();
    haveModel_ = model_.buildFromCooked(cooked, name, browseDir_, textures);
    if (!haveModel_) return false;
    return place(textures);
}

bool ModelBench::standFigure(const FigureBody* body) {
    // A figure is stood the way the town stands one: its feet on the land at the bench's own
    // spot, its parts on one rig, whatever it carries in its hands, and its own idle running.
    // `safe` is false, so a character has his weapon DRAWN and in his hand rather than on his
    // back -- which is the pose somebody browsing a sword wants to see.
    haveModel_ = false;
    haveFigure_ = true;
    figure_.stand(body, stand_, 0.0f, body->scale, false);
    if (body->library && !body->library->clips.clips.empty()) {
        figure_.play(body->idleClip >= 0 ? body->idleClip : 0, true);
    } else {
        core::logf("browser: %s has no clips and stands in bind pose", body->name.c_str());
    }

    const float radius = body->radius * body->scale;
    const float height = body->height * body->scale;
    focus_[0] = stand_[0];
    focus_[1] = stand_[1] + height * 0.5f;
    focus_[2] = stand_[2];
    height_ = height;
    if (distance_ <= 0.0f) distance_ = framingDistance(radius);

    // The rig's own count, not the palette's: as game/crowd.cpp.
    scratch_.assign(size_t(gfx::Renderer::kMaxBones) * 12, 0.0f);
    drawables_.clear();
    if (!haveWorldGround_) {
        gfx::Drawable groundDraw;
        groundDraw.mesh = &ground_;
        bx::mtxIdentity(groundDraw.transform);
        drawables_.push_back(groundDraw);
    }
    fixed_ = drawables_.size();
    core::logf("browser: %s, %zu parts, %zu held, %zu bones, radius %.2f m, camera at %.2f m",
               body->label.c_str(), body->parts.size(), body->held.size(), body->boneCount(),
               radius, distance_);
    return true;
}

bool ModelBench::stepClip(int by) {
    if (!haveFigure_ || !figure_.body() || !figure_.body()->library) return false;
    const content::CookedClips& clips = figure_.body()->library->clips;
    if (clips.clips.empty()) return false;
    const long long count = (long long)clips.clips.size();
    // Wrapped, unlike the model list: a clip list is short, it is walked round rather than
    // searched, and running off the end of eight animations with nothing happening reads as
    // a broken key.
    long long wanted = (long long)figure_.clip() + by;
    wanted = ((wanted % count) + count) % count;
    figure_.play(int(wanted), true);
    core::logf("browser: %s", clipLine().c_str());
    return true;
}

std::string ModelBench::browseName(size_t index) const {
    if (categories_.empty()) return std::string();
    const BrowseCategory& open = categories_[category_];
    if (index >= open.entries.size()) return std::string();
    return open.entries[index].name;
}

std::string ModelBench::browseLine() const {
    if (categories_.empty()) return std::string();
    const BrowseCategory& open = categories_[category_];
    if (open.entries.empty()) return open.label + ": empty";
    char line[512];
    if (haveFigure_) {
        std::snprintf(line, sizeof(line), "%s  %zu/%zu  %s", open.label.c_str(), open.at + 1,
                      open.entries.size(), clipLine().c_str());
    } else {
        std::snprintf(line, sizeof(line), "%s  %zu/%zu  %s  (%zu parts, %zu materials)",
                      open.label.c_str(), open.at + 1, open.entries.size(),
                      open.entries[open.at].name.c_str(), model_.parts().size(),
                      model_.materials().size());
    }
    return std::string(line);
}

bool ModelBench::openFigure(const std::string& assetDir, const std::string& world,
                            const std::string& name, int clip, bool safe,
                            content::Textures& textures) {
    if (!figures_.open(assetDir, world, textures)) return false;
    const FigureBody* body = figures_.body(name);
    if (!body) {
        core::logError("no figure called %s; index.json's own names are what this takes",
                       name.c_str());
        return false;
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    figure_.stand(body, origin, 0.0f, body->scale, safe);
    // A clip asked for by MU's action number, out of the breed's OWN table: a monster's 4 is
    // its second swing and a player's 4 is "Stop sword", and both files call it action4.
    if (clip >= 0 && body->library) {
        const int found = body->library->find(clip);
        if (found < 0) {
            core::logError("%s has no clip %d; it has %zu", name.c_str(), clip,
                           body->library->clips.clips.size());
        } else {
            figure_.play(found, true);
        }
    }
    haveFigure_ = true;

    // Framed on the whole figure, not on its first part: a Dark Knight's parts[0] is his
    // helmet, and framing on that filled the shot with a chin.
    const float radius = body->radius * body->scale;
    focus_[0] = 0.0f;
    focus_[1] = body->height * body->scale * 0.5f;
    focus_[2] = 0.0f;
    if (distance_ <= 0.0f) distance_ = framingDistance(radius);
    if (!makeGround(textures, std::max(radius * 8.0f, 20.0f))) return false;

    scratch_.assign(size_t(128) * 12, 0.0f);   // as game/crowd.cpp: the rig's count, not the palette's
    drawables_.clear();
    gfx::Drawable groundDraw;
    groundDraw.mesh = &ground_;
    bx::mtxIdentity(groundDraw.transform);
    drawables_.push_back(groundDraw);
    fixed_ = drawables_.size();
    core::logf("bench: %s, %zu parts, %zu held, radius %.2f m, camera at %.2f m",
               body->label.c_str(), body->parts.size(), body->held.size(), radius, distance_);
    return true;
}

const std::vector<gfx::Drawable>& ModelBench::gather(gfx::Renderer& renderer) {
    if (!haveFigure_) return drawables_;
    // The fallback plane, where there is one, was made once; everything after it is this
    // frame's. On the world's own land there is no plane and fixed_ is zero.
    drawables_.resize(fixed_);
    const int bones = figure_.pose(scratch_.data());
    const int row = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
    figure_.gather(row, drawables_);
    return drawables_;
}

std::string ModelBench::clipLine() const {
    if (!haveFigure_ || !figure_.body() || !figure_.body()->library) return "no figure";
    const int clip = figure_.clip();
    if (clip < 0) return std::string(figure_.body()->label) + ": no clip";
    const content::CookedClip& one = figure_.body()->library->clips.clips[size_t(clip)];
    char line[256];
    std::snprintf(line, sizeof(line),
                  "%s: %s (%s, slot %d) at %.2f s of %.2f, %u frames, %s, travel %.3f m",
                  figure_.body()->label.c_str(), one.label.empty() ? one.name.c_str()
                                                                   : one.label.c_str(),
                  one.name.c_str(), one.slot, figure_.clock(), one.duration, one.frames,
                  one.hold ? "holds" : "loops", one.travel);
    return line;
}

void ModelBench::orbit(float dYawPixels, float dPitchPixels) {
    // A screen's width is about one full turn, which is what a hand expects of a drag.
    yawOffset_ += dYawPixels * 0.006f;
    // Clamped just short of the poles. At exactly straight up the camera's forward vector is
    // the world up it is built against, the cross product is zero, and the view matrix comes
    // out as NaN -- the picture does not tilt, it disappears.
    constexpr float kLimit = 1.52f;  // 87 degrees
    pitchOffset_ += dPitchPixels * 0.006f;
    if (pitchOffset_ > kLimit) pitchOffset_ = kLimit;
    if (pitchOffset_ < -kLimit) pitchOffset_ = -kLimit;
}

void ModelBench::zoom(float notches) {
    // Multiplicative, so a notch moves the same fraction whether the subject is a candle or
    // a house; additive steps are unusable across a list whose radii differ by a hundred.
    zoom_ *= std::pow(0.88f, notches);
    if (zoom_ < 0.05f) zoom_ = 0.05f;
    if (zoom_ > 20.0f) zoom_ = 20.0f;
}

void ModelBench::update(double seconds, double delta, bool spin) {
    if (haveFigure_) figure_.update(float(delta));
    // MU looks down at about 40 degrees; the bench keeps that so what is judged here reads
    // the way the game will. The turn is slow enough that a shot every hundred frames walks
    // round the subject rather than jumping.
    float pitch = 38.0f * 3.14159265f / 180.0f + pitchOffset_;
    constexpr float kLimit = 1.52f;
    if (pitch > kLimit) pitch = kLimit;
    if (pitch < -kLimit) pitch = -kLimit;
    const float yaw = (spin ? float(seconds) * 0.35f : 0.9f) + yawOffset_;
    const float distance = distance_ * zoom_;

    camera_.target[0] = focus_[0];
    camera_.target[1] = focus_[1];
    camera_.target[2] = focus_[2];
    camera_.position[0] = focus_[0] + std::cos(yaw) * std::cos(pitch) * distance;
    camera_.position[1] = focus_[1] + std::sin(pitch) * distance;
    camera_.position[2] = focus_[2] + std::sin(yaw) * std::cos(pitch) * distance;
    // The near plane follows the distance: MU2's units put a model 300 away, and a near
    // plane of 10 there wastes most of the depth buffer's precision. A floor of 1 metre was
    // right while the bench framed a house and is wrong now the wheel can bring a candle to
    // arm's length -- at 0.3 m away a near plane of 1 clips the whole subject away.
    camera_.nearPlane = std::max(distance * 0.02f, 0.02f);
    camera_.farPlane = std::max(distance * 20.0f, 2000.0f);
}

void ModelBench::shutdown() {
    model_.shutdown();
    ground_.shutdown();
    figures_.shutdown();
    drawables_.clear();
}

}  // namespace mu::game
