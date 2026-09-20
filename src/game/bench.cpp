#include "game/bench.h"

#include <bx/math.h>

#include <algorithm>
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

// How high above the land the subject floats, as a multiple of its own radius. Held off the
// ground rather than stood on it: a viewer is for turning a thing over and looking at its
// underside, and a model sitting on the land hides its own base and takes the ground's bounce
// on it. The contact -- the shadow where it meets the earth -- is what the town is for.
constexpr float kFloatRadii = 2.2f;

void ModelBench::frameOn(float radius, const content::Bounds& bounds) {
    lift_ = radius * kFloatRadii;
    // The subject's own centre is where the camera looks, and that is exactly stand + lift:
    // `place` translates the model by minus its bounds' centre, so the centre lands there and
    // nowhere else. Adding the centre again here -- which this did for one run -- aims the
    // camera at a point the model is not at, by as much as the model is off its own origin,
    // and the thing sits low and to one side of every shot for no visible reason.
    focus_[0] = stand_[0];
    focus_[1] = stand_[1] + lift_;
    focus_[2] = stand_[2];
    height_ = bounds.max[1] - bounds.min[1];
    if (distance_ <= 0.0f) distance_ = radius * 3.0f;
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
        distance_ = radius * 3.0f;
    }

    drawables_.clear();
    if (!haveWorldGround_) {
        if (!makeGround(textures, std::max(radius * 8.0f, 400.0f))) return false;
        gfx::Drawable groundDraw;
        groundDraw.mesh = &ground_;
        bx::mtxIdentity(groundDraw.transform);
        drawables_.push_back(groundDraw);
    }

    if (haveModel_) {
        gfx::Drawable modelDraw;
        modelDraw.mesh = &model_;
        // Floated clear of the land and centred on its own bounds, so the camera turns about
        // the middle of the thing and every face of it comes round. Its own centre is
        // subtracted rather than its minimum y: a model authored about its feet and one
        // authored about its middle then hang the same way.
        const content::Bounds& b = model_.bounds();
        bx::mtxTranslate(modelDraw.transform, stand_[0] - b.centre[0],
                         stand_[1] + lift_ - b.centre[1], stand_[2] - b.centre[2]);
        drawables_.push_back(modelDraw);
    }

    core::logf("bench: %s, radius %.1f, camera at %.1f units", haveModel_ ? model_.name().c_str()
                                                                         : "the ground alone",
               radius, distance_);
    return true;
}

bool ModelBench::openBrowser(const std::string& assetDir, const std::string& world,
                             content::Textures& textures) {
    // Two directories, because the cook writes to two: a world's own models, and the figures',
    // which are cooked once and reached by every map. Both are walked and the list is sorted
    // by file name, so stepping through it is alphabetical and repeatable between runs.
    const std::string cooked = core::join(assetDir, "cooked");
    const std::string dirs[2] = {core::join(core::join(cooked, world), "meshes"),
                                 core::join(core::join(cooked, "figures"), "meshes")};
    browse_.clear();
    for (const std::string& dir : dirs) {
        std::error_code error;
        std::filesystem::directory_iterator walk(dir, error);
        if (error) {
            core::logf("browser: nothing cooked in %s", dir.c_str());
            continue;
        }
        for (const std::filesystem::directory_entry& entry : walk) {
            if (entry.path().extension() == ".mum") browse_.push_back(entry.path().string());
        }
    }
    std::sort(browse_.begin(), browse_.end(), [](const std::string& a, const std::string& b) {
        return std::filesystem::path(a).filename() < std::filesystem::path(b).filename();
    });
    if (browse_.empty()) {
        core::logError("browser: no cooked meshes under %s. Run tools/cook.py first: this "
                       "walks what the cook wrote, not the glb it was made from",
                       cooked.c_str());
        return false;
    }
    browseDir_ = assetDir;
    browseAt_ = 0;
    raiseWorldGround(assetDir, world, textures);
    core::logf("browser: %zu cooked meshes; left and right step one, up and down step ten",
               browse_.size());
    return step(0, textures);
}

