// The Renderer coming up and going down: the device-side objects it owns for the whole run.
//
// init() and shutdown() are a matched pair and every handle created in one is destroyed in the
// other; createTargets()/destroyTargets() are the same pair for everything that depends on the
// backbuffer size, which is why a resize is the second pair run again and not the first.
// See gfx/renderer_internal.h for why this is one of five files.
#include "gfx/renderer.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "gfx/renderer_internal.h"
#include "gfx/views.h"

namespace mu::gfx {

bool Renderer::init(int width, int height, const std::string& shaderDir, int msaa,
                    uint16_t shadowSize, float scale) {
    msaa_ = msaa;
    shadowSize_ = shadowSize;
    // Held to a half at the bottom: below that the magnification is visible on a figure's
    // silhouette however good the filter is, and there is nothing to be gained by letting a
    // command line ask for a picture nobody would keep.
    scale_ = scale < 0.5f ? 0.5f : (scale > 1.0f ? 1.0f : scale);
    if (!loadPrograms(shaderDir)) return false;

    uSunDir_ = bgfx::createUniform("u_sunDir", bgfx::UniformType::Vec4);
    uSunColour_ = bgfx::createUniform("u_sunColour", bgfx::UniformType::Vec4);
    uSkyColour_ = bgfx::createUniform("u_skyColour", bgfx::UniformType::Vec4);
    uGroundColour_ = bgfx::createUniform("u_groundColour", bgfx::UniformType::Vec4);
    uDust_ = bgfx::createUniform("u_dust", bgfx::UniformType::Vec4);
    uEdge_ = bgfx::createUniform("u_edge", bgfx::UniformType::Vec4);
    uCamPos_ = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);
    uParams_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
    uMaterial_ = bgfx::createUniform("u_material", bgfx::UniformType::Vec4);
    uTranslucency_ = bgfx::createUniform("u_translucency", bgfx::UniformType::Vec4);
    uShadowMtx_ = bgfx::createUniform("u_shadowMtx", bgfx::UniformType::Mat4);
    uShadowParams_ = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    uShadowDebug_ = bgfx::createUniform("u_shadowDebug", bgfx::UniformType::Vec4);
    uShadowReach_ = bgfx::createUniform("u_shadowReach", bgfx::UniformType::Vec4);
    uCamRay_ = bgfx::createUniform("u_camRay", bgfx::UniformType::Vec4);
    uPrepassSize_ = bgfx::createUniform("u_prepassSize", bgfx::UniformType::Vec4);
    uGroundRepeat_ = bgfx::createUniform("u_groundRepeat", bgfx::UniformType::Vec4);
    uGroundBlend_ = bgfx::createUniform("u_groundBlend", bgfx::UniformType::Vec4);
    uGrassCard_ = bgfx::createUniform("u_grassCard", bgfx::UniformType::Vec4);
    uGrassWind_ = bgfx::createUniform("u_grassWind", bgfx::UniformType::Vec4);
    uGrassRoot_ = bgfx::createUniform("u_grassRoot", bgfx::UniformType::Vec4);
    uGrassTip_ = bgfx::createUniform("u_grassTip", bgfx::UniformType::Vec4);
    uGrassVary_ = bgfx::createUniform("u_grassVary", bgfx::UniformType::Vec4);
    uGrassSheet_ = bgfx::createUniform("u_grassSheet", bgfx::UniformType::Vec4);
    uGrassSize_ = bgfx::createUniform("u_grassSize", bgfx::UniformType::Vec4);
    uGrassReach_ = bgfx::createUniform("u_grassReach", bgfx::UniformType::Vec4);
    uGrassWalkers_ = bgfx::createUniform("u_grassWalkers", bgfx::UniformType::Vec4,
                                         GrassField::kMaxWalkers);
    sAlbedo2_ = bgfx::createUniform("s_albedo2", bgfx::UniformType::Sampler);
    uGrassSteps_ = bgfx::createUniform("u_grassSteps", bgfx::UniformType::Vec4,
                                       GrassField::kMaxSteps);
    uGrassWake_ = bgfx::createUniform("u_grassWake", bgfx::UniformType::Vec4);
    sNormal2_ = bgfx::createUniform("s_normal2", bgfx::UniformType::Sampler);
    sOrm2_ = bgfx::createUniform("s_orm2", bgfx::UniformType::Sampler);

