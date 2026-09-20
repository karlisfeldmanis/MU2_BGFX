#include "game/bench.h"

#include <bx/math.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace mu::game {

bool ModelBench::makeGround(content::Textures& textures, float halfSize) {
    // A plane that a model can cast onto and be occluded against, in the world's own units.
    // Flat and untextured on purpose: this bench is about the model, and a tiled ground
    // would argue with it. The real ground arrives in sprint 2.
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

bool ModelBench::open(const std::string& modelPath, content::Textures& textures) {
    if (!modelPath.empty()) {
        haveModel_ = model_.load(modelPath, textures);
        if (!haveModel_) return false;
    }

    float radius = 100.0f;
    if (haveModel_) {
        const content::Bounds& b = model_.bounds();
        radius = b.radius > 0.0f ? b.radius : 100.0f;
        // The model is stood on the plane rather than centred on it: what this bench is for
        // is the contact — the shadow under it and the occlusion where it meets the ground.
        focus_[0] = b.centre[0];
        focus_[1] = (b.max[1] - b.min[1]) * 0.4f;
        focus_[2] = b.centre[2];
        height_ = b.max[1] - b.min[1];
    }
    if (distance_ <= 0.0f) distance_ = radius * 3.0f;

    if (!makeGround(textures, std::max(radius * 8.0f, 400.0f))) return false;

    drawables_.clear();
    gfx::Drawable groundDraw;
    groundDraw.mesh = &ground_;
    bx::mtxIdentity(groundDraw.transform);
    // The model's own minimum y is put on the plane, so a model authored about its centre
    // does not float or sink.
    drawables_.push_back(groundDraw);

    if (haveModel_) {
        gfx::Drawable modelDraw;
        modelDraw.mesh = &model_;
        bx::mtxTranslate(modelDraw.transform, 0.0f, -model_.bounds().min[1], 0.0f);
        drawables_.push_back(modelDraw);
    }

    core::logf("bench: %s, radius %.1f, camera at %.1f units", haveModel_ ? model_.name().c_str()
                                                                         : "the ground alone",
               radius, distance_);
    return true;
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
