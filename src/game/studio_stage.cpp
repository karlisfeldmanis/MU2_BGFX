#include "game/studio_stage.h"

#include <cmath>

#include <bx/math.h>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;

content::Vertex vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v,
                       float tx = 1.0f, float ty = 0.0f, float tz = 0.0f) {
    content::Vertex out{};
    out.position[0] = x, out.position[1] = y, out.position[2] = z;
    out.normal[0] = nx, out.normal[1] = ny, out.normal[2] = nz;
    out.tangent[0] = tx, out.tangent[1] = ty, out.tangent[2] = tz, out.tangent[3] = 1.0f;
    out.uv[0] = u, out.uv[1] = v;
    return out;
}

// A flat disc facing up, `y` high, counter-clockwise seen from above (bench.cpp's ground).
void disc(float radius, float y, std::vector<content::Vertex>& v, std::vector<uint32_t>& i) {
    constexpr int kSides = 96;
    v.push_back(vertex(0.0f, y, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f));
    for (int s = 0; s <= kSides; ++s) {
        const float a = 2.0f * kPi * float(s) / float(kSides);
        v.push_back(vertex(std::cos(a) * radius, y, std::sin(a) * radius, 0.0f, 1.0f, 0.0f,
                           0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)));
    }
    for (int s = 0; s < kSides; ++s) {
        i.insert(i.end(), {0u, uint32_t(s + 2), uint32_t(s + 1)});
    }
}

// The plinth's rim: a band from the floor up to its top, facing out.
void rim(float radius, float top, std::vector<content::Vertex>& v, std::vector<uint32_t>& i) {
    constexpr int kSides = 96;
    const uint32_t first = uint32_t(v.size());
    for (int s = 0; s <= kSides; ++s) {
        const float a = 2.0f * kPi * float(s) / float(kSides);
        const float c = std::cos(a), n = std::sin(a);
        v.push_back(vertex(c * radius, 0.0f, n * radius, c, 0.0f, n, float(s) / kSides, 1.0f, -n, 0.0f, c));
        v.push_back(vertex(c * radius, top, n * radius, c, 0.0f, n, float(s) / kSides, 0.0f, -n, 0.0f, c));
    }
    for (int s = 0; s < kSides; ++s) {
        const uint32_t a = first + uint32_t(s) * 2;
        i.insert(i.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
    }
}

void sphere(float radius, std::vector<content::Vertex>& v, std::vector<uint32_t>& i) {
    constexpr int kRings = 32, kSegments = 48;
    for (int r = 0; r <= kRings; ++r) {
        const float polar = kPi * float(r) / float(kRings);
        for (int s = 0; s <= kSegments; ++s) {
            const float a = 2.0f * kPi * float(s) / float(kSegments);
            const float x = std::sin(polar) * std::cos(a), y = std::cos(polar),
                        z = std::sin(polar) * std::sin(a);
            v.push_back(vertex(x * radius, y * radius + radius, z * radius, x, y, z,
                               float(s) / kSegments, float(r) / kRings, -std::sin(a), 0.0f,
                               std::cos(a)));
        }
    }
    for (int r = 0; r < kRings; ++r) {
        for (int s = 0; s < kSegments; ++s) {
            const uint32_t a = uint32_t(r * (kSegments + 1) + s), b = a + kSegments + 1;
            i.insert(i.end(), {a, a + 1, b, b, a + 1, b + 1});
        }
    }
}

// A square post from the floor to `height`, `half` a side's half.
void post(float half, float height, std::vector<content::Vertex>& v, std::vector<uint32_t>& i) {
    const float faces[4][3] = {{1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}};
    for (const auto& n : faces) {
        const uint32_t first = uint32_t(v.size());
        const float tx = -n[2], tz = n[0];   // along the face, round the post
        for (int corner = 0; corner < 4; ++corner) {
            const float side = (corner == 0 || corner == 3) ? -1.0f : 1.0f;
            const float y = corner < 2 ? 0.0f : height;
            v.push_back(vertex(n[0] * half + tx * side * half, y, n[2] * half + tz * side * half,
                               n[0], 0.0f, n[2], side * 0.5f + 0.5f, y / height, tx, 0.0f, tz));
        }
        i.insert(i.end(), {first, first + 2, first + 1, first, first + 3, first + 2});
    }
    const uint32_t top = uint32_t(v.size());
    for (int corner = 0; corner < 4; ++corner) {
        const float x = (corner == 1 || corner == 2) ? half : -half;
        const float z = corner >= 2 ? half : -half;
        v.push_back(vertex(x, height, z, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f));
    }
    i.insert(i.end(), {top, top + 2, top + 1, top, top + 3, top + 2});
}

}  // namespace