    sAlbedo_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    sNormal_ = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
    sOrm_ = bgfx::createUniform("s_orm", bgfx::UniformType::Sampler);
    sEmissive_ = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
    sShadowCompare_ = bgfx::createUniform("s_shadowCompare", bgfx::UniformType::Sampler);
    sShadowDepth_ = bgfx::createUniform("s_shadowDepth", bgfx::UniformType::Sampler);
    sPrepass_ = bgfx::createUniform("s_prepass", bgfx::UniformType::Sampler);
    sAo_ = bgfx::createUniform("s_ao", bgfx::UniformType::Sampler);
    sColour_ = bgfx::createUniform("s_colour", bgfx::UniformType::Sampler);
    sBones_ = bgfx::createUniform("s_bones", bgfx::UniformType::Sampler);
    uBloom_ = bgfx::createUniform("u_bloom", bgfx::UniformType::Vec4);
    uPresent_ = bgfx::createUniform("u_present", bgfx::UniformType::Vec4);
    uGrade_ = bgfx::createUniform("u_grade", bgfx::UniformType::Vec4);
    uTintLow_ = bgfx::createUniform("u_tintLow", bgfx::UniformType::Vec4);
    uTintHigh_ = bgfx::createUniform("u_tintHigh", bgfx::UniformType::Vec4);
    uBloomTexel_ = bgfx::createUniform("u_bloomTexel", bgfx::UniformType::Vec4);
    sBloom_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    uLampGrid_ = bgfx::createUniform("u_lampGrid", bgfx::UniformType::Vec4);
    uLampParams_ = bgfx::createUniform("u_lampParams", bgfx::UniformType::Vec4);
    // An array uniform, and its length has to be the shader's: lights.sh declares [4] and
    // bgfx refuses a handle whose num disagrees.
    uTransientAt_ = bgfx::createUniform("u_transientAt", bgfx::UniformType::Vec4,
                                        kMaxTransientLights);
    uTransientColour_ = bgfx::createUniform("u_transientColour", bgfx::UniformType::Vec4,
                                           kMaxTransientLights);
    sLamps_ = bgfx::createUniform("s_lamps", bgfx::UniformType::Sampler);
    sLampGrid_ = bgfx::createUniform("s_lampGrid", bgfx::UniformType::Sampler);

