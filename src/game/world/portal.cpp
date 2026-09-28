#include "game/world/portal.h"

#include <algorithm>
#include <cmath>

#include "content/cooked.h"
#include "content/missiles.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kReference = 25.0f;
// Tile (223, 30), its centre, and the discs 350 units over the ground there.
constexpr float kColumn = 223.0f, kRow = 30.0f;
constexpr float kLift = 3.5f;
// The stack, along MU's y -- which is our -z -- in units, and which model each is.
constexpr float kStack[5] = {0.0f, 4.0f, 8.0f, 12.0f, 20.0f};
constexpr int kShapeOf[5] = {0, 1, 0, 1, 2};
constexpr const char* kShapes[3] = {"Warp01", "Warp02", "Warp03"};
// MU's placement angle, (0, 0, 10): a yaw of ten degrees.
constexpr float kYaw = 10.0f * 3.14159265f / 180.0f;
// Seen from further than this it is not drawn; the lamps' own reach.
constexpr float kSight = 40.0f;

}  // namespace

uint32_t Portal::next() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

bool Portal::open(const std::string& assetDir, const std::string& world,
                  const content::Ground& ground, content::Textures& textures) {
    shutdown();
    if (world != "noria") return true;

    content::Missiles missiles;
    if (!missiles.read(core::join(assetDir, "cooked/showing/missiles.mup"))) return false;
    for (const char* name : kShapes) {
        const content::MissileRow* row = missiles.find(name);
        Shape shape;
        content::CookedMesh cooked;
        std::string error;
        const std::vector<uint8_t> bytes =
            row ? core::readFile(core::join(assetDir, row->mesh)) : std::vector<uint8_t>();
        if (bytes.empty() || !content::parseCookedMesh(bytes, cooked, error) ||
            cooked.materials.empty()) {
            core::logf("portal: no cooked %s (tools/cook.py --only missiles); Noria's warp is "
                       "not stood",
                       name);
            shapes_.clear();
            return false;
        }
        for (const content::CookedVertex& v : cooked.vertices) {
            shape.positions.insert(shape.positions.end(), v.position, v.position + 3);
            shape.uvs.insert(shape.uvs.end(), v.uv, v.uv + 2);
        }
        shape.indices = cooked.indices;
        shape.sheet = textures.load(core::join(assetDir, cooked.materials[0].albedo),
                                    content::TextureRole::Albedo);
        shapes_.push_back(std::move(shape));
    }

    const float metres = ground.metresPerTile();
    centre_[0] = (kColumn + 0.5f) * metres;
    centre_[2] = -(kRow + 0.5f) * metres;
    centre_[1] = ground.heightAt(centre_[0], centre_[2]) + kLift;
    for (int i = 0; i < 5; ++i) {
        Disc disc;
        disc.shape = kShapeOf[i];
        disc.at[0] = centre_[0];
        disc.at[1] = centre_[1];
        disc.at[2] = centre_[2] - kStack[i] * 0.01f;
        if (disc.shape == 2) {
            disc.scale = 0.6f;
            disc.turn = 4.0f;  // WARP3 takes no Gravity
        } else {
            disc.scale = 1.3f + float(next() % 50u) / 100.0f;
            disc.turn = 4.0f + float(next() % 80u) / 10.0f;
        }
        disc.angle = float(next() % 360u) * 3.14159265f / 180.0f;
        discs_.push_back(disc);
    }
    core::logf("portal: Noria's warp stands at tile %.0f,%.0f, five discs", kColumn, kRow);
    return true;
}

void Portal::shutdown() {
    shapes_.clear();
    discs_.clear();
}

void Portal::update(float seconds) {
    if (discs_.empty()) return;
    clock_ = std::fmod(clock_ + seconds, 3600.0f);
    const float ms = clock_ * 1000.0f;
    colour_[0] = std::max(0.0f, std::sin(ms * 0.0011f) * 0.2f + 0.01f);
    colour_[1] = std::max(0.0f, std::sin(ms * 0.0017f) * 0.2f + 0.01f);
    colour_[2] = std::max(0.0f, std::sin(ms * 0.0013f) * 0.2f + 0.01f);
    for (Disc& disc : discs_) {
        disc.angle = std::fmod(disc.angle + disc.turn * kReference * seconds * 3.14159265f / 180.0f,
                               6.2831853f);
    }
}

void Portal::gather(gfx::Effects& effects, const float near[3]) const {
    if (discs_.empty()) return;
    const float dx = near[0] - centre_[0], dz = near[2] - centre_[2];
    if (dx * dx + dz * dz > kSight * kSight) return;
    const float cy = std::cos(kYaw), sy = std::sin(kYaw);
    for (const Disc& disc : discs_) {
        const Shape& shape = shapes_[size_t(disc.shape)];
        if (!bgfx::isValid(shape.sheet)) continue;
        // About the disc's own horizontal axis -- MU's y, our z -- and then the ten-degree yaw.
        const float ca = std::cos(disc.angle), sa = std::sin(disc.angle);
        const auto place = [&](uint32_t index, float out[3]) {
            const float* p = &shape.positions[size_t(index) * 3];
            const float x = p[0] * disc.scale, y = p[1] * disc.scale, z = p[2] * disc.scale;
            const float tx = x * ca - y * sa, ty = x * sa + y * ca;  // turned about z
            out[0] = disc.at[0] + tx * cy + z * sy;
            out[1] = disc.at[1] + ty;
            out[2] = disc.at[2] - tx * sy + z * cy;
        };
        for (size_t t = 0; t + 2 < shape.indices.size(); t += 3) {
            gfx::Sprite sprite;
            sprite.placed = true;
            sprite.sheet = shape.sheet;
            sprite.blend = gfx::Blend::Additive;
            const uint32_t corner[4] = {shape.indices[t], shape.indices[t + 1],
                                        shape.indices[t + 2], shape.indices[t + 2]};
            for (int c = 0; c < 4; ++c) {
                place(corner[c], sprite.corner[c]);
                sprite.cornerUv[c][0] = shape.uvs[size_t(corner[c]) * 2];
                sprite.cornerUv[c][1] = shape.uvs[size_t(corner[c]) * 2 + 1];
            }
            for (int a = 0; a < 3; ++a) {
                sprite.position[a] =
                    (sprite.corner[0][a] + sprite.corner[1][a] + sprite.corner[2][a]) / 3.0f;
                sprite.colour[a] = colour_[a];
            }
            sprite.colour[3] = 1.0f;
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