bgfx::TextureHandle StudioStage::grey(uint8_t value) {
    const uint8_t texel[4] = {value, value, value, 255};
    const bgfx::TextureHandle handle = bgfx::createTexture2D(
        1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_SRGB, bgfx::copy(texel, 4));
    owned_.push_back(handle);
    return handle;
}

bool StudioStage::open(content::Textures& textures) {
    if (open_) return true;
    // A surface of one grey, rough or not, metal or not. The albedos are sRGB bytes: 118 is
    // the 18% grey card, the floor a stop and a half under it, the plinth half a stop.
    auto surface = [&](const char* name, uint8_t value, float roughness, bool metal) {
        content::Material m;
        m.name = name;
        m.albedo = grey(value);
        m.normal = textures.flatNormal();
        m.orm = metal ? textures.ormOne() : textures.neutralOrm();
        m.emissive = textures.black();
        m.roughnessFactor = roughness;
        m.metalFactor = metal ? 1.0f : 0.0f;
        m.calibrated = metal;   // the chrome's albedo is its reflectance already
        return m;
    };
    auto build = [&](content::Mesh& mesh, const char* name, content::Material material,
                     std::vector<content::Vertex> v, std::vector<uint32_t> i) {
        content::Part part;
        part.firstIndex = 0;
        part.indexCount = uint32_t(i.size());
        part.material = 0;
        return mesh.build(name, std::move(v), std::move(i), {material}, {part});
    };
    std::vector<content::Vertex> v;
    std::vector<uint32_t> i;
    bool ok = true;
    disc(60.0f, 0.0f, v, i);
    ok &= build(floor_, "stage floor", surface("stage floor", 52, 0.9f, false), v, i);
    v.clear(), i.clear();
    disc(2.2f, 0.06f, v, i);
    rim(2.2f, 0.06f, v, i);
    ok &= build(plinth_, "stage plinth", surface("stage plinth", 92, 0.75f, false), v, i);
    v.clear(), i.clear();
    sphere(0.22f, v, i);
    ok &= build(ball_, "grey ball", surface("grey ball", 118, 0.6f, false), v, i);
    v.clear(), i.clear();
    sphere(0.22f, v, i);
    ok &= build(chrome_, "chrome ball", surface("chrome ball", 230, 0.05f, true), v, i);
    v.clear(), i.clear();
    post(0.025f, 1.8f, v, i);
    ok &= build(post_, "scale post", surface("scale post", 160, 0.8f, false), v, i);
    if (!ok) {
        core::logError("studio: the stage did not build");
        shutdown();
        return false;
    }
    open_ = true;
    return true;
}

void StudioStage::place(const float stand[3]) {
    drawables_.clear();
    // The studio's camera first looks down the (-1, -1) diagonal (bench.cpp's rest, 45
    // degrees), so its left and right are along (1, -1): the balls to one side of the
    // subject, the post to the other, both on the plinth and clear of a body's arms.
    const float side[2] = {0.7071f, -0.7071f};
    auto put = [&](const content::Mesh& mesh, float along, float up) {
        gfx::Drawable d;
        d.mesh = &mesh;
        bx::mtxTranslate(d.transform, stand[0] + side[0] * along, stand[1] + up,
                         stand[2] + side[1] * along);
        drawables_.push_back(d);
    };
    put(floor_, 0.0f, -0.002f);
    put(plinth_, 0.0f, -0.06f);
    put(ball_, 1.25f, 0.0f);
    put(chrome_, 1.75f, 0.0f);
    put(post_, -1.4f, 0.0f);
}

void StudioStage::shutdown() {
    floor_.shutdown();
    plinth_.shutdown();
    ball_.shutdown();
    chrome_.shutdown();
    post_.shutdown();
    for (bgfx::TextureHandle handle : owned_) {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
    owned_.clear();
    drawables_.clear();
    open_ = false;
}

}  // namespace mu::game
