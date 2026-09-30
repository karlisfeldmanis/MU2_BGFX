#include "game/world/scurry.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/sound.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kFrames = 25.0f;        // REFERENCE_FPS: MU's rates are a frame of these
constexpr float kSpawnBox = 5.12f;      // rand() % 1024 - 512 units
constexpr float kFarGone = 15.0f;       // Range >= 1500
constexpr float kHeard = 6.0f;          // Range < 600
constexpr float kFlockRange = 4.0f;     // MoveBoid's 400
constexpr float kPersonalSpace = 0.8f;  // and its 80
constexpr float kTurnRate = 13.0f;      // `Gravity = 13`, degrees a frame
constexpr float kFadeStep = 0.05f;      // Alpha(): 0.05 a frame toward AlphaTarget
constexpr uint16_t kNoGround = 8;       // TW_NOGROUND: the attribute byte at or past it is void

float wrapPi(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

}  // namespace

std::string crawlOf(const std::string& world) {
    // MoveFishs' WD_1DUNGEON arm (GOBoid.cpp:1720-1722). Must agree with tools/cook.py's CRAWLS.
    if (world == "dungeon") return "Rat01";
    return std::string();
}

float Scurry::random01() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ & 0xFFFFFFu) / float(0x1000000);
}

bool Scurry::open(const std::string& assetDir, const std::string& world, const std::string& model,
                  content::Textures& textures, Sound* sound) {
    shutdown();
    sound_ = sound;
    for (Rat& rat : rats_) rat = Rat{};
    if (model.empty()) return true;

    const std::string dir = core::join(assetDir, "cooked/" + world);
    const std::string meshPath = core::join(dir, "meshes/" + model + ".mum");
    const std::string clipPath = core::join(dir, "clips/" + model + ".muc");
    std::vector<uint8_t> meshBytes = core::readFile(meshPath);
    if (meshBytes.empty()) {
        core::logf("scurry: no cooked %s at %s -- nothing runs in %s", model.c_str(),
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
        core::logError("scurry: %s is not skinned, so it cannot run", model.c_str());
        return false;
    }
    std::vector<uint8_t> clipBytes = core::readFile(clipPath);
    auto library = std::make_unique<ClipLibrary>();
    if (clipBytes.empty() || !content::parseCookedClips(clipBytes, library->clips, error)) {
        core::logError("%s: %s", clipPath.c_str(), clipBytes.empty() ? "is not there" : error.c_str());
        return false;
    }
    if (library->clips.clips.empty()) return false;

    // As the birds' body: the clip is baked from the same glb in the same joint order.
    auto body = std::make_unique<FigureBody>();
    body->name = model;
    body->parts.push_back(mesh.get());
    body->skeletonMesh = mesh.get();
    body->library = library.get();
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
    if (sound_ != nullptr) squeak_ = sound_->load("boid_rat", true, true);
    core::logf("scurry: %s runs in %s, %d at most, %s", model.c_str(), world.c_str(), kMaxRats,
               squeak_ >= 0 ? "squeaking" : "silent");
    return true;
}

void Scurry::shutdown() {
    body_.reset();
    library_.reset();
    if (mesh_) mesh_->shutdown();
    mesh_.reset();
    scratch_.clear();
    for (int i = 0; i < kMaxRats; ++i) {
        rats_[i] = Rat{};
        paletteRows_[i] = -1;
        standing_[i] = false;
    }
    squeak_ = -1;
}

bool Scurry::floor(const content::Ground& ground, float x, float z) const {
    const float perTile = std::max(ground.metresPerTile(), 0.001f);
    const int column = int(std::floor(x / perTile)), row = int(std::floor(-z / perTile));
    return (ground.attributesAt(column, row) & 0xFFu) < kNoGround;
}

void Scurry::spawn(Rat& rat, const float hero[3], const content::Ground& ground) {
    const float x = hero[0] + (random01() * 2.0f - 1.0f) * kSpawnBox;
    const float z = hero[2] + (random01() * 2.0f - 1.0f) * kSpawnBox;
    if (!floor(ground, x, z)) return;  // the client's draw missed the floor: next frame
    rat = Rat{};
    rat.position[0] = x;
    rat.position[1] = ground.heightAt(x, z);
    rat.position[2] = z;
    // `Angle = 0`: MU's +x, which is the column, which is this world's +x. Facing is the birds'
    // (sin, cos) over (x, z), so a quarter turn.
    rat.facing = kPi * 0.5f;
    rat.scale = float(int(random01() * 4.0f) + 4) * 0.1f;
    rat.running = float(int(random01() * 128.0f));
    rat.heading[0] = x;
    rat.heading[1] = z;
    rat.live = true;
}

void Scurry::flock(Rat& rat, float factor) {
    // MoveBoid (ZzzAI.cpp:192): each neighbour in range adds its own heading, plus the way to it
    // or minus it inside the personal space, each normalised; the rat turns toward the mean.
    float target[2] = {0.0f, 0.0f};
    int neighbours = 0;
    for (const Rat& other : rats_) {
        if (!other.live || &other == &rat) continue;
        const float dx = rat.position[0] - other.position[0];
        const float dy = rat.position[1] - other.position[1];
        const float dz = rat.position[2] - other.position[2];
        const float apart = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (apart >= kFlockRange) continue;
        const bool close = apart < kPersonalSpace;
        const float own[2] = {other.heading[0] - other.position[0],
                              other.heading[1] - other.position[2]};
        const float to[2] = {other.heading[0] - rat.position[0], other.heading[1] - rat.position[2]};
        const float sum[2] = {(close ? own[0] - to[0] : own[0] + to[0]) * factor,
                              (close ? own[1] - to[1] : own[1] + to[1]) * factor};
        const float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1]);
        if (length <= 0.0001f) continue;
        target[0] += sum[0] / length;
        target[1] += sum[1] / length;
        ++neighbours;
    }
    if (neighbours == 0) return;
    if (target[0] * target[0] + target[1] * target[1] <= 0.0001f) return;
    const float desired = std::atan2(target[0], target[1]);
    const float turn = kTurnRate * kPi / 180.0f * factor;
    rat.facing = wrapPi(rat.facing + std::clamp(wrapPi(desired - rat.facing), -turn, turn));
}