    // The point lights, empty until a world sets them. Both textures exist from the start so
    // stages 13 and 14 are never unbound: u_lampParams.y is 0 and nothing reads them, but an
    // unbound stage on Metal is a validation error waiting for the first shader that does.
    const uint64_t point = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    lampCpu_.assign(size_t(kMaxPointLights + 1) * 2 * 4, 0.0f);
    lamps_ = bgfx::createTexture2D(uint16_t(kMaxPointLights + 1), 2, false, 1,
                                   bgfx::TextureFormat::RGBA32F, point);
    const uint8_t none[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    lampGrid_ = bgfx::createTexture2D(2, 1, false, 1, bgfx::TextureFormat::RGBA8, point,
                                      bgfx::copy(none, sizeof(none)));

    // The bone palette: three texels a bone across, one figure a row down. RGBA32F because
    // a pose is a matrix and a half float loses the translation at Lorencia's scale. 512
    // rows is 1.6 MB resident and holds Lorencia's whole crowd -- 290 monsters and the
    // fourteen townsfolk -- with room over; what is uploaded each frame is the rows that
    // were written, not the texture.
    palette_ = bgfx::createTexture2D(uint16_t(kMaxBones * 3), uint16_t(kMaxPaletteRows), false,
                                     1, bgfx::TextureFormat::RGBA32F,
                                     BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP |
                                         BGFX_SAMPLER_V_CLAMP);
    paletteCpu_.assign(size_t(kMaxPaletteRows) * kMaxBones * 12, 0.0f);
    if (!bgfx::isValid(palette_)) {
        core::logError("the bone palette did not survive creation; no figure will be posed");
    }

    screenLayout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    // One triangle over the screen rather than two: no seam down the diagonal, and one fewer
    // vertex. v runs top-down, as Metal's origin does.
    static const float tri[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
         3.0f, -1.0f, 0.0f, 2.0f, 1.0f,
        -1.0f,  3.0f, 0.0f, 0.0f, -1.0f,
    };
    screenVb_ = bgfx::createVertexBuffer(bgfx::makeRef(tri, sizeof(tri)), screenLayout_);

    // The transparent pass. Built here and not on first use, which is Pool.cs' lesson out of
    // MU2: the four pools that built themselves on demand hitched on the first blow, the
    // first number, the first swing and the first level -- the four most conspicuous moments
    // in a fight. A pass that fails to build is reported and the frame goes on without it,
    // because a town with no blood is a picture and a town with no frame is not.
    if (!effects_.init(shaderDir)) {
        core::logError("the transparent pass did not build; effects will not draw");
    }

    // The probe is not a render target of the window's size, so it is made once and not on a
    // resize. A probe that did not build is said, and the shade pass keeps the closed-form sky.
    probeOk_ = createProbe(shaderDir);
    if (!probeOk_) core::logError("the reflection probe did not build; metal reflects the sky alone");

    // Not a render target of the window's size either, and for the same reason: a fixed
    // square, made once. A run that fails to build it plays on with no ring at all.
    outlineOk_ = createOutline(shaderDir);
    if (!outlineOk_) core::logError("the hover outline did not build; nothing will ring");

    return createTargets(width, height);
}

bool Renderer::loadPrograms(const std::string& dir) {
    shadowProgram_ = loadProgram(dir, "vs_depth", "fs_shadow");
    prepassProgram_ = loadProgram(dir, "vs_static", "fs_prepass");
    ssaoProgram_ = loadProgram(dir, "vs_screen", "fs_ssao");
    blurProgram_ = loadProgram(dir, "vs_screen", "fs_blur");
    ssaoMsProgram_ = loadProgram(dir, "vs_screen", "fs_ssao_ms");
    blurMsProgram_ = loadProgram(dir, "vs_screen", "fs_blur_ms");
    shadeProgram_ = loadProgram(dir, "vs_static", "fs_shade");
    skinnedShadowProgram_ = loadProgram(dir, "vs_skinned_depth", "fs_shadow");
    skinnedPrepassProgram_ = loadProgram(dir, "vs_skinned", "fs_prepass");
    skinnedShadeProgram_ = loadProgram(dir, "vs_skinned", "fs_shade");
    presentProgram_ = loadProgram(dir, "vs_screen", "fs_present");
    groundShadowProgram_ = loadProgram(dir, "vs_ground_depth", "fs_shadow");
    groundPrepassProgram_ = loadProgram(dir, "vs_ground", "fs_ground_prepass");
    groundShadeProgram_ = loadProgram(dir, "vs_ground", "fs_ground");
    // One program, and one pass. The field is drawn in the shade pass alone and lays its own
    // depth there; see Renderer::draw for why it is not in the prepass. Not required: a town
    // with no grass in it is the town this engine drew until now, and it says so.
    grassShadeProgram_ = loadProgram(dir, "vs_grass", "fs_grass");
    if (!bgfx::isValid(grassShadeProgram_)) {
        core::logError("the grass program did not link; the land goes without its grass");
    }
    // Not required: a frame without its glows is a picture, and says so.
    bloomDownProgram_ = loadProgram(dir, "vs_screen", "fs_bloom_down");
    bloomUpProgram_ = loadProgram(dir, "vs_screen", "fs_bloom_up");
    if (!bgfx::isValid(bloomDownProgram_) || !bgfx::isValid(bloomUpProgram_)) {
        core::logError("the bloom programs did not link; the frame goes without it");
    }
    glowProgram_ = loadProgram(dir, "vs_static", "fs_glow");
    skinnedGlowProgram_ = loadProgram(dir, "vs_skinned", "fs_glow");
    if (!bgfx::isValid(glowProgram_) || !bgfx::isValid(skinnedGlowProgram_)) {
        core::logError("the glow programs did not link; MU's BlendMeshes will not draw");
    }
    {
        const std::pair<const char*, bgfx::ProgramHandle> all[] = {
            {"shadow", shadowProgram_},   {"prepass", prepassProgram_},
            {"ssao", ssaoProgram_},       {"ssaoMs", ssaoMsProgram_},
            {"blur", blurProgram_},       {"blurMs", blurMsProgram_},
            {"shade", shadeProgram_},     {"present", presentProgram_},
            {"skinnedShadow", skinnedShadowProgram_},
            {"skinnedPrepass", skinnedPrepassProgram_},
            {"skinnedShade", skinnedShadeProgram_},
            {"groundShadow", groundShadowProgram_},
            {"groundPrepass", groundPrepassProgram_},
            {"groundShade", groundShadeProgram_}};
        for (const auto& one : all) {
            if (!bgfx::isValid(one.second)) core::logError("program %s did not link", one.first);
        }
    }
    const bool ok = bgfx::isValid(shadowProgram_) && bgfx::isValid(prepassProgram_) &&
                    bgfx::isValid(ssaoProgram_) && bgfx::isValid(blurProgram_) &&
                    bgfx::isValid(ssaoMsProgram_) && bgfx::isValid(blurMsProgram_) &&
                    bgfx::isValid(shadeProgram_) && bgfx::isValid(presentProgram_) &&
                    bgfx::isValid(groundShadowProgram_) && bgfx::isValid(groundPrepassProgram_) &&
                    bgfx::isValid(groundShadeProgram_) && bgfx::isValid(skinnedShadowProgram_) &&
                    bgfx::isValid(skinnedPrepassProgram_) && bgfx::isValid(skinnedShadeProgram_);
    if (!ok) core::logError("the frame is missing a program; nothing will draw");
    return ok;
}

bool Renderer::createTargets(int width, int height) {
    // What comes in is the backbuffer's size; what the world is drawn at is that times the
    // scale, rounded to an even pair of numbers so the half-resolution SSAO target is exactly
    // half of it and the bloom chain halves cleanly rather than losing a column a level.
    outWidth_ = width;
    outHeight_ = height;
    width_ = std::max(2, (int(std::lround(width * scale_)) / 2) * 2);
    height_ = std::max(2, (int(std::lround(height * scale_)) / 2) * 2);
    if (scale_ < 1.0f) {
        core::logf("the world is drawn at %dx%d and presented at %dx%d (scale %.2f)", width_,
                   height_, outWidth_, outHeight_, double(scale_));
    }
    width = width_;
    height = height_;
    const uint16_t w = uint16_t(width);
    const uint16_t h = uint16_t(height);
    const uint16_t hw = uint16_t(std::max(1, width / 2));
    const uint16_t hh = uint16_t(std::max(1, height / 2));

    const uint64_t rt = BGFX_TEXTURE_RT;
    const uint64_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    // MSAA on the three targets that carry geometry: the prepass, the depth they share, and
    // the shade target. Not on the shadow map, where more samples buy nothing a wider filter
    // does not, and not on the half-resolution SSAO, which has no edges of its own.
    //
    // The samples are never read individually. bgfx resolves an MSAA render target to a
    // single sample when it is sampled as a texture, because BGFX_TEXTURE_MSAA_SAMPLE is not
    // asked for -- so the SSAO reads a resolved prepass and the present reads a resolved
    // shade target, and on a tile-based GPU both resolves happen in tile memory.
    uint64_t msaaFlag = 0;
    switch (msaa_) {
        case 2: msaaFlag = BGFX_TEXTURE_RT_MSAA_X2; break;
        case 4: msaaFlag = BGFX_TEXTURE_RT_MSAA_X4; break;
        case 8: msaaFlag = BGFX_TEXTURE_RT_MSAA_X8; break;
        default: msaaFlag = 0; break;
    }

    // The shadow map is read twice: through the compare sampler for the filtered result,
    // and as plain depth to find the blocker. docs/conventions.md.
    shadowMap_ = bgfx::createTexture2D(shadowSize_, shadowSize_, false, 1,
                                       bgfx::TextureFormat::D16, rt | clamp);
    shadowFb_ = bgfx::createFrameBuffer(1, &shadowMap_, true);

    // View normal and view depth in one target. RGBA16F because the depth is in world units
    // and Lorencia is 25 600 of them across: a half float runs out of precision at that
    // range, so what is stored is the distance from the eye, which stays small.
    // MSAA_SAMPLE keeps bgfx from resolving it: the SSAO reads one sample itself, because
    // an averaged normal across a silhouette is not a normal. Nothing else reads this target.
    const uint64_t prepassMsaa = msaaFlag ? (msaaFlag | BGFX_TEXTURE_MSAA_SAMPLE) : 0;
    prepassColour_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F,
                                           rt | prepassMsaa | clamp);
    // Write only, which is what it is: the depth is an attachment the prepass writes and the
    // shade pass tests EQUAL against, and nothing ever samples it. bgfx requires the flag
    // outright once the texture is multisampled -- "a frame buffer depth MSAA texture cannot
    // be resolved" -- and without it both the prepass and the shade buffer are refused while
    // every texture in them reports valid.
    sceneDepth_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F,
                                        rt | msaaFlag | BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle prepassAttachments[] = {prepassColour_, sceneDepth_};
    prepassFb_ = bgfx::createFrameBuffer(2, prepassAttachments, false);

