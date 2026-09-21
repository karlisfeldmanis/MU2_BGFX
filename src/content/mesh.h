// A .glb into buffers bgfx can draw. Static only for now; the skinned layout arrives with
// the figures in sprint 4.
#pragma once

#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"

namespace mu::content {

struct Vertex {
    float position[3];
    float normal[3];
    float tangent[4];  // w is the bitangent's sign, as glTF has it
    float uv[2];
};
static_assert(sizeof(Vertex) == 48, "the vertex layout drifted");

// The same 48 bytes with the skin on the end: four joint bytes and four normalised weight
// bytes, 56. One byte holds the largest rig in this content twice over, and the joints reach
// the shader as `uvec4` -- Metal reads an unsigned byte attribute into an unsigned type and
// refuses the pipeline for an `ivec4`, silently drawing nothing. docs/conventions.md.
struct SkinnedVertex {
    float position[3];
    float normal[3];
    float tangent[4];
    float uv[2];
    uint8_t joints[4];
    uint8_t weights[4];
};
static_assert(sizeof(SkinnedVertex) == 56, "the skinned vertex layout drifted");
static_assert(sizeof(SkinnedVertex) == sizeof(CookedSkinnedVertex),
              "the skinned vertex disagrees with the cook's");

// One bone: where it hangs and how it undoes the bind pose. The order is the skin's own, and
// a parent always precedes its child, so a pose is one walk of the array.
struct Bone {
    std::string name;
    int32_t parent = -1;
    float inverseBind[16];
};

// One material, closed: four maps and three flags, and nothing else. docs/conventions.md.
struct Material {
    bgfx::TextureHandle albedo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normal = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle orm = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle emissive = BGFX_INVALID_HANDLE;
    // Below this alpha a pixel is discarded, in every pass. Negative means no cutout, which
    // is the whole test: a cutout is decided when the model is read, not guessed per pixel.
    float cutout = -1.0f;
    bool twoSided = false;
    // MU's BlendMesh: drawn ADDED to the frame in the transparent pass, and nowhere else --
    // not in the sun's split, not in the prepass, not in the shade. The fourth flag the
    // closed model gained in sprint 8a, and it costs the walls nothing: a glow is its own
    // program in its own view, not a variant of fs_shade. docs/sprints/08a-the-lamps.md.
    bool glow = false;
    // Whether the albedo's metal is already reflectance. MU2's item bake lifts a metal's
    // painted texels onto the metal's own f0 and writes the result as `<item>_basecolor`
    // (build_maps.base_colour); the world's tiled path does not, and its iron is the dark
    // paint MU drew. The shade's metal_gain exists for the second kind, and applied to the
    // first it lifted the armour twice -- the Plate set's metal reflected 0.6 to 0.9, with
    // up to half its texels at the cap. True leaves the gain off.
    bool calibrated = false;
    // glTF's own scalars, and the shader multiplies the ORM by them: roughness = orm.g * this,
    // metal = orm.b * this. A surface whose relief came out of its art has a map and both
    // factors at 1.0; a surface whose material declares no grain -- MU's foliage, grass and
    // water -- has no map at all and carries its whole answer here. Ignoring them drew 195 of
    // this content's material slots at the missing-map fallback, water at roughness 1 among
    // them.
    float roughnessFactor = 1.0f;
    float metalFactor = 1.0f;
    // Light that comes through a leaf rather than off it: MU2's pipeline writes the leaf's
    // own sheet as its emissive, at this fraction, so a canopy's shaded side shows some of
    // itself instead of black. Not a light source -- the shade scales it by the sun and sky
    // the scene has, so it goes out at night. 0 on everything that is not foliage, and then
    // the emissive map is a real emission.
    float translucency = 0.0f;
    // MoveObject's BlendMeshTexCoordV: how many times a second the glow material's one
    // additive submesh slides down its V axis. 0 on every material that does not scroll --
    // the fires, the candles, the lit windows that only flicker -- and set on the three that
    // do: the waterspout's fall, House04's and House05's lit windows. Only glow (above) ever
    // carries a nonzero one. See fs_glow.sc and Renderer::submitBatches.
    float scrollPerSecond = 0.0f;
    std::string name;
};

// One draw: a range of indices sharing a material.
struct Part {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t material = 0;
};

struct Bounds {
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    float centre[3] = {0, 0, 0};
    float radius = 0.0f;
};

class Mesh {
public:
    bool load(const std::string& path, Textures& textures);

    // A mesh the cook already flattened: no glTF parser, no buffer walk, and the textures
    // named by path rather than embedded. `assetDir` is what those paths are relative to.
    bool buildFromCooked(const CookedMesh& cooked, const std::string& name,
                         const std::string& assetDir, Textures& textures);

    // A mesh made rather than read: the bench's ground, and later the terrain's chunks.
    // Takes the vectors, works out the bounds, and makes the buffers.
    bool build(const std::string& name, std::vector<Vertex> vertices,
               std::vector<uint32_t> indices, std::vector<Material> materials,
               std::vector<Part> parts);

    // The same, with a skin on it. A skinned mesh differs from a static one in exactly two
    // ways the rest of the engine can see: this layout, and the bone table below.
    bool buildSkinned(const std::string& name, std::vector<SkinnedVertex> vertices,
                      std::vector<uint32_t> indices, std::vector<Material> materials,
                      std::vector<Part> parts, std::vector<Bone> bones);

    void shutdown();

    const std::vector<Part>& parts() const { return parts_; }
    const std::vector<Material>& materials() const { return materials_; }
    bgfx::VertexBufferHandle vertexBuffer() const { return vbh_; }
    bgfx::IndexBufferHandle indexBuffer() const { return ibh_; }
    const Bounds& bounds() const { return bounds_; }
    uint32_t triangleCount() const { return indexCount_ / 3; }
    uint32_t vertexCount() const { return vertexCount_; }
    const std::string& name() const { return name_; }
    const std::vector<Bone>& bones() const { return bones_; }
    bool isSkinned() const { return !bones_.empty(); }

    // The layout every static draw uses. Valid after the first Mesh::load in the process.
    static const bgfx::VertexLayout& layout();
    // And the one every skinned draw uses.
    static const bgfx::VertexLayout& skinnedLayout();

private:
    bool finish(const void* vertices, uint32_t count, size_t stride,
                const bgfx::VertexLayout& layout, std::vector<uint32_t> indices);

    std::string name_;
    std::vector<Part> parts_;
    std::vector<Material> materials_;
    std::vector<Bone> bones_;
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    uint32_t indexCount_ = 0;
    uint32_t vertexCount_ = 0;
    Bounds bounds_;
};

}  // namespace mu::content
