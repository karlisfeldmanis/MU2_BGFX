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
    // MoveObject's BlendMeshTexCoordU instead: the Lost Tower's slotted walls and machines,
    // whose red Chrome01 streams sideways behind a band that stays put. `scrollAlongU` slides
    // U rather than V; `maskHeld` samples the sheet's alpha at its own place while its colour
    // slides, so the stream moves and the band it shows through does not. The cook's bit 7.
    bool scrollAlongU = false;
    // And along both, for an opaque stream (MU's StreamMesh sliding U and V at once: Tarkan's
    // whirlpool). The cook's scroll mode bit 6.
    bool scrollAlongUV = false;
    bool maskHeld = false;
    // MU's BITMAP_WATER instead of a slide: a mesh whose sheet is wt00 steps through the 32
    // caustic frames one a reference frame, as the ground's pools do (ZzzBMD.cpp:1322-1325) --
    // Atlans's Object24. The sheet is the 8 by 4 caustic atlas and `scrollPerSecond` the frame
    // rate; fs_glow cuts the frame's cell out of it. The cook's mode bit 2.
    bool waterFrames = false;
    // MU's alpha test AND blend on a cut-out (EnableAlphaTest, ZzzOpenglUtil.cpp:366-393): cut
    // below `cutout`, and above it seen through by the sheet's own alpha -- the Bahamut's fin
    // membrane. Out of the prepass and the shade; drawn after the opaque frame, depth first and
    // then blended (Renderer::draw, view 5). Shadows keep the plain cut. The cook's mode bit 4.
    bool softAlpha = false;
    // An item's glow as ItemObjectAttribute sets it (ZzzObject.cpp:5199): its brightness
    // `sin(WorldTime*0.004)*pulse[0] + pulse[1]` -- the Light Spear's and the two shields'
    // breathing, 0/1 on a steady glow -- and `jitter`, the step of the per-frame random jump
    // of its sheet, the Legendary Shield's shimmer (MU jumps U and V in tenths; here V only,
    // carried where the scroll is). The cook's bit 5; see Renderer::submitBatches.
    // A negative pulse[1] is not a sine: it is MU's per-frame roll, (rand()%10)*0.1 times
    // -pulse[1] -- the Gorgon's eye (Renderer::submitBatches).
    float pulse[2] = {0.0f, 1.0f};
    // And at MU's WorldTime*0.001 rather than 0.004: the 2nd wings' slow breath (the cooked
    // scroll mode's bit 3, docs/second-wings.md).
    bool slowPulse = false;
    // A glow MU keeps a levelled item's chrome and metal passes off: its NoneBlendMesh
    // (ZzzBMD.cpp:1425), the Staff of Resurrection's swirl cards and the Light Saber's beam.
    // Every other item glow takes them in fs_glow. The cook's mode bit 5.
    bool noChrome = false;
    float jitter = 0.0f;
    // An item's glow rather than a lamp's: drawn at MU's BlendMeshLight alone, without the
    // world sheet's glow_strength, which is tuned for fires and windows (2.0 in Lorencia).
    bool itemGlow = false;
    // An item's glow whose sheet never moves is sampled clamped, as MU loads every model
    // sheet (LoadBitmap's GL_CLAMP_TO_EDGE, ZzzTexture.h:15). Tiled, the Light Saber's
    // beam, black at its tip and green at its hilt end, bled the hilt's green round onto the
    // tip, and its three crossed quads drew a star above the blade. A sliding or jumping
    // sheet keeps tiling: it is the wrap that moves it.
    bool glowClamped() const { return itemGlow && scrollPerSecond == 0.0f && jitter == 0.0f; }
    // A glow that casts the sun's shadow all the same, though it is drawn only in the glow
    // pass, at this strength: 0 none, 1 full, between dithered by fs_shadow as a fading figure
    // is. Ours, on the Ice Monster alone: MU draws it no shadow (ZzzCharacter.cpp:8668), and
    // its additive body could not be seen on Devias's snow without one (the user, 2026-09-30).
    float glowShadow = 0.0f;
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
    // How far the farthest vertex stands from the line through the centre along each axis:
    // twice this is the widest the mesh can be turned about that axis, which the bounds'
    // diagonal overstates for anything round -- a mace's head is a disc, not a square.
    float reach[3] = {0, 0, 0};
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

    // The arrow or bolt drawn on a bow's or crossbow's string -- its "nocked" material -- as
    // its two ends in the bind pose and the bone that carries it, so the one that flies can
    // leave from where this one was drawn (Figure::nocked). Ends in no order; `bone` -1 when
    // the mesh has no such part.
    struct Nock {
        int bone = -1;
        float ends[2][3] = {{0, 0, 0}, {0, 0, 0}};
    };
    const Nock& nock() const { return nock_; }

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
    Nock nock_;
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    uint32_t indexCount_ = 0;
    uint32_t vertexCount_ = 0;
    Bounds bounds_;
};

}  // namespace mu::content
