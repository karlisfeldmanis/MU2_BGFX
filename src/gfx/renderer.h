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
    // How much of this is there, 0 to 1. Below 1 it is not in the opaque passes at all: it is
    // drawn after them, its own depth first and then shaded and blended at this opacity, which
    // is what stops a half-there figure showing its own far side through itself. Its shadow is
    // dithered by the same number (fs_shadow). The character's fade-in when the game has
    // loaded, and it works on a figure or on the weapon in its hand.
    float fade = 1.0f;
    // False leaves it out of the reflection probe, as a posed figure always is. The viewer's
    // subject: the cube is taken 1.2 m over the camera's focus, which in the viewer is inside
    // the subject, and a cannon would reflect the inside of its own barrel. In the game the
    // cube is at the player, beside a thing and never in it. Per mesh, like `posed`: one
    // instance left out leaves out every instance of that mesh in the frame.
    bool inProbe = true;
};

// The near field's grass, ready to draw. The renderer knows nothing of maps, tiles or where
// grass grows: `game`'s Grass walks the land and fills this, and what arrives here is one
// buffer of blade strips, one instance per square metre, and the look's own numbers.
//
// Two draws come out of it, the prepass and the shade pass, off ONE vertex shader. The shade
// pass tests depth EQUAL against the prepass, so the two passes must place a blade at exactly
// the same point; two shaders that compute the same thing are not the same thing, and the
// only way to be certain is for them to be the same compiled code. docs/grass.md.
//
// Never the shadow pass. MU's own grass casts nothing, and a field of blades in the sun's
// split is a shadow map full of noise.
struct GrassField {
    // At most this many painted sheets in one field. MU loads three grass sheets for a map
    // and a world's tile_slots names at most that many TileGrass entries; four is one spare.
    static constexpr int kMaxSheets = 4;

    // One sheet's worth of the field: a run of the instance buffer, and the picture its cards
    // are cut out of. Separate draws rather than an atlas because MU's sheets are separate
    // pictures of different heights, and because at two or three of them a draw each is
    // cheaper than the uv arithmetic an atlas would put in every vertex.
    struct Batch {
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
        uint32_t first = 0;
        uint32_t count = 0;
        // The sheet's own size in texels. Per axis, and it must be: MU's sheets are 256 by 64
        // and 256 by 128, and a mip level worked out from the width alone is two whole levels
        // of blur on the short axis. That was the first version of this and it is what made
        // the field look smeared. docs/grass.md.
        float width = 256.0f;
        float height = 64.0f;
    };

    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    // Allocated out of bgfx's own transient store during the gather, so it is good for this
    // frame and no longer. One patch each: see game/world/grass.cpp for what is in it.
    bgfx::InstanceDataBuffer instances{};
    Batch batches[kMaxSheets];
    int batchCount = 0;

