// The frame: six views, in the order docs/conventions.md fixes them. Knows meshes and
// materials and nothing about the game — no map, no figure, no rules.
#pragma once

#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/ground.h"
#include "content/mesh.h"
#include "gfx/lighting.h"

namespace mu::gfx {

struct Camera {
    float position[3] = {0, 0, 0};
    float target[3] = {0, 0, 0};
    float up[3] = {0, 1, 0};
    float fovDegrees = 30.0f;
    float nearPlane = 10.0f;
    float farPlane = 20000.0f;
};

// One thing to draw: a mesh and where it stands. Instances of the same mesh become one draw.
struct Drawable {
    const content::Mesh* mesh = nullptr;
    float transform[16];
    // MU's baked terrain light where this instance stands, which multiplies the albedo.
    // White for anything that has none -- the bench's own models, and any world without a
    // light map.
    float light[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // Which row of the bone palette this instance's pose is in, or -1 for a static mesh.
    // Five worn parts of one character share a row: wearing is swapping which meshes draw
    // against one set of bone rows, and the skeleton does not know what it has on.
    int paletteRow = -1;
};

class Renderer {
public:
    // `msaa` is 1, 2, 4 or 8 samples on the prepass, the depth and the shade target.
    bool init(int width, int height, const std::string& shaderDir, int msaa);
    void shutdown();
    void resize(int width, int height);

    // The whole frame. `drawables` may hold the same mesh many times; `ground` may be null.
    //
    // `casters` is what the sun's split draws, and it is a SEPARATE list on purpose. A
    // chunk behind the camera still casts into the frame, so culling the shadow pass with
    // the camera's frustum removes the shadow of whatever is just off screen -- the bug
    // foundation 7 of PLAN.md names. Null means the camera's own list casts, which is right
    // only when nothing was culled out of it.
    void draw(const Camera& camera, const Lighting& lighting,
              const std::vector<Drawable>& drawables, const content::Ground* ground,
              const std::vector<Drawable>* casters = nullptr);

    uint32_t lastDrawCount() const { return drawCount_; }

    // --- the bone palette -------------------------------------------------------------
    // One texture holds every figure's pose for the frame: a row a figure, three RGBA32F
    // texels a bone. The game fills a row and remembers its number; the drawables that wear
    // that pose carry the number. Nothing here knows what a figure is.
    //
    // `resetPalettes` is called once at the top of a frame, `addPalette` once per posed
    // figure, and the upload happens inside draw().
    //
    // **Row 0 is the bind row** and is written by resetPalettes: kMaxBones identities. A
    // skinned mesh whose drawable names no row draws against it, which is its bind pose --
    // the town's twenty sway models, and a bow whose own clip is not built yet. Without it a
    // row of -1 reaches the shader as a texelFetch outside the texture, every bone comes back
    // as zeroes, and the mesh collapses into a point at the origin: which is how the
    // fountain's spout, the trees, the signs and the curtains disappeared out of Lorencia the
    // moment the cook started writing them as skinned.
    static constexpr int kBindRow = 0;
    // 128 and not 64: Storage01, which stands in the town, has 69 bones, and the lobby's
    // faces have 100 to 115. A rig that does not fit is clamped and says so, and a figure
    // with half a palette is a figure with a hand left in the bind pose -- visible, where a
    // silent refusal would have looked like a missing clip.
    static constexpr int kMaxBones = 128;
    static constexpr int kMaxPaletteRows = 512;
    void resetPalettes();
    // `rows12` is `bones` lots of twelve floats -- three rows of a 4x3, already transposed by
    // core::writePaletteRows. Returns the row, or -1 when the palette is full, which the
    // caller must treat as "draw this in bind pose" rather than as a reason to stop.
    int addPalette(const float* rows12, int bones);
    int paletteRowsUsed() const { return paletteWritten_; }
    // How many figures asked for a row this frame and were refused one. They draw in bind
    // pose, which is a picture, so nothing fails -- but a frame where the crowd quietly
    // stopped being posed must be visible in a number, as foundation 7 says of culling.
    int paletteRowsRefused() const { return paletteRefused_; }