void Scurry::update(float seconds, const float hero[3], const content::Ground& ground,
                    gfx::Renderer& renderer) {
    if (!body_) return;
    const float factor = seconds * kFrames;
    const float perTile = std::max(ground.metresPerTile(), 0.001f);

    for (int i = 0; i < kMaxRats; ++i) {
        Rat& rat = rats_[i];
        if (!rat.live) {
            spawn(rat, hero, ground);
            if (!rat.live) {
                paletteRows_[i] = -1;
                standing_[i] = false;
                continue;
            }
        }

        const bool running = rat.running > 0.0f && !rat.leaving;
        if (running) {
            flock(rat, factor);
            // `Velocity * (rand() % 4 + 6)` units a frame, Velocity 0.6 / scale.
            const float units = 0.6f / rat.scale * float(int(random01() * 4.0f) + 6);
            const float step = units * 0.01f * factor;
            const float forward[2] = {std::sin(rat.facing), std::cos(rat.facing)};
            rat.position[0] += forward[0] * step;
            rat.position[2] += forward[1] * step;
            rat.position[1] = ground.heightAt(rat.position[0], rat.position[2]);
            const float perFrame = units * 0.01f;
            rat.heading[0] = rat.position[0] + 3.0f * forward[0] * perFrame;
            rat.heading[1] = rat.position[2] + 3.0f * forward[1] * perFrame;
            if (!floor(ground, rat.position[0], rat.position[2])) {
                rat.facing = wrapPi(rat.facing + kPi);
                ++rat.strikes;
            } else if (rat.strikes > 0) {
                --rat.strikes;
            }
            if (rat.strikes >= 2) rat.leaving = true;

            const float dx = rat.position[0] - hero[0], dz = rat.position[2] - hero[2];
            const float range = std::sqrt(dx * dx + dz * dz);
            if (range >= kFarGone) rat.leaving = true;
            if (range < kHeard && sound_ != nullptr && squeak_ >= 0 &&
                random01() < factor / 256.0f) {
                sound_->playAt(squeak_, rat.position[0], rat.position[1], rat.position[2]);
            }
        }
        rat.running -= factor;
        if (rat.running <= 0.0f && random01() < factor / 64.0f) {
            rat.running = float(int(random01() * 128.0f));
        }

        // Alpha(): 0.05 a frame toward 1, and ours toward 0 on the way out.
        rat.fade = std::clamp(rat.fade + (rat.leaving ? -kFadeStep : kFadeStep) * factor, 0.0f, 1.0f);
        if (rat.leaving && rat.fade <= 0.0f) {
            rat.live = false;
            paletteRows_[i] = -1;
            standing_[i] = false;
            continue;
        }

        Figure& figure = figures_[i];
        if (!standing_[i]) {
            standing_[i] = true;
            figure.stand(body_.get(), rat.position, rat.facing, rat.scale);
            figure.play(body_->idleClip);
            figure.setClock(std::fmod(rat.position[0] * 0.37f + std::fabs(rat.position[2]) * 0.61f,
                                      std::max(figure.length(), 0.001f)));
        } else {
            figure.place(rat.position, rat.facing, false);
        }
        // MU plays the run at `Velocity * 0.5` keys a frame, 12.5 * 0.6 / scale keys a second,
        // over the cook's four; held while it stands (ours: MU runs it on the spot).
        figure.update(seconds, running ? 12.5f * 0.6f / rat.scale / 4.0f : 0.0f);
        const int posed = figure.pose(scratch_.data());
        paletteRows_[i] = posed > 0 ? renderer.addPalette(scratch_.data(), posed) : -1;
        // `LightEnable`: the baked terrain light of the tile it is on.
        ground.lightAt(int(rat.position[0] / perTile), int(-rat.position[2] / perTile), light_[i]);
    }
}

void Scurry::gather(std::vector<gfx::Drawable>& out) const {
    if (!body_) return;
    for (int i = 0; i < kMaxRats; ++i) {
        if (!rats_[i].live || paletteRows_[i] < 0) continue;
        const size_t first = out.size();
        figures_[i].gather(paletteRows_[i], out);
        for (size_t at = first; at < out.size(); ++at) {
            for (int k = 0; k < 3; ++k) out[at].light[k] = light_[i][k];
            out[at].fade = rats_[i].fade;
        }
    }
}

}  // namespace mu::game
