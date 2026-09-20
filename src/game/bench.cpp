#include "game/bench.h"

#include <bx/math.h>

#include <cmath>
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
    // Wound counter-clockwise seen from above, which is glTF's front face and so the one
    // CULL_CW keeps. Every material in MU2's build is double sided, so nothing else here
    // would have caught this being backwards: the plane simply was not there.
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    content::Material material;
    material.name = "bench ground";
    material.albedo = textures.white();
    material.normal = textures.flatNormal();
    material.orm = textures.white();
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

void ModelBench::update(double seconds, bool spin) {
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
    drawables_.clear();
}

}  // namespace mu::game