    ssaoTex_ = bgfx::createTexture2D(hw, hh, false, 1, bgfx::TextureFormat::R8, rt | clamp);
    ssaoFb_ = bgfx::createFrameBuffer(1, &ssaoTex_, true);
    blurTex_ = bgfx::createTexture2D(hw, hh, false, 1, bgfx::TextureFormat::R8, rt | clamp);
    blurFb_ = bgfx::createFrameBuffer(1, &blurTex_, true);

    // The shade pass shares the prepass's depth so it can test EQUAL against it. That
    // sharing is the whole reason nothing is shaded twice.
    shadeColour_ =
        bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, rt | msaaFlag | clamp);
    bgfx::TextureHandle shadeAttachments[] = {shadeColour_, sceneDepth_};
    shadeFb_ = bgfx::createFrameBuffer(2, shadeAttachments, false);

    // The bloom chain: half, quarter ... a thirty-second, linear filtered so each tap of the
    // shaders is itself a 2x2 average. RGBA16F because what it holds is HDR.
    for (int i = 0; i < kBloomLevels; ++i) {
        bloomW_[i] = uint16_t(std::max(1, width >> (i + 1)));
        bloomH_[i] = uint16_t(std::max(1, height >> (i + 1)));
        bloomTex_[i] = bgfx::createTexture2D(bloomW_[i], bloomH_[i], false, 1,
                                             bgfx::TextureFormat::RGBA16F, rt | clamp);
        bloomFb_[i] = bgfx::createFrameBuffer(1, &bloomTex_[i], true);
        if (!bgfx::isValid(bloomFb_[i])) core::logError("bloom level %d did not survive", i);
    }

    // Named one by one, because "a render target did not survive" is not a thing anyone can
    // act on, and a format this Metal refuses is exactly the kind of thing that lands here.
    const std::pair<const char*, bool> made[] = {
        {"shadow map", bgfx::isValid(shadowMap_)},
        {"shadow buffer", bgfx::isValid(shadowFb_)},
        {"prepass colour", bgfx::isValid(prepassColour_)},
        {"scene depth", bgfx::isValid(sceneDepth_)},
        {"prepass buffer", bgfx::isValid(prepassFb_)},
        {"ssao buffer", bgfx::isValid(ssaoFb_)},
        {"blur buffer", bgfx::isValid(blurFb_)},
        {"shade colour", bgfx::isValid(shadeColour_)},
        {"shade buffer", bgfx::isValid(shadeFb_)},
    };
    bool ok = true;
    for (const auto& [what, valid] : made) {
        if (valid) continue;
        core::logError("the %s did not survive creation at %dx%d with %dx msaa", what, width,
                       height, msaa_);
        ok = false;
    }
    core::logf("targets %dx%d, %dx msaa, shadow %u, ssao %dx%d", width, height, msaa_,
               unsigned(shadowSize_), hw, hh);
    return ok;
}