    float card[4] = {0.42f, 0.30f, 0.10f, 1.3f};  // height m, width over height, lean, widening
    float wind[4] = {1.0f, 0.0f, 0.10f, 0.0f};    // direction xz, strength, seconds
    float root[4] = {0.54f, 0.72f, 0.40f, 0.62f}; // the colour at the root, then the AO there
    float tip[4] = {0.94f, 1.12f, 0.58f, 0.45f};  // the colour at the top, then the roughness
    float colour = 0.26f;  // how far the sheet is graded towards those two
    // cards a patch, the stratification grid's side, the rank share, how dry a dry tuft goes
    float vary[4] = {36.0f, 6.0f, 0.085f, 0.16f};
    // columns in the sheet, the alpha the cutout tests, a sharpening bias on the mip level
    // (negative is sharper), and 1 when this draw is the meadow rather than the sward. The
    // sheet's own size rides per batch, above.
    float sheet[4] = {4.0f, 0.28f, -0.4f, 0.0f};
    // How far the field reaches, measured from the EYE and worked out per card in the vertex
    // shader: x is the metres past which no card stands, y the band before that over which a
    // card shrinks into the turf, z where the thinning with distance begins and w where it
    // has taken all it takes. From the eye, not the focus, because MU's camera is rigid to the
    // player: a distance from the eye is a place on the SCREEN, so every band this describes
    // sits still in the frame as the player walks and nothing crosses it. docs/grass.md.
    float reach[4] = {26.0f, 4.0f, 9.0f, 24.0f};
    // The walkers: the feet of whoever stands in the field, world metres, and in w how far
    // round each the sward is pushed aside. w at 0 is an empty slot, which is what a world
    // with nobody in it sends in all of them. Eight, which is the hero and the seven nearest
    // of the crowd; a played frame draws about nine bodies.
    static constexpr int kMaxWalkers = 8;
    float walkers[kMaxWalkers * 4] = {};
    // And where they have been: the footprints, (x, z, the second it was laid, the angle
    // they were walking in), so the grass they walked through is still getting up behind
    // them. A slot whose laid second is far in the past is empty. Turf kept ten for one
    // walker; this keeps a ring for all of them. The clock they are aged against is
    // wind.w, the run's seconds.
    static constexpr int kMaxSteps = 24;
    float steps[kMaxSteps * 4] = {};
    // What the whole wake sits inside: (centre x, centre z, radius, the seconds now). A card
    // further from that centre than the radius skips the footprints altogether, which is every
    // card in the field but the few hundred behind somebody's heels -- the loop is 24 iterations
    // on every vertex of every card otherwise. Turf measured the same bound for the same
    // reason. A radius of 0 is no wake at all, which is what a measuring run sends.
    float wake[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // --- the meadow -------------------------------------------------------------------
    // What a lawn has that a field has not: a seed head standing over the blades, a broad leaf
    // lying under them, clover, daisies, buttercups, bellflowers. Eight painted cells of them
    // in `assets/effects/grass/wild.png`, which MU2 invented and painted (pipeline/meadow.py)
    // and which nothing in this engine read until now.
    //
    // It is the SAME patches and the SAME instance buffer -- a flower does not care which grass
    // sheet its tile wears -- drawn a second time with its own sheet, its own size and a short
    // index range, so only the first few cards of each patch become plants. One more draw.
    Batch meadow;
    float meadowCard[4] = {0.34f, 0.5f, 0.06f, 0.6f};  // height m, width over height, lean, widening
    float meadowVary[4] = {9.0f, 3.0f, 0.0f, 0.0f};    // cards, stratification side, rank, dry
    float meadowSheet[4] = {8.0f, 0.28f, -0.4f, 1.0f}; // eight cells; the 1 says "meadow"
    float meadowDensity = 0.0f;    // of the cards it is offered, how many become plants
    uint32_t meadowIndices = 0;    // how much of the index buffer the meadow draw covers
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

    // The clock every time-animated material reads: the fountain's water and the other
    // scrolling submeshes (content::Material::scrollPerSecond), and the cloud shadow's slide.
    //
    // Handed in by the Application once a frame rather than read off bx::getHPCounter() here,
    // which is what this used to do. The wall clock made the PICTURE depend on how long the
    // process had been alive -- so it swept up the preloader's own duration, which varies with
    // the disk cache, and it ignored --fixed-dt entirely. Two runs of the same pinned command
    // therefore drew the fountain differently, by 3.5% of the pixels of a 640x360 frame, and
    // that is what stopped a run from being reproducible. It is seconds of play now, starting
    // at 0 on the first frame, so --fixed-dt reaches the water like it reaches everything else.
    void setClock(float seconds) { elapsed_ = seconds; }

    // The whole frame. `drawables` may hold the same mesh many times; `ground` may be null.
    //
    // `casters` is what the sun's split draws, and it is a SEPARATE list on purpose. A
    // chunk behind the camera still casts into the frame, so culling the shadow pass with
    // the camera's frustum removes the shadow of whatever is just off screen -- the bug
    // foundation 7 of PLAN.md names. Null means the camera's own list casts, which is right
    // only when nothing was culled out of it.
    // `grass` may be null, and is null on every bench and every stage: a field of blades is
    // the town's, not a subject's.
    void draw(const Camera& camera, const Lighting& lighting,
              const std::vector<Drawable>& drawables, const content::Ground* ground,
              const std::vector<Drawable>* casters = nullptr, const GrassField* grass = nullptr);

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
    // How much the colour is drained out of the WORLD, 0 none and 1 grey. The interface is
    // drawn in its own pass afterwards and is untouched, which is the whole point: the game
    // goes grey while he is down and the HUD he is reading does not. It multiplies the sheet's
    // own saturation rather than replacing it, so a world graded flat stays flat.
    void setDrain(float drain) { drain_ = drain < 0.0f ? 0.0f : (drain > 1.0f ? 1.0f : drain); }

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

    // --- the item stages, sprint 7 ------------------------------------------------------
    // A window's item pictures: `drawables` photographed into `target` through `view` and
    // `proj`, lit by the stage's own room (fs_stage.sc) and written as sRGB for the interface.
    // Nothing of the frame's -- no sun, split, AO or probe -- reaches it. Opened once after
    // init, closed before shutdown. gfx/stage_pass.cpp.
    bool openStages(const std::string& shaderDir);
    void closeStages();
    void drawStage(bgfx::ViewId viewId, bgfx::FrameBufferHandle target, uint16_t width,
                   uint16_t height, const float* view, const float* proj,
                   const std::vector<Drawable>& drawables);

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

    // The lights that MOVE, and are gone again: a burning meteor on its way down, and whatever
    // else an effect wants to light the town with. Set once a frame, before draw(); null or a
    // count of 0 turns them off, and the shader's loop then does not run.
    //
    // They are deliberately NOT in the grid above. That grid is binned once when a world opens
    // precisely because the lamps never move, and a light that travels would have the whole map
    // walked again every frame -- 16 384 cells of Lorencia against four lights, to save a loop
    // of four. So these are a uniform array every lit pixel reads, and 4 is the cap the loop is
    // affordable at. Above it the extra are dropped, loudly, rather than silently truncated.
    //
    // They are packed exactly as a static light is and lit by the same function in lights.sh --
    // same flat-on-the-ground distance, same `(1 - d^2/R^2)^2` falloff, same lamp_strength --
    // so a torch and a meteor of equal reach light a wall identically.
    static constexpr uint32_t kMaxTransientLights = 4;
    void setTransientLights(const PointLight* lights, uint32_t count);
    uint32_t transientLightCount() const { return transientCount_; }

    // The view and projection this renderer will use for that camera, so that whoever culls
    // against the frustum culls against the SAME frustum that is drawn. Handedness and the
    // depth range are decided in one place only; a second copy of these two calls elsewhere
    // is how a cull starts disagreeing with the picture.
    void cameraMatrices(const Camera& camera, float* view, float* proj) const;

    // --- the hover outline ---------------------------------------------------------------
    // MU2's Outline.cs: a gold silhouette round whatever the pointer is over. Ported from a
    // Godot SubViewport mask and a CanvasItem shader to a small offscreen mask and a bgfx
    // screen pass; see game/outline.cpp for the box that is fitted round the hovered thing
    // and the priority (a townsperson, then a drop, then a monster) that picks it, both of
    // which are game rules and not the renderer's.
    struct OutlineParams {
        // The hovered thing's own box on the real screen, in backbuffer pixels: both what
        // the mask is fitted to (up to kOutlineMaskSize) and where the ring composes.
        int screenX = 0, screenY = 0, screenW = 0, screenH = 0;
        // A dropped item's soft shadow under the ring. False for a monster or a townsperson,
        // which already stand on their own cast shadow -- see Outline.Shade in the C# this
        // was ported from.
        bool shadow = false;
    };
    // How wide the ring is and how far its box must be grown to hold it, in pixels of the
    // real screen -- shared with game/outline.cpp's own box fit so the two agree on how much
    // room the ring needs without the literal being written twice.
    static constexpr float kOutlineWidth = 2.6f;
    static constexpr float kOutlineReach = 7.0f;  // the drop shadow's own further reach
    // The mask's own cap, pixels on a side. MU's camera keeps a hovered thing under a few
    // hundred pixels, so one fixed target never reallocates; unlike Godot's SubViewport,
    // which resized in 64-pixel steps to the exact box, this is simpler at the cost of a
    // thing approached close enough to fill more of the screen than this being clipped at
    // the cap's edge rather than losing precision -- a camera this close to a monster or an
    // item is not how MU is played.
    static constexpr int kOutlineMaskSize = 512;
    // Draws the ring: `hovered`'s own meshes into their own tiny mask (the SAME instances
    // draw() already posed this frame, submitted a second time -- no re-skinning, matching
    // Outline.cs's "the same mesh, drawn into both, in the same pose"), then a screen pass
    // that composes it into `params`'s box of the backbuffer. Call after draw() and before
    // the HUD submits: MU2's ring sits over the world and under the windows. Does nothing
    // when `hovered` is empty or `params`'s box is empty.
    void drawOutline(const float* mainView, const Camera& camera, const OutlineParams& params,
                     const std::vector<Drawable>& hovered);

private:
    // See setDrain: 0 is the world as the sheet grades it, 1 is grey.
    float drain_ = 0.0f;

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
    // MU's world clock, which MoveObject scrolls a glow's additive submesh off: seconds of
    // PLAY, free-running so two glow models at different rates never fall out of step with
    // each other, the way "every placement of a type moves in step" requires. See
    // content::Material::scrollPerSecond, and setClock() for why it is not the wall clock.
    float elapsed_ = 0.0f;

    void screenPass(bgfx::ViewId view, bgfx::ProgramHandle program);
    // The land. Its own vertex layout and its own shader: it blends two full material sets
    // by a per-vertex weight and carries MU's baked light, which the closed material model
    // has no room for. docs/conventions.md.
    // One instanced draw per painted sheet, once a frame, in the shade pass. See GrassField.
    void submitGrass(bgfx::ViewId view, bgfx::ProgramHandle program, const GrassField& grass,
                     uint64_t state);

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
    bgfx::ProgramHandle grassShadeProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle glowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bloomDownProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bloomUpProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinnedGlowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle stageProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinnedStageProgram_ = BGFX_INVALID_HANDLE;

    // --- the hover outline, sprint 9 ----------------------------------------------------
    // The mask: one fixed kOutlineMaskSize square target, R8 -- coverage is all it holds,
    // and the shadow pass's own program writes it (fs_shadow honours a cutout and writes
    // 1.0, which is exactly a silhouette). No depth attachment: nothing here is ever blended,
    // so a later part of the same thing simply overwrites an earlier one's 1.0 with its own.
    bool createOutline(const std::string& shaderDir);
    void destroyOutline();
    bgfx::TextureHandle outlineMaskTex_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle outlineMaskFb_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle outlineProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uOutlineEdge_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uOutlineParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uOutlinePixel_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uOutlineDrift_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uOutlineScale_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sOutlineMask_ = BGFX_INVALID_HANDLE;
    bool outlineOk_ = false;
    std::vector<Batch> outlineBatches_;

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
    bgfx::UniformHandle uGrassCard_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassWind_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassRoot_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassTip_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassVary_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassSheet_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassSize_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassReach_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassWalkers_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sAlbedo2_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sNormal2_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassSteps_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrassWake_ = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle uGrade_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uTintLow_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uTintHigh_ = BGFX_INVALID_HANDLE;
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

    // The movers: the same two vec4s a static light occupies in the lamp texture's two rows,
    // position and reach then colour and height, so lights.sh reads one shape either way.
    // The count rides in u_lampParams.z, which was spare, rather than in a uniform of its own:
    // one more uniform is one more upload on every shaded draw in the frame.
    float transientAt_[kMaxTransientLights * 4] = {};
    float transientColour_[kMaxTransientLights * 4] = {};
    uint32_t transientCount_ = 0;
    bgfx::UniformHandle uTransientAt_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uTransientColour_ = BGFX_INVALID_HANDLE;

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
