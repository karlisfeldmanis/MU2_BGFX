// The frame: eight views, in the order docs/conventions.md fixes them. Knows meshes and
// materials and nothing about the game — no map, no figure, no rules.
#pragma once

#include <string>
#include <vector>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "content/ground.h"
#include "content/mesh.h"
#include "gfx/effects.h"
#include "gfx/lighting.h"
#include "gfx/views.h"

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
    // False leaves it out of the reflection probe, as a posed figure always is. The viewer's
    // subject: the cube is taken 1.2 m over the camera's focus, which in the viewer is inside
    // the subject, and a cannon would reflect the inside of its own barrel. In the game the
    // cube is at the player, beside a thing and never in it. Per mesh, like `posed`: one
    // instance left out leaves out every instance of that mesh in the frame.
    bool inProbe = true;
};

// One point light, in world metres. The renderer knows nothing of lamps, torches or fires:
// what burns, and how it flickers, is `game`'s. docs/sprints/08a-the-lamps.md.
struct PointLight {
    float position[3] = {0.0f, 0.0f, 0.0f};
    // Metres on the GROUND: the light is zero this far out, measured flat. lights.sh.
    float reach = 1.0f;
    // How far above the ground under it the light hangs. Between the ground and this height
    // the falloff is flat distance alone, as MU's is; above or below, it falls off further.
    float height = 0.0f;
    float colour[3] = {1.0f, 1.0f, 1.0f};     // linear, before the flicker
};

class Renderer {
public:
    // `msaa` is 1, 2, 4 or 8 samples on the prepass, the depth and the shade target.
    // `shadowSize` is the sun's map, square, in texels.
    bool init(int width, int height, const std::string& shaderDir, int msaa,
              uint16_t shadowSize = 4096);
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

    // --- the shadow probe -------------------------------------------------------------
    // Where the sun's split stood this frame, measured against a grid fixed to the WORLD --
    // the sun's own axes through the origin -- and not against the split's own matrix, which
    // is the thing under test. A split snapped to its texels keeps the fractional part of
    // `texelX` and `texelY` the same every frame however the camera moves; one that crawls
    // shows it here as a phase that wanders, and one that pops as a phase that jumps. The
    // same for `depthQuanta`, in steps of the D16 map's own quantum: a depth that is not
    // snapped re-rounds every stored depth each frame, which is acne that flickers.
    struct SplitRecord {
        float texel = 0.0f;        // metres a shadow-map texel spans
        float texelX = 0.0f;       // the split's centre, in texels along the sun's x
        float texelY = 0.0f;       // and along its y
        float depthQuanta = 0.0f;  // the split's eye, in D16 steps along the sun
    };
    const SplitRecord& lastSplit() const { return split_; }
    // Moves the split's focus off the camera's by this many metres, and nothing else: the
    // camera, the figures and the light stay put. With the camera held this is the one test
    // in which any pixel that changes between two frames is the shadow's fault. tools/shimmer.py.
    void slideSplit(const float* metres) {
        for (int i = 0; i < 3; ++i) splitSlide_[i] = metres[i];
    }
    // The probe's two switches. `view` 1 draws the sun's visibility alone, as grey. `noise`
    // is where the penumbra's disc turn is anchored: 0 the screen, 1 the world's texel grid,
    // 2 nowhere, which is the default; a negative one keeps it. docs/shadow-probe.md.
    void setShadowDebug(int view, int noise) {
        shadowDebug_[0] = float(view);
        if (noise >= 0) shadowDebug_[1] = float(noise);
    }
    // The split's width this frame, in metres, which the fit may have chosen.
    float splitSide() const { return splitSide_; }

    // The transparent pass, which the renderer owns because the view it draws into is part
    // of the frame and not part of any one caller. A caller fills it between begin() and the
    // next draw(); draw() submits it between the shade and the tonemap and empties it.
    //
    // The renderer knows nothing about what a sprite MEANS -- no blow, no monster, no cue.
    // What lives, for how long, and on which cue belongs to `game`, and none of it is here.
    Effects& effects() { return effects_; }
    const Effects& effects() const { return effects_; }

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

