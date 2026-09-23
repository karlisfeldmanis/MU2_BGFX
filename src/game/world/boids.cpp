#include "game/world/boids.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/files.h"
#include "core/log.h"
#include "game/sound.h"

namespace mu::game {
namespace {

// How big a bird is drawn. MU2's `bird.Node.Scale = Vector3.One * 0.8f`; the cooked mesh is
// 0.68 m across the wings, so this flies a half-metre bird.
constexpr float kBirdScale = 0.8f;

// What the rules ask of the world, answered with the real ground and the real camera. See
// game/world/flight.h for why they are asked at all rather than reached for.
struct Looking {
    const content::Ground* ground;
    const float* viewProj;
};

float groundAt(void* context, float x, float z) {
    return static_cast<const Looking*>(context)->ground->heightAt(x, z);
}

// How far outside the frame still counts as on screen, as a share of it. A bird is a point to
// this test and a model on screen, and the model has a wingspan; the margin covers that and the
// frame or two between deciding and drawing.
constexpr float kScreenMargin = 0.15f;

bool inFrame(void* context, const float at[3]) {
    const float* m = static_cast<const Looking*>(context)->viewProj;
    if (m == nullptr) return false;  // nobody looking: the client's own answer
    float out[4];
    for (int i = 0; i < 4; ++i) {
        out[i] = at[0] * m[0 * 4 + i] + at[1] * m[1 * 4 + i] + at[2] * m[2 * 4 + i] + m[3 * 4 + i];
    }
    // The house idiom, in clip space: `w <= 0` is behind the lens, which a divide would fold
    // back into the middle of the frame and call a bird directly behind the player watched.
    if (out[3] <= 0.0f) return false;
    const float edge = 1.0f + kScreenMargin;
    return std::fabs(out[0]) <= edge * out[3] && std::fabs(out[1]) <= edge * out[3];
}

}  // namespace

std::string boidOf(const std::string& world) {
    // Lorencia's MODEL_BIRD01, and nothing else is cooked yet. Noria's butterfly is the next row
    // and is deliberately not guessed: its model is Butterfly01, its velocity 0.3 and it neither
    // takes the terrain's light nor calls, and none of that is worth writing down until there is
    // a cooked Butterfly01 to try it on. See tools/cook.py's AIRS.
    if (world == "lorencia") return "Bird01";
    return std::string();
}

Airs airsOf(const std::string& world) {
    Airs airs;  // the defaults are the bird's: 1.0, lit, calling
    (void)world;
    return airs;
}

bool Boids::open(const std::string& assetDir, const std::string& world, const std::string& model,
                 content::Textures& textures, const Airs& airs, Sound* sound) {
    shutdown();
    airs_ = airs;
    sound_ = sound;
    flight_.reset();
    flight_.setPace(airs_.speed);
    if (model.empty()) return true;

    const std::string dir = core::join(assetDir, "cooked/" + world);
    const std::string meshPath = core::join(dir, "meshes/" + model + ".mum");
    const std::string clipPath = core::join(dir, "clips/" + model + ".muc");

    std::vector<uint8_t> meshBytes = core::readFile(meshPath);
    if (meshBytes.empty()) {
        // Not an error and not silent: a world whose boid has not been cooked flies nothing, and
        // the log says which file would have made it fly. tools/cook.py's AIRS names it.
        core::logf("boids: no cooked %s at %s -- nothing flies over %s", model.c_str(),
                   meshPath.c_str(), world.c_str());
        return true;
    }
    content::CookedMesh cooked;
    std::string error;
    if (!content::parseCookedMesh(meshBytes, cooked, error)) {
        core::logError("%s: %s", meshPath.c_str(), error.c_str());
        return false;
    }
    auto mesh = std::make_unique<content::Mesh>();
    if (!mesh->buildFromCooked(cooked, model, assetDir, textures)) return false;
    if (!mesh->isSkinned()) {
        core::logError("boids: %s is not skinned, so it cannot flap", model.c_str());
        return false;
    }

    std::vector<uint8_t> clipBytes = core::readFile(clipPath);
    auto library = std::make_unique<ClipLibrary>();
    if (clipBytes.empty() || !content::parseCookedClips(clipBytes, library->clips, error)) {
        core::logError("%s: %s", clipPath.c_str(),
                       clipBytes.empty() ? "is not there" : error.c_str());
        return false;
    }
    if (library->clips.clips.empty()) return false;

    auto body = std::make_unique<FigureBody>();
    body->name = model;
    body->parts.push_back(mesh.get());
    body->skeletonMesh = mesh.get();
    body->library = library.get();
    // Identity, for Sway's reason: the clip is baked from the very same glb as the mesh, in the
    // very same joint order, so there is no second rig here to name-match against.
    const size_t bones = std::min(mesh->bones().size(), size_t(library->clips.bones));
    body->clipBoneOf.assign(mesh->bones().size(), -1);
    for (size_t i = 0; i < bones; ++i) body->clipBoneOf[i] = int32_t(i);
    body->idleClip = 0;
    const content::Bounds& box = mesh->bounds();
    for (int axis = 0; axis < 3; ++axis) {
        body->min[axis] = box.min[axis];
        body->max[axis] = box.max[axis];
    }
    body->radius = box.radius;

    mesh_ = std::move(mesh);
    library_ = std::move(library);
    body_ = std::move(body);
    scratch_.assign(size_t(gfx::Renderer::kMaxBones) * 12, 0.0f);

    if (sound_ != nullptr && airs_.calls) {
        call1_ = sound_->load("bird_1", true, true);
        call2_ = sound_->load("bird_2", true, true);
    }
    core::logf("boids: %s flies over %s, %d birds at %.2f of a bird's pace, %s", model.c_str(),
               world.c_str(), Flight::kMaxBirds, double(airs_.speed),
               airs_.calls && call1_ >= 0 ? "calling" : "silent");
    return true;
}

void Boids::shutdown() {
    body_.reset();
    library_.reset();
    if (mesh_) mesh_->shutdown();
    mesh_.reset();
    scratch_.clear();
    for (int i = 0; i < Flight::kMaxBirds; ++i) {
        paletteRows_[i] = -1;
        standing_[i] = false;
    }
    call1_ = call2_ = -1;
}

void Boids::update(float seconds, const float hero[3], bool walking, bool indoors,
                   const content::Ground& ground, const float* viewProj,
                   gfx::Renderer& renderer) {
    if (!body_) return;

    Looking looking{&ground, viewProj};
    Sky sky;
    sky.ground = &groundAt;
    sky.inFrame = &inFrame;
    sky.context = &looking;

    BirdCall calls[Flight::kMostCalls];
    int called = 0;
    flight_.update(seconds, hero, walking, indoors, sky, calls, &called);

    // What sounded. Placed at the bird, which is one of the few sounds the client loads with 3D
    // enabled; the heights are dropped, as MU's own SetPosition does.
    if (sound_ != nullptr && airs_.calls) {
        for (int i = 0; i < called; ++i) {
            const int event = calls[i].which == 0 ? call1_ : call2_;
            if (event >= 0) sound_->playAt(event, calls[i].at[0], calls[i].at[2]);
        }
    }

    const float perTile = std::max(ground.metresPerTile(), 0.001f);
    for (int i = 0; i < Flight::kMaxBirds; ++i) {
        const Flight::Bird& bird = flight_.bird(i);
        if (!bird.live) {
            paletteRows_[i] = -1;
            standing_[i] = false;
            continue;
        }
        Figure& figure = figures_[i];
        if (!standing_[i]) {
            standing_[i] = true;
            figure.stand(body_.get(), bird.position, bird.facing, kBirdScale);
            figure.play(body_->idleClip);
            // Each on its own beat, or five birds flap as one wing. The client zeroes the frame
            // for every boid; MU2 gave the town's placements a seeded phase for the same reason
            // and this is that, off the bird's own place so it needs no roll of its own.
            const float phase =
                std::fmod(std::fabs(bird.position[0] * 0.37f + bird.position[2] * 0.61f),
                          std::max(figure.length(), 0.001f));
            figure.setClock(phase);
        } else {
            figure.place(bird.position, bird.facing, false);
        }
        // The clock runs whether or not it is seen, as Sway's does, so a bird the camera turns
        // back to is where its own time has taken it.
        figure.update(seconds, 1.0f);
        const int posed = figure.pose(scratch_.data());
        paletteRows_[i] = posed > 0 ? renderer.addPalette(scratch_.data(), posed) : -1;

        // `o->LightEnable`: a bird takes MU's baked terrain light from the tile it is over, so it
        // darkens as it crosses the shaded side of a building; an unlit boid is drawn at its own
        // `o->Light` wherever it is. The tile under it, as MU's own `Light = TerrainLight[...]`
        // does, with no interpolation -- one lookup per bird per frame, and the bird is moving.
        if (airs_.lit) {
            ground.lightAt(int(bird.position[0] / perTile), int(-bird.position[2] / perTile),
                           light_[i]);
        } else {
            std::memcpy(light_[i], airs_.tint, sizeof(light_[i]));
        }
    }
}

void Boids::gather(std::vector<gfx::Drawable>& out) const {
    if (!body_) return;
    for (int i = 0; i < Flight::kMaxBirds; ++i) {
        if (!flight_.bird(i).live || paletteRows_[i] < 0) continue;
        const size_t first = out.size();
        figures_[i].gather(paletteRows_[i], out);
        for (size_t at = first; at < out.size(); ++at) {
            out[at].light[0] = light_[i][0];
            out[at].light[1] = light_[i][1];
            out[at].light[2] = light_[i][2];
        }
    }
}

}  // namespace mu::game