    // The view and projection this renderer will use for that camera, so that whoever culls
    // against the frustum culls against the SAME frustum that is drawn. Handedness and the
    // depth range are decided in one place only; a second copy of these two calls elsewhere
    // is how a cull starts disagreeing with the picture.
    void cameraMatrices(const Camera& camera, float* view, float* proj) const;

private:
    struct Batch {
        const content::Mesh* mesh = nullptr;
        uint32_t first = 0;   // into the frame's instance buffer
        uint32_t count = 0;
    };

    bool createTargets(int width, int height);
    void destroyTargets();
    bool loadPrograms(const std::string& shaderDir);
    // `program` draws the static meshes and `skinnedProgram` the skinned ones. They are two
    // programs rather than one with a branch because the vertex layouts differ, and the
    // batches are already grouped by mesh, so which to use is decided once a batch and not
    // once a draw.
    void submitBatches(bgfx::ViewId view, bgfx::ProgramHandle program,
                       bgfx::ProgramHandle skinnedProgram, const std::vector<Batch>& batches,
                       const bgfx::InstanceDataBuffer& idb, uint64_t state, bool bindMaterial);
    // The shadow map and the AO, bound for ONE draw.
    //
    // bgfx::submit discards its bindings by default, so a texture bound once before a view's
    // draws reaches the first of them and no other. Set at the view level, as they were,
    // stages 4, 5 and 7 survived exactly one submit: the land's first surface had a shadow
    // and an AO, its other forty-three had neither, and every placement in the town sampled
    // an unbound stage -- which reads as shadow 0 and AO 0, so the whole town shaded black
    // whatever its albedo, its normals or its ORM said. Every draw that shades binds them
    // itself.
    void bindShadeInputs();

    void screenPass(bgfx::ViewId view, bgfx::ProgramHandle program);
    // The land. Its own vertex layout and its own shader: it blends two full material sets
    // by a per-vertex weight and carries MU's baked light, which the closed material model
    // has no room for. docs/conventions.md.
    void submitGround(bgfx::ViewId view, bgfx::ProgramHandle program, const content::Ground& g,
                      uint64_t state, bool lit);

    int width_ = 0;
    int height_ = 0;
    int msaa_ = 1;
    uint32_t drawCount_ = 0;

    // The shadow map is square and fixed; a split framed on the camera does not want to
    // change size with the window.
    static constexpr uint16_t kShadowSize = 2048;

    bgfx::FrameBufferHandle shadowFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowMap_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle prepassFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle prepassColour_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sceneDepth_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle ssaoFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ssaoTex_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle blurFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blurTex_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadeFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadeColour_ = BGFX_INVALID_HANDLE;

    bgfx::ProgramHandle shadowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle prepassProgram_ = BGFX_INVALID_HANDLE;
    // The same three passes again, for meshes that carry a skin. Only the vertex program
    // differs; the fragment side is shared, and vs_skinned declares the identical varyings
    // so that it can be.
    bgfx::ProgramHandle skinnedShadowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinnedPrepassProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinnedShadeProgram_ = BGFX_INVALID_HANDLE;
    // Two variants each, chosen by the sample count: the multisampled twin reads one
    // sample of the prepass rather than an average of them. See common.sh's prepassAt.
    bgfx::ProgramHandle ssaoProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle ssaoMsProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle blurProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle blurMsProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shadeProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle presentProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle groundShadowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle groundPrepassProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle groundShadeProgram_ = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle uSunDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSunColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSkyColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGroundColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uCamPos_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uMaterial_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uShadowMtx_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uShadowParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uCamRay_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uPrepassSize_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGroundRepeat_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGroundBlend_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sAlbedo2_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sNormal2_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sOrm2_ = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle sAlbedo_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sNormal_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sOrm_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sEmissive_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sShadowCompare_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sShadowDepth_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sPrepass_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sAo_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sBones_ = BGFX_INVALID_HANDLE;

    bgfx::TextureHandle palette_ = BGFX_INVALID_HANDLE;
    std::vector<float> paletteCpu_;  // kMaxPaletteRows x kMaxBones x 12
    int paletteWritten_ = 0;
    int paletteRefused_ = 0;

    bgfx::VertexBufferHandle screenVb_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout screenLayout_;

    std::vector<Batch> batches_;
    std::vector<Batch> casterBatches_;
};

}  // namespace mu::gfx