    // --- the point lights ---------------------------------------------------------------
    // The static set, once, when a world opens: the lights never move, so which of them can
    // reach each 2 m cell of the ground is settled here and not per frame. `minX`, `minZ` and
    // `side` are the square of the world the grid covers, in metres. Null and zero clears it.
    // At most kMaxPointLights; a cell holds at most kLightsPerCell, the nearest ones, and the
    // log says when a cell wanted more.
    static constexpr uint32_t kMaxPointLights = 255;
    static constexpr int kLightsPerCell = 8;
    static constexpr float kLightCellMetres = 2.0f;
    void setPointLights(const PointLight* lights, uint32_t count, float minX, float minZ,
                        float side);
    // This frame's brightness of each, multiplying its colour: the flicker. `count` is the
    // set's own; uploaded inside draw(), 8 kB.
    void setPointLightLevels(const float* levels, uint32_t count);
    uint32_t pointLightCount() const { return lightCount_; }

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
        // A figure in a pose of its own, or anything else marked out of the probe. The probe
        // leaves these out: it is taken from the player's chest, and a cube taken from inside
        // him holds nothing but his armour's inside, which every piece of it would then reflect.
        bool posed = false;
    };

    bool createTargets(int width, int height);
    void destroyTargets();
    bool loadPrograms(const std::string& shaderDir);
    // `program` draws the static meshes and `skinnedProgram` the skinned ones. They are two
    // programs rather than one with a branch because the vertex layouts differ, and the
    // batches are already grouped by mesh, so which to use is decided once a batch and not
    // once a draw.
    //
    // `glowPass` picks which parts: false draws every part but the glows, which is what the
    // shadow, the prepass and the shade want -- a glow casts nothing, occludes nothing and is
    // not lit -- and true draws the glows alone, into the transparent view.
    void submitBatches(bgfx::ViewId view, bgfx::ProgramHandle program,
                       bgfx::ProgramHandle skinnedProgram, const std::vector<Batch>& batches,
                       const bgfx::InstanceDataBuffer& idb, uint64_t state, bool bindMaterial,
                       bool glowPass = false);
    // The shadow map and the AO, bound for ONE draw.
    //
    // bgfx::submit discards its bindings by default, so a texture bound once before a view's
    // draws reaches the first of them and no other. Set at the view level, as they were,
    // stages 4, 5 and 7 survived exactly one submit: the land's first surface had a shadow
    // and an AO, its other forty-three had neither, and every placement in the town sampled
    // an unbound stage -- which reads as shadow 0 and AO 0, so the whole town shaded black
    // whatever its albedo, its normals or its ORM said. Every draw that shades binds them
    // itself.
    //
    // And the frame's own uniforms with them, for the same reason one level up. A uniform
    // set once rides on the NEXT submit only, and the shade view sorts its draws by program:
    // every draw the sort put ahead of the one that carried the update ran on last frame's
    // values. Most of the time last frame's values are this frame's; on the frame the sun's
    // split stepped a texel, the town read the new map through the old matrix and every
    // shadow in it jumped for one frame -- a flicker at every texel the walk crossed. And
    // u_camPos a frame late is specular a frame late whenever the camera moves.
    // tools/shimmer.py found it; docs/shadow-probe.md.
    void bindShadeInputs();
    struct ShadeUniforms {
        float sunDir[4], sunColour[4], skyColour[4], groundColour[4], dust[4], camPos[4], params[4];
        float shadowMtx[16], shadowParams[4], shadowDebug[4], shadowReach[4];
    };
    ShadeUniforms shade_ = {};
    float lampParams_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float lampGridUniform_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float glowStrength_ = 1.0f;

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
    SplitRecord split_;
    float splitSlide_[3] = {0.0f, 0.0f, 0.0f};

    // The shadow map is square; its size is init()'s and fixed for the run.
    uint16_t shadowSize_ = 4096;
    // The split fitted to the camera's view: the hull from the eye to where the frustum meets
    // a plane `shadowFitBelow` under the target, seen from the sun. Its width is kept rather
    // than recomputed every frame, because a width that wobbles by a float is a texel that
    // wobbles, and a texel that changes size re-rasterises every edge -- the crawl the snap
    // exists to stop. It moves only when the fit moves by more than a fiftieth.
    void fitSplit(const Camera& camera, const Lighting& lighting, const float* worldAxes,
                  float* side, bx::Vec3* offset);
    float fittedSide_ = 0.0f;
    float splitSide_ = 0.0f;
    float shadowDebug_[2] = {0.0f, 2.0f};  // the still taps; shadow.sh says why

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
    // The bloom chain, sprint 8b: level 0 is half resolution, level 4 a thirty-second.
    bgfx::TextureHandle bloomTex_[kBloomLevels] = {};
    bgfx::FrameBufferHandle bloomFb_[kBloomLevels] = {};
    uint16_t bloomW_[kBloomLevels] = {}, bloomH_[kBloomLevels] = {};
    void bloom(const Lighting& lighting);

    // --- the reflection probe, sprint 8c ---------------------------------------------------
    // A cube round the player: one face drawn a frame with the shade program, the sky behind
    // it, and its chain; the frame after all six are new, the prefiltered copy a mip of
    // roughness at a time. Seven turns a cycle, on every other frame. The
    // shade pass reads the copy the next frame. docs/sprints/08c-the-metal.md.
    bool createProbe(const std::string& shaderDir);
    void destroyProbe();
    void drawProbe(const Camera& camera, const content::Ground* ground,
                   const std::vector<Batch>& batches, const bgfx::InstanceDataBuffer& idb);
    // A face's camera: 90 degrees square from `at`, turned so that the direction under each
    // of its texels is the one probe.sh's cubeDir names for that texel. Left-handed, because
    // a cube face is the mirror of what a right-handed camera sees; the probe's draws cull
    // the other winding to match.
    void probeFaceView(int face, const float* at, float* view, float* proj) const;
    void filterProbe();
    void chainProbeFace(int face);
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
    bgfx::ProgramHandle glowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bloomDownProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bloomUpProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinnedGlowProgram_ = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle uSunDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSunColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSkyColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGroundColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uDust_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uCamPos_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uMaterial_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uTranslucency_ = BGFX_INVALID_HANDLE;  // x: content::Material's
    bgfx::UniformHandle uShadowMtx_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uShadowParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uShadowDebug_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uShadowReach_ = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle uBloom_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uPresent_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uBloomTexel_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sBloom_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uLampGrid_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uLampParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sLamps_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sLampGrid_ = BGFX_INVALID_HANDLE;

    // The lights: a column each, row 0 position and reach, row 1 colour times level. And the
    // grid over the ground, two RGBA8 texels a cell. lights.sh reads both. Row 1's alpha is
    // the light's height, which the flicker never touches.
    bgfx::TextureHandle lamps_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle lampGrid_ = BGFX_INVALID_HANDLE;
    std::vector<float> lampCpu_;     // (kMaxPointLights + 1) x 2 texels x 4 floats
    std::vector<float> lampColour_;  // the colours before the flicker, three a light
    uint32_t lightCount_ = 0;
    bool lampsDirty_ = false;

    bgfx::TextureHandle palette_ = BGFX_INVALID_HANDLE;
    std::vector<float> paletteCpu_;  // kMaxPaletteRows x kMaxBones x 12
    int paletteWritten_ = 0;
    int paletteRefused_ = 0;

    bgfx::VertexBufferHandle screenVb_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout screenLayout_;

    Effects effects_;

    std::vector<Batch> batches_;
    std::vector<Batch> casterBatches_;

    // The probe. `probeRaw_` is what the faces draw into, one level; `probeFiltered_` is what
    // the shade pass reads, its mips written by fs_probe_filter.
    bgfx::TextureHandle probeRaw_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle probeDepth_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle probeFiltered_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle probeFaceFb_[6] = {
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle probeFilterFb_[6 * kProbeMips];
    // The raw cube's chain, a face at a time, by fs_probe_down. Its own texture, because a
    // level cannot be drawn while the level above it is read from the same one.
    bgfx::TextureHandle probeChain_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle probeChainFb_[6 * kProbeChain];
    bgfx::ProgramHandle probeDownProgram_ = BGFX_INVALID_HANDLE;
    // What a stage reads where there is no probe: a black cube and an occlusion of one. The
    // probe's own faces bind these, since a face cannot read the cube it is being drawn into
    // and has no SSAO of its own.
    bgfx::TextureHandle blackCube_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteAo_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle probeSkyProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle probeFilterProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uProbe_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uProbePos_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uProbeFace_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sProbe_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sSource_ = BGFX_INVALID_HANDLE;
    bool probeOk_ = false;       // everything above was made
    bool probeOn_ = false;       // the sheet wants it this frame
    float probeView_ = 0.0f;
    float metalGain_ = 1.0f;     // the sheet's metal_gain, into u_probe.w     // the sheet's check view: 0 off, else the mip shown plus one
    bool probeReady_ = false;    // a filtered cube exists to read
    bool probePass_ = false;     // the draws being submitted are a probe face's
    bool probeFilterDue_ = false;  // six new faces wait for the filter, next frame
    int probeNextFace_ = 0;
    uint32_t probeTick_ = 0;     // the probe works on even frames only; see drawProbe
    int probeFacesDrawn_ = 0;
    float probeAt_[3] = {0.0f, 0.0f, 0.0f};     // this face's eye
    float probeTaken_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // the eye when the copy was last filtered
    // Which winding the lit draws cull: CW for the camera, CCW inside a mirrored cube face.
    uint64_t cullBit_ = BGFX_STATE_CULL_CW;
};

}  // namespace mu::gfx