bool ModelBench::step(int by, content::Textures& textures) {
    if (browse_.empty()) return false;
    // Clamped rather than wrapped. A list this long is walked to look for something, and a
    // wrap at the end quietly puts you back at the start with nothing to say it happened.
    const long long wanted = (long long)browseAt_ + by;
    const long long last = (long long)browse_.size() - 1;
    browseAt_ = size_t(wanted < 0 ? 0 : (wanted > last ? last : wanted));

    const std::string& path = browse_[browseAt_];
    const std::vector<uint8_t> bytes = core::readFile(path);
    content::CookedMesh cooked;
    std::string error;
    if (bytes.empty() || !content::parseCookedMesh(bytes, cooked, error)) {
        core::logError("browser: %s did not parse: %s", path.c_str(),
                       bytes.empty() ? "unreadable" : error.c_str());
        haveModel_ = false;
        return false;
    }
    const std::string name = std::filesystem::path(path).filename().string();
    // The camera is re-framed on every model, so --dist is honoured once and then the list's
    // own sizes take over; a cannon and a candle cannot share one distance.
    if (!wantsFixedDistance_) distance_ = 0.0f;
    haveModel_ = model_.buildFromCooked(cooked, name, browseDir_, textures);
    if (!haveModel_) return false;
    return place(textures);
}

std::string ModelBench::browseLine() const {
    if (browse_.empty()) return std::string();
    char line[512];
    std::snprintf(line, sizeof(line), "%zu/%zu  %s  (%zu parts, %zu materials)", browseAt_ + 1,
                  browse_.size(),
                  std::filesystem::path(browse_[browseAt_]).filename().string().c_str(),
                  model_.parts().size(), model_.materials().size());
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
    if (distance_ <= 0.0f) distance_ = radius * 3.0f;
    if (!makeGround(textures, std::max(radius * 8.0f, 20.0f))) return false;

    scratch_.assign(size_t(128) * 12, 0.0f);   // as game/crowd.cpp: the rig's count, not the palette's
    drawables_.clear();
    gfx::Drawable groundDraw;
    groundDraw.mesh = &ground_;
    bx::mtxIdentity(groundDraw.transform);
    drawables_.push_back(groundDraw);
    core::logf("bench: %s, %zu parts, %zu held, radius %.2f m, camera at %.2f m",
               body->label.c_str(), body->parts.size(), body->held.size(), radius, distance_);
    return true;
}

const std::vector<gfx::Drawable>& ModelBench::gather(gfx::Renderer& renderer) {
    if (!haveFigure_) return drawables_;
    // The ground is drawables_[0] and was made once; everything after it is this frame's.
    drawables_.resize(1);
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

void ModelBench::update(double seconds, double delta, bool spin) {
    if (haveFigure_) figure_.update(float(delta));
    // MU looks down at about 40 degrees; the bench keeps that so what is judged here reads
    // the way the game will. The turn is slow enough that a shot every hundred frames walks
    // round the subject rather than jumping.
    const float pitch = 38.0f * 3.14159265f / 180.0f;
    const float yaw = spin ? float(seconds) * 0.35f : 0.9f;

    camera_.target[0] = focus_[0];
    camera_.target[1] = focus_[1];
    camera_.target[2] = focus_[2];
    camera_.position[0] = focus_[0] + std::cos(yaw) * std::cos(pitch) * distance_;
    camera_.position[1] = focus_[1] + std::sin(pitch) * distance_;
    camera_.position[2] = focus_[2] + std::sin(yaw) * std::cos(pitch) * distance_;
    // The near plane follows the distance: MU2's units put a model 300 away, and a near
    // plane of 10 there wastes most of the depth buffer's precision.
    camera_.nearPlane = std::max(distance_ * 0.02f, 1.0f);
    camera_.farPlane = std::max(distance_ * 20.0f, 2000.0f);
}

void ModelBench::shutdown() {
    model_.shutdown();
    ground_.shutdown();
    figures_.shutdown();
    drawables_.clear();
}

}  // namespace mu::game