void Renderer::destroyTargets() {
    for (bgfx::FrameBufferHandle* fb : {&shadowFb_, &prepassFb_, &ssaoFb_, &blurFb_, &shadeFb_}) {
        if (bgfx::isValid(*fb)) bgfx::destroy(*fb);
        *fb = BGFX_INVALID_HANDLE;
    }
    // Each level's buffer owns its texture.
    for (int i = 0; i < kBloomLevels; ++i) {
        if (bgfx::isValid(bloomFb_[i])) bgfx::destroy(bloomFb_[i]);
        bloomFb_[i] = BGFX_INVALID_HANDLE;
        bloomTex_[i] = BGFX_INVALID_HANDLE;
    }
    // The textures a frame buffer was told to own went with it; these were not.
    for (bgfx::TextureHandle* t : {&prepassColour_, &sceneDepth_, &shadeColour_}) {
        if (bgfx::isValid(*t)) bgfx::destroy(*t);
        *t = BGFX_INVALID_HANDLE;
    }
    shadowMap_ = BGFX_INVALID_HANDLE;
    ssaoTex_ = BGFX_INVALID_HANDLE;
    blurTex_ = BGFX_INVALID_HANDLE;
}

void Renderer::resize(int width, int height) {
    if (width == outWidth_ && height == outHeight_) return;
    destroyTargets();
    createTargets(width, height);
}

