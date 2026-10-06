#include "game/world/boids.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "content/grid.h"
#include "core/files.h"
#include "core/log.h"
#include "game/sound.h"

namespace mu::game {
namespace {

// How big a bird is drawn. MU2's `bird.Node.Scale = Vector3.One * 0.8f`; the cooked mesh is
// 0.68 m across the wings, so this flies a half-metre bird.
constexpr float kBirdScale = 0.8f;
// The dragons' softening (Boids::gather): five copies at 0.3 of opaque, four of them shifted
// 12 cm along each side and up and down.
constexpr int kSoftCopies = 5;
constexpr float kSoftFade = 0.3f;
constexpr float kSoftShift[kSoftCopies - 1][3] = {
    {0.12f, 0.0f, 0.0f}, {-0.12f, 0.0f, 0.0f}, {0.0f, 0.08f, 0.12f}, {0.0f, -0.08f, -0.12f}};

// What the rules ask of the world, answered with the real ground and the real camera. See
// game/world/flight.h for why they are asked at all rather than reached for.
struct Looking {
    const content::Ground* ground;
    const float* viewProj;
};

float groundAt(void* context, float x, float z) {
    return static_cast<const Looking*>(context)->ground->heightAt(x, z);
}

// A safe tile: in Atlans the basin, which is dry (Flight::swim).
bool dryAt(void* context, float x, float z) {
    const content::Ground& ground = *static_cast<const Looking*>(context)->ground;
    const float perTile = std::max(ground.metresPerTile(), 0.001f);
    const int c = int(std::floor(x / perTile));
    const int r = int(std::floor(-z / perTile));
    return c >= 0 && r >= 0 && c < ground.size() && r < ground.size() &&
           (ground.attributesAt(c, r) & content::kSafeZone) != 0;
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
    // Noria's: MODEL_BUTTERFLY01, which MuMain loads from Data/Object1 -- Lorencia's folder
    // -- for Noria (MapManager.cpp:89). tools/cook.py's AIRS cooks it into Noria from there.
    if (world == "noria") return "Butterfly01";
    // The Dungeon's: MODEL_BAT01, as the Lost Tower's (GOBoid.cpp:1327-1328).
    if (world == "dungeon" || world == "losttower") return "Bat01";
    // Every Blood Castle's: MODEL_CROW (GOBoid.cpp:1342-1345), cooked from Object12.
    if (world == "bloodcastle") return "Crow01";
    // Atlans's: MODEL_FISH01 + 1 (CreateAtlanseFish, GOBoid.cpp:886-918). MU rolls + 1 or + 2;
    // Fish02 alone here until Fish03 is cooked.
    if (world == "atlans") return "Fish02";
    // Icarus's: MODEL_DRAGON_, Monster32, the Red Dragon's body (CreateDragon, GOBoid.cpp:824).
    if (world == "icarus") return "Dragon01";
    return std::string();
}

Airs airsOf(const std::string& world) {
    Airs airs;  // the defaults are the bird's: 1.0, lit, calling
    if (world == "noria") {
        // GOBoid.cpp:1335: `Velocity = 0.3f; LightEnable = false; Light = (1, 1, 1)`, and not
        // in the chain of boids that call.
        airs.speed = 0.3f;
        airs.lit = false;
        airs.tint[0] = airs.tint[1] = airs.tint[2] = 1.0f;
        airs.calls = false;
        // MU's key a frame over the cook's 0.25 s: 0.25 / 0.04, a beat of 0.12 s, eight a
        // second, as a real butterfly's.
        airs.flap = 6.25f;
    }
    if (world == "dungeon" || world == "losttower") {
        // The bat keeps the bird's 1.0, its light and its 0.5 (GOBoid.cpp:1316-1320), and calls
        // SOUND_BAT01 alone. MU plays a boid a key a frame; the cook spaces Bat01's four keys
        // 0.25 s apart, so 6.25 for the client's 0.04.
        airs.flap = 6.25f;
        airs.call[0] = "boid_bat";
        airs.call[1] = nullptr;
    }
    if (world == "atlans") {
        // CreateAtlanseFish: `LightEnable = false`, `Light = (1, 1, 1)`, and no fish calls.
        airs.lit = false;
        airs.tint[0] = airs.tint[1] = airs.tint[2] = 1.0f;
        airs.calls = false;
        airs.call[0] = airs.call[1] = nullptr;
    }
    if (world == "icarus") {
        // The dragon: lit, but RenderObject sets its BodyLight to (0.02, 0.05, 0.15) in Icarus
        // (ZzzObject.cpp:487-493), a dark blue shape far below; it never calls.
        airs.lit = false;
        airs.tint[0] = 0.02f;
        airs.tint[1] = 0.05f;
        airs.tint[2] = 0.15f;
        airs.calls = false;
        airs.call[0] = airs.call[1] = nullptr;
    }
    if (world == "bloodcastle") {
        // The crow keeps the bird's 1.0, its light and its 0.5 (GOBoid.cpp:1316-1324) and caws
        // SOUND_CROW alone, a frame in 128, over the court only.
        airs.call[0] = "boid_crow";
        airs.call[1] = nullptr;
        airs.callEvery = 128.0f;
        airs.callRolls = 1;
        airs.callsOverSafe = true;
    }
    return airs;
}

bool Boids::open(const std::string& assetDir, const std::string& world, const std::string& model,
                 content::Textures& textures, const Airs& airs, Sound* sound) {
    shutdown();
    airs_ = airs;
    sound_ = sound;
    flight_.reset();
    flight_.setPace(airs_.speed);
    flight_.setCalls(airs_.callEvery, airs_.callRolls);
    crowEyes_ = model == "Crow01";
    flight_.setButterfly(model == "Butterfly01");
    flight_.setBat(model == "Bat01");
    flight_.setFish(model == "Fish02");
    flight_.setDragon(model == "Dragon01");
    scurry_.open(assetDir, world, crawlOf(world), textures, sound);
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
        if (airs_.call[0]) call1_ = sound_->load(airs_.call[0], true, true);
        if (airs_.call[1]) call2_ = sound_->load(airs_.call[1], true, true);
    }
    core::logf("boids: %s flies over %s, %d birds at %.2f of a bird's pace, %s", model.c_str(),
               world.c_str(), Flight::kMaxBirds, double(airs_.speed),
               airs_.calls && call1_ >= 0 ? "calling" : "silent");
    return true;
}

void Boids::shutdown() {
    scurry_.shutdown();
    body_.reset();
    library_.reset();
    if (mesh_) mesh_->shutdown();
    mesh_.reset();
    scratch_.clear();
    for (int i = 0; i < Flight::kMaxSlots; ++i) {
        paletteRows_[i] = -1;
        standing_[i] = false;
    }
    call1_ = call2_ = -1;
}

void Boids::update(float seconds, const float hero[3], bool walking, bool indoors,
                   const content::Ground& ground, const float* viewProj,
                   gfx::Renderer& renderer) {
    scurry_.update(seconds, hero, ground, renderer);
    if (!body_) return;

    Looking looking{&ground, viewProj};
    Sky sky;
    sky.ground = &groundAt;
    sky.inFrame = &inFrame;
    if (flight_.isFish()) sky.dry = &dryAt;
    sky.context = &looking;

    BirdCall calls[Flight::kMostCalls];
    int called = 0;
    // A bat is the underground's own: the Dungeon is "indoors" on every tile (World::indoors),
    // and MU flies its bats there (GOBoid.cpp:1327). The roof rule is the birds'.
    // Blood Castle is "underground" for its air (no wind, no leaves) but under the open night,
    // and MU flies its crows over every tile of it.
    flight_.update(seconds, hero, walking, indoors && !flight_.isBat() && !crowEyes_, sky, calls,
                   &called);

    // What sounded. Placed at the bird, which is one of the few sounds the client loads with 3D
    // enabled. The height is kept: MU's SetPosition dropped it, and here it only moves the pan
    // to where the bird is drawn, since loudness is along the ground (Sound's weigh()).
    if (sound_ != nullptr && airs_.calls) {
        for (int i = 0; i < called; ++i) {
            const int event = calls[i].which == 0 ? call1_ : call2_;
            if (event >= 0 && airs_.callsOverSafe) {
                const float perTile = std::max(ground.metresPerTile(), 0.001f);
                const int c = int(std::floor(calls[i].at[0] / perTile));
                const int r = int(std::floor(-calls[i].at[2] / perTile));
                // Off the court a quarter as often, one call in four kept -- ours: MU caws only
                // over the court (the user, 2026-10-02, of the crows over the bridge: 'lets do it
                // i like it').
                const bool safe = c >= 0 && r >= 0 && c < ground.size() && r < ground.size() &&
                                  (ground.attributesAt(c, r) & content::kSafeZone) != 0;
                if (!safe && (offCourt_++ & 3) != 0) continue;
            }
            if (event >= 0) {
                sound_->playAt(event, calls[i].at[0], calls[i].at[1], calls[i].at[2]);
            }
        }
    }

    const float perTile = std::max(ground.metresPerTile(), 0.001f);
    for (int i = 0; i < Flight::kMaxSlots; ++i) {
        const Flight::Bird& bird = flight_.bird(i);
        if (!bird.live) {
            paletteRows_[i] = -1;
            standing_[i] = false;
            continue;
        }
        Figure& figure = figures_[i];
        if (!standing_[i]) {
            standing_[i] = true;
            figure.stand(body_.get(), bird.position, bird.facing,
                         flight_.isFish() || flight_.isDragon() ? bird.size : kBirdScale);
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
        // A fish's tail beats with its pace, and a small one's faster (ours, Flight::swim);
        // MU plays every boid at one rate.
        float flap = airs_.flap;
        // A dragon plays its clip at its own Velocity, keys a frame (GOBoid.cpp:627), over the
        // cook's 0.25 s a key: 6.25 times.
        if (flight_.isDragon()) flap = 6.25f * bird.cruise;
        if (flight_.isFish() && bird.cruise > 0.0f) {
            flap *= std::clamp(0.6f + 0.55f * bird.speed / bird.cruise, 0.6f, 2.4f) *
                    (0.65f / std::max(bird.size, 0.2f)) * 0.8f;
        }
        figure.update(seconds, flap);
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

void Boids::glow(gfx::Effects& effects) {
    // MODEL_BUTTERFLY01's own line in the boids' render (GOBoid.cpp:1558): a BITMAP_LIGHT at
    // Scale 1 on the butterfly, `Luminosity * (0.2, 0.4, 0.4)` with Luminosity rolled 0.64 to
    // 0.96 each frame MU draws -- a cyan firefly. Rolled 25 times a second here, as the
    // lanterns are, since a roll at the monitor's rate is a strobe.
    if (!bgfx::isValid(glowSheet_)) return;
    if (crowEyes_) {
        // MODEL_CROW's eyes (GOBoid.cpp:1573-1585): a BITMAP_LIGHT at bone 1, five units either
        // side, Scale 0.1, (1, 0.2, 0) times (rand() % 32 + 128) * 0.01 -- the butterfly's roll
        // carried onto 1.28-1.59.
        static const float kEyes[2][3] = {{-0.05f, 0.0f, 0.0f}, {0.05f, 0.0f, 0.0f}};
        for (int i = 0; i < Flight::kMaxSlots; ++i) {
            if (!flight_.bird(i).live || !standing_[i]) continue;
            const float luminosity = 1.28f + (glowLevel_[i] - 0.64f) / 0.32f * 0.31f;
            for (const float* eye : kEyes) {
                gfx::Sprite sprite;
                if (!figures_[i].pointOn(1, eye, sprite.position)) continue;
                sprite.halfWidth = sprite.halfHeight = 0.5f * 0.64f * 0.1f;
                sprite.colour[0] = luminosity;
                sprite.colour[1] = 0.2f * luminosity;
                sprite.colour[2] = 0.0f;
                sprite.sheet = glowSheet_;
                sprite.blend = gfx::Blend::Additive;
                effects.add(sprite);
            }
        }
        return;
    }
    if (!flight_.isButterfly()) return;
    for (int i = 0; i < Flight::kMaxSlots; ++i) {
        const Flight::Bird& bird = flight_.bird(i);
        if (!bird.live) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = bird.position[k];
        sprite.halfWidth = sprite.halfHeight = 0.5f * 0.64f;  // Scale 1 over a 64-texel sheet
        const float luminosity = glowLevel_[i];
        sprite.colour[0] = 0.2f * luminosity;
        sprite.colour[1] = sprite.colour[2] = 0.4f * luminosity;
        sprite.sheet = glowSheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

void Boids::stepGlow(float seconds) {
    glowWait_ -= seconds;
    if (glowWait_ > 0.0f) return;
    glowWait_ = 1.0f / 25.0f;
    for (float& level : glowLevel_) {
        glowSeed_ ^= glowSeed_ << 13;
        glowSeed_ ^= glowSeed_ >> 17;
        glowSeed_ ^= glowSeed_ << 5;
        level = float(glowSeed_ % 32u + 64u) * 0.01f;
    }
}

void Boids::gather(std::vector<gfx::Drawable>& out) const {
    scurry_.gather(out);
    if (!body_) return;
    for (int i = 0; i < Flight::kMaxSlots; ++i) {
        if (!flight_.bird(i).live || paletteRows_[i] < 0) continue;
        const size_t first = out.size();
        figures_[i].gather(paletteRows_[i], out);
        for (size_t at = first; at < out.size(); ++at) {
            out[at].light[0] = light_[i][0];
            out[at].light[1] = light_[i][1];
            out[at].light[2] = light_[i][2];
        }
 
        // Icarus's dragons, softened as far things are (ours; the user, 2026-10-06: 'can we
        // make that flying dragon more blurry to simuate that he is in far distance?'). No blur
        // pass to put one in: the body is drawn kSoftCopies times, each a faint see-through
        // copy shifted a few centimetres, so the overlaps sum near solid in its middle and its
        // outline thins away.
        if (flight_.isDragon()) {
            const size_t last = out.size();
            for (size_t at = first; at < last; ++at) out[at].fade = kSoftFade;
            for (int copy = 1; copy < kSoftCopies; ++copy) {
                for (size_t at = first; at < last; ++at) {
                    gfx::Drawable soft = out[at];
                    for (int k = 0; k < 3; ++k) soft.transform[12 + k] += kSoftShift[copy - 1][k];
                    out.push_back(soft);
                }
            }
        }
    }
}

}  // namespace mu::game