void Renderer::shutdown() {
    destroyTargets();
    destroyProbe();
    destroyOutline();
    effects_.shutdown();
    if (bgfx::isValid(palette_)) bgfx::destroy(palette_);
    palette_ = BGFX_INVALID_HANDLE;
    for (bgfx::TextureHandle* t : {&lamps_, &lampGrid_}) {
        if (bgfx::isValid(*t)) bgfx::destroy(*t);
        *t = BGFX_INVALID_HANDLE;
    }
    for (bgfx::ProgramHandle* p : {&shadowProgram_, &prepassProgram_, &ssaoProgram_, &blurProgram_,
                                   &ssaoMsProgram_, &blurMsProgram_, &shadeProgram_,
                                   &presentProgram_, &groundShadowProgram_,
                                   &groundPrepassProgram_, &groundShadeProgram_,
                                   &skinnedShadowProgram_, &skinnedPrepassProgram_,
                                   &skinnedShadeProgram_, &glowProgram_, &skinnedGlowProgram_,
                                   &grassShadeProgram_,
                                   &bloomDownProgram_, &bloomUpProgram_}) {
        if (bgfx::isValid(*p)) bgfx::destroy(*p);
        *p = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* u :
         {&uSunDir_, &uSunColour_, &uSkyColour_, &uGroundColour_, &uDust_, &uEdge_, &uCamPos_, &uParams_,
          &uMaterial_, &uTranslucency_, &uShadowMtx_, &uShadowParams_, &uShadowDebug_, &uShadowReach_, &uCamRay_, &uPrepassSize_, &uGroundRepeat_, &uGroundBlend_, &uGrassCard_, &uGrassWind_, &uGrassRoot_, &uGrassTip_, &uGrassVary_, &uGrassSheet_, &uGrassSize_, &uGrassReach_, &uGrassWalkers_, &sAlbedo2_, &sNormal2_, &sOrm2_, &sAlbedo_,
          &sNormal_, &sOrm_, &sEmissive_, &sShadowCompare_, &sShadowDepth_, &sPrepass_, &sAo_,
          &uGrassSteps_, &uGrassWake_,
          &sColour_, &sBones_, &uLampGrid_, &uLampParams_, &uTransientAt_, &uTransientColour_, &sLamps_, &sLampGrid_, &uBloom_, &uPresent_, &uGrade_, &uTintLow_, &uTintHigh_, &uBloomTexel_,
          &sBloom_}) {
        if (bgfx::isValid(*u)) bgfx::destroy(*u);
        *u = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(screenVb_)) bgfx::destroy(screenVb_);
    screenVb_ = BGFX_INVALID_HANDLE;
}

}  // namespace mu::gfx
