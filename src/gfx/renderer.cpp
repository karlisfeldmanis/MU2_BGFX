#include "gfx/renderer.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

#include "core/files.h"
#include "core/log.h"
#include "gfx/views.h"

namespace mu::gfx {
namespace {

bgfx::ShaderHandle loadShader(const std::string& dir, const char* name) {
    // bgfx_compile_shaders keeps the source's own extension: vs_depth.sc becomes
    // vs_depth.sc.bin, not vs_depth.bin.
    const std::string path = dir + "/" + name + ".sc.bin";
    std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) return BGFX_INVALID_HANDLE;
    const bgfx::Memory* mem = bgfx::copy(bytes.data(), uint32_t(bytes.size()));
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    if (bgfx::isValid(handle)) bgfx::setName(handle, name);
    return handle;
}

bgfx::ProgramHandle loadProgram(const std::string& dir, const char* vs, const char* fs) {
    bgfx::ShaderHandle v = loadShader(dir, vs);
    bgfx::ShaderHandle f = loadShader(dir, fs);
    if (!bgfx::isValid(v) || !bgfx::isValid(f)) {
        core::logError("the program %s/%s did not load", vs, fs);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(v, f, true);
}

}  // namespace

bool Renderer::init(int width, int height, const std::string& shaderDir, int msaa,
                    uint16_t shadowSize) {
    msaa_ = msaa;
    shadowSize_ = shadowSize;
    if (!loadPrograms(shaderDir)) return false;

    uSunDir_ = bgfx::createUniform("u_sunDir", bgfx::UniformType::Vec4);
    uSunColour_ = bgfx::createUniform("u_sunColour", bgfx::UniformType::Vec4);
    uSkyColour_ = bgfx::createUniform("u_skyColour", bgfx::UniformType::Vec4);
    uGroundColour_ = bgfx::createUniform("u_groundColour", bgfx::UniformType::Vec4);
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
    sAlbedo2_ = bgfx::createUniform("s_albedo2", bgfx::UniformType::Sampler);
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
    uBloomTexel_ = bgfx::createUniform("u_bloomTexel", bgfx::UniformType::Vec4);
    sBloom_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    uLampGrid_ = bgfx::createUniform("u_lampGrid", bgfx::UniformType::Vec4);
    uLampParams_ = bgfx::createUniform("u_lampParams", bgfx::UniformType::Vec4);
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
    width_ = width;
    height_ = height;
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
    if (width == width_ && height == height_) return;
    destroyTargets();
    createTargets(width, height);
}

void Renderer::shutdown() {
    destroyTargets();
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
                                   &bloomDownProgram_, &bloomUpProgram_}) {
        if (bgfx::isValid(*p)) bgfx::destroy(*p);
        *p = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* u :
         {&uSunDir_, &uSunColour_, &uSkyColour_, &uGroundColour_, &uCamPos_, &uParams_,
          &uMaterial_, &uTranslucency_, &uShadowMtx_, &uShadowParams_, &uShadowDebug_, &uShadowReach_, &uCamRay_, &uPrepassSize_, &uGroundRepeat_, &uGroundBlend_, &sAlbedo2_, &sNormal2_, &sOrm2_, &sAlbedo_,
          &sNormal_, &sOrm_, &sEmissive_, &sShadowCompare_, &sShadowDepth_, &sPrepass_, &sAo_,
          &sColour_, &sBones_, &uLampGrid_, &uLampParams_, &sLamps_, &sLampGrid_, &uBloom_, &uBloomTexel_,
          &sBloom_}) {
        if (bgfx::isValid(*u)) bgfx::destroy(*u);
        *u = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(screenVb_)) bgfx::destroy(screenVb_);
    screenVb_ = BGFX_INVALID_HANDLE;
}

void Renderer::bindShadeInputs() {
    bgfx::setUniform(uSunDir_, shade_.sunDir);
    bgfx::setUniform(uSunColour_, shade_.sunColour);
    bgfx::setUniform(uSkyColour_, shade_.skyColour);
    bgfx::setUniform(uGroundColour_, shade_.groundColour);
    bgfx::setUniform(uCamPos_, shade_.camPos);
    bgfx::setUniform(uParams_, shade_.params);
    bgfx::setUniform(uShadowMtx_, shade_.shadowMtx);
    bgfx::setUniform(uShadowParams_, shade_.shadowParams);
    bgfx::setUniform(uShadowDebug_, shade_.shadowDebug);
    bgfx::setUniform(uShadowReach_, shade_.shadowReach);
    bgfx::setTexture(4, sShadowCompare_, shadowMap_,
                     BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(5, sShadowDepth_, shadowMap_,
                     BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(7, sAo_, blurTex_);
    bgfx::setUniform(uLampGrid_, lampGridUniform_);
    bgfx::setUniform(uLampParams_, lampParams_);
    bgfx::setTexture(13, sLamps_, lamps_);
    bgfx::setTexture(14, sLampGrid_, lampGrid_);
}

void Renderer::fitSplit(const Camera& camera, const Lighting& lighting, const float* worldAxes,
                        float* side, bx::Vec3* offset) {
    // Relative to the target throughout, so the answer depends on the camera's shape and not
    // on where in the world it stands.
    const bx::Vec3 eye(camera.position[0] - camera.target[0],
                       camera.position[1] - camera.target[1],
                       camera.position[2] - camera.target[2]);
    const bx::Vec3 forward = bx::normalize(bx::neg(eye));
    const bx::Vec3 right =
        bx::normalize(bx::cross(forward, bx::Vec3(camera.up[0], camera.up[1], camera.up[2])));
    const bx::Vec3 upward = bx::cross(right, forward);
    const float tanY = std::tan(camera.fovDegrees * 0.5f * 3.14159265f / 180.0f);
    const float tanX = tanY * float(width_) / float(std::max(1, height_));
    const float plane = -lighting.shadowFitBelow;

    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    auto take = [&](const bx::Vec3& p) {
        // worldAxes' translation runs along the sun, so x and y are the rotation's alone.
        const bx::Vec3 l = bx::mul(p, worldAxes);
        lo[0] = std::min(lo[0], l.x);
        hi[0] = std::max(hi[0], l.x);
        lo[1] = std::min(lo[1], l.y);
        hi[1] = std::max(hi[1], l.y);
    };
    take(eye);
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            const bx::Vec3 ray = bx::add(forward, bx::add(bx::mul(right, float(sx) * tanX),
                                                          bx::mul(upward, float(sy) * tanY)));
            // A corner that looks level or up never meets the ground, and there is no hull
            // to fit: the fixed square it was given stands.
            if (ray.y > -1e-3f) return;
            take(bx::add(eye, bx::mul(ray, (plane - eye.y) / ray.y)));
        }
    }
    // A twentieth over, for the filter's reach past the box's own edge.
    const float wanted = std::max(hi[0] - lo[0], hi[1] - lo[1]) * 1.05f;
    if (fittedSide_ <= 0.0f || std::fabs(wanted - fittedSide_) > 0.02f * fittedSide_) {
        fittedSide_ = wanted;
        core::logf("shadow split fitted to the camera: %.1f m, %.1f mm a texel", fittedSide_,
                   1000.0f * fittedSide_ / float(shadowSize_));
    }
    *side = fittedSide_;

    // The box's centre, back out of the sun's axes into the world.
    float fromLight[16];
    bx::mtxInverse(fromLight, worldAxes);
    const bx::Vec3 centre((lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, 0.0f);
    *offset = bx::sub(bx::mul(centre, fromLight), bx::mul(bx::Vec3(0.0f, 0.0f, 0.0f), fromLight));
}

void Renderer::resetPalettes() {
    // Row 0 is the bind row, and it is written every frame rather than once at init because
    // the upload below sends only the rows this frame wrote: a row written once at start-up
    // would never be uploaded at all.
    paletteWritten_ = 0;
    paletteRefused_ = 0;
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float rows[12];
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 4; ++row) rows[column * 4 + row] = identity[row * 4 + column];
    }
    for (int bone = 0; bone < kMaxBones; ++bone) {
        std::memcpy(&paletteCpu_[size_t(bone) * 12], rows, sizeof(rows));
    }
    paletteWritten_ = 1;
}

int Renderer::addPalette(const float* rows12, int bones) {
    if (paletteWritten_ >= kMaxPaletteRows) {
        ++paletteRefused_;
        return -1;
    }
    const int row = paletteWritten_++;
    // A rig too big for the palette is clamped rather than refused: the bones past the end
    // stay whatever the row held, which is visible, where a silent return of -1 would put
    // the whole figure in bind pose and look like a missing clip. Nothing in this content
    // has more than 60; the lobby's faces, at 100 to 115, would be the first.
    // A caller that hands over more rows than fit is clamped here as well as at the pose,
    // because this is the one that owns the texture's width.
    if (bones > kMaxBones) bones = kMaxBones;
    std::memcpy(&paletteCpu_[size_t(row) * kMaxBones * 12], rows12,
                size_t(bones) * 12 * sizeof(float));
    return row;
}

void Renderer::submitBatches(bgfx::ViewId view, bgfx::ProgramHandle program,
                             bgfx::ProgramHandle skinnedProgram,
                             const std::vector<Batch>& batches, const bgfx::InstanceDataBuffer& idb,
                             uint64_t state, bool bindMaterial, bool glowPass) {
    for (const Batch& batch : batches) {
        const content::Mesh& mesh = *batch.mesh;
        const bool skinned = mesh.isSkinned();
        const bgfx::ProgramHandle batchProgram = skinned ? skinnedProgram : program;
        for (const content::Part& part : mesh.parts()) {
            const content::Material& material = mesh.materials()[part.material];
            // A glow is drawn in its own pass and in no other. See the header.
            if (material.glow != glowPass) continue;

            // A cutout discards in every pass, this one included, or a leaf casts a card.
            // z and w are glTF's roughness and metal factors, which the shade pass multiplies
            // the ORM by: a material with no ORM map carries its whole answer there. A glow
            // has neither, and its z is the sheet's glow_strength instead.
            const float materialParams[4] = {material.cutout, material.twoSided ? 1.0f : 0.0f,
                                             glowPass ? glowStrength_ : material.roughnessFactor,
                                             material.metalFactor};
            bgfx::setUniform(uMaterial_, materialParams);
            // The albedo is bound even in the depth passes, because the cutout reads its alpha.
            bgfx::setTexture(0, sAlbedo_, material.albedo);
            if (bindMaterial) {
                bgfx::setTexture(1, sNormal_, material.normal);
                bgfx::setTexture(2, sOrm_, material.orm);
                bgfx::setTexture(3, sEmissive_, material.emissive);
                const float translucency[4] = {material.translucency, 0.0f, 0.0f, 0.0f};
                bgfx::setUniform(uTranslucency_, translucency);
                bindShadeInputs();
            }

            uint64_t drawState = state;
            // MU's figures are single sheets of mixed winding and are drawn two-sided. A glow
            // always is: MU draws its BlendMesh with culling off, and a flame card seen from
            // behind is still a flame.
            if (!material.twoSided && !glowPass) drawState |= BGFX_STATE_CULL_CW;
            // No alpha to coverage here, deliberately. It was set for a while and did
            // nothing: coverage comes from gl_FragColor.a and every pass writes 1.0 or a
            // depth, so the mask was always full. Making it real is not one line -- the
            // prepass and the shade pass must compute the SAME mask or the shade pass's
            // DEPTH_TEST_EQUAL leaves the samples it drops unshaded and every leaf grows a
            // black fringe, and the prepass has no channel free for coverage until its
            // normal is packed octahedrally. That is sprint 3's work, with the grass that
            // needs it and can test it. See docs/conventions.md.

            // Per draw, like the shadow map and the AO above and for the same reason:
            // bgfx::submit discards its bindings, so a palette bound once a view reaches
            // the first figure and leaves every other one reading an unbound stage -- which
            // is a pose of zeroes, and a figure collapsed into a point at the origin.
            if (skinned) bgfx::setTexture(12, sBones_, palette_);

            bgfx::setVertexBuffer(0, mesh.vertexBuffer());
            bgfx::setIndexBuffer(mesh.indexBuffer(), part.firstIndex, part.indexCount);
            bgfx::setInstanceDataBuffer(&idb, batch.first, batch.count);
            bgfx::setState(drawState);
            bgfx::submit(view, batchProgram);
            ++drawCount_;
        }
    }
}

void Renderer::bloom(const Lighting& lighting) {
    if (!bgfx::isValid(bloomDownProgram_) || !bgfx::isValid(bloomUpProgram_)) return;
    // Down: the shade target into level 0 with the threshold, then each level into the next.
    for (int i = 0; i < kBloomLevels; ++i) {
        const bgfx::ViewId view = bgfx::ViewId(ViewBloomDown + i);
        const uint16_t sourceW = i == 0 ? uint16_t(width_) : bloomW_[i - 1];
        const uint16_t sourceH = i == 0 ? uint16_t(height_) : bloomH_[i - 1];
        bgfx::setViewFrameBuffer(view, bloomFb_[i]);
        bgfx::setViewRect(view, 0, 0, bloomW_[i], bloomH_[i]);
        bgfx::setViewClear(view, 0, 0, 1.0f, 0);
        bgfx::setViewTransform(view, nullptr, nullptr);
        const float params[4] = {lighting.bloomThreshold, std::max(lighting.bloomKnee, 1e-3f),
                                 i == 0 ? 1.0f : 0.0f, 0.0f};
        const float texel[4] = {1.0f / float(sourceW), 1.0f / float(sourceH), 0.0f, 0.0f};
        bgfx::setUniform(uBloom_, params);
        bgfx::setUniform(uBloomTexel_, texel);
        bgfx::setTexture(8, sColour_, i == 0 ? shadeColour_ : bloomTex_[i - 1]);
        screenPass(view, bloomDownProgram_);
    }
    // Up: each level read through a tent and ADDED into the one above it, so level 0 ends up
    // holding every level's share, the widest softest ones included.
    for (int j = 0; j < kBloomLevels - 1; ++j) {
        const int target = kBloomLevels - 2 - j;
        const bgfx::ViewId view = bgfx::ViewId(ViewBloomUp + j);
        bgfx::setViewFrameBuffer(view, bloomFb_[target]);
        bgfx::setViewRect(view, 0, 0, bloomW_[target], bloomH_[target]);
        bgfx::setViewClear(view, 0, 0, 1.0f, 0);
        bgfx::setViewTransform(view, nullptr, nullptr);
        const float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const float texel[4] = {1.0f / float(bloomW_[target + 1]),
                                1.0f / float(bloomH_[target + 1]), 0.0f, 0.0f};
        bgfx::setUniform(uBloom_, params);
        bgfx::setUniform(uBloomTexel_, texel);
        bgfx::setTexture(8, sColour_, bloomTex_[target + 1]);
        bgfx::setVertexBuffer(0, screenVb_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD);
        bgfx::submit(view, bloomUpProgram_);
        ++drawCount_;
    }
}

void Renderer::setPointLights(const PointLight* lights, uint32_t count, float minX, float minZ,
                               float side) {
    if (count > kMaxPointLights) {
        core::logError("%u point lights and the frame holds %u; the rest are dark", count,
                       kMaxPointLights);
        count = kMaxPointLights;
    }
    lightCount_ = lights ? count : 0;
    lampColour_.assign(size_t(lightCount_) * 3, 0.0f);
    std::fill(lampCpu_.begin(), lampCpu_.end(), 0.0f);
    const size_t row = size_t(kMaxPointLights + 1) * 4;
    for (uint32_t i = 0; i < lightCount_; ++i) {
        const PointLight& one = lights[i];
        float* at = &lampCpu_[size_t(i) * 4];
        at[0] = one.position[0];
        at[1] = one.position[1];
        at[2] = one.position[2];
        at[3] = one.reach;
        for (int c = 0; c < 3; ++c) {
            lampColour_[size_t(i) * 3 + c] = one.colour[c];
            lampCpu_[row + size_t(i) * 4 + c] = one.colour[c];
        }
        lampCpu_[row + size_t(i) * 4 + 3] = one.height;
    }
    lampsDirty_ = true;

    // The grid. A light is listed in every cell its sphere's footprint touches: the circle of
    // its reach on the ground, tested against the cell's square, so a pixel on a wall above a
    // cell still finds the light that reaches it through that cell's column.
    const int cells = lightCount_ > 0 ? std::max(1, int(std::ceil(side / kLightCellMetres))) : 0;
    std::vector<uint8_t> grid(size_t(std::max(cells, 1)) * 2 * 4 * size_t(std::max(cells, 1)), 0);
    int worst = 0, crowded = 0, lit = 0;
    std::vector<std::pair<float, int>> wanted;
    wanted.reserve(64);
    for (int cz = 0; cz < cells; ++cz) {
        for (int cx = 0; cx < cells; ++cx) {
            const float x0 = minX + float(cx) * kLightCellMetres;
            const float z0 = minZ + float(cz) * kLightCellMetres;
            wanted.clear();
            for (uint32_t i = 0; i < lightCount_; ++i) {
                const PointLight& one = lights[i];
                const float nx = std::clamp(one.position[0], x0, x0 + kLightCellMetres);
                const float nz = std::clamp(one.position[2], z0, z0 + kLightCellMetres);
                const float dx = one.position[0] - nx, dz = one.position[2] - nz;
                const float d2 = dx * dx + dz * dz;
                if (d2 < one.reach * one.reach) wanted.emplace_back(d2, int(i));
            }
            if (wanted.empty()) continue;
            ++lit;
            worst = std::max(worst, int(wanted.size()));
            if (int(wanted.size()) > kLightsPerCell) {
                // The nearest are kept. A far light cut from a crowded cell is the dimmest
                // there, and the cap is logged below so it is a number and not a surprise.
                ++crowded;
                std::sort(wanted.begin(), wanted.end());
            }
            uint8_t* out = &grid[(size_t(cz) * size_t(cells) * 2 + size_t(cx) * 2) * 4];
            for (int k = 0; k < std::min(int(wanted.size()), kLightsPerCell); ++k) {
                out[k] = uint8_t(wanted[size_t(k)].second + 1);
            }
        }
    }
    if (bgfx::isValid(lampGrid_)) bgfx::destroy(lampGrid_);
    const int w = std::max(cells, 1) * 2, h = std::max(cells, 1);
    lampGrid_ = bgfx::createTexture2D(uint16_t(w), uint16_t(h), false, 1,
                                      bgfx::TextureFormat::RGBA8,
                                      BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP |
                                          BGFX_SAMPLER_V_CLAMP,
                                      bgfx::copy(grid.data(), uint32_t(grid.size())));
    lampGridUniform_[0] = minX;
    lampGridUniform_[1] = minZ;
    lampGridUniform_[2] = 1.0f / kLightCellMetres;
    lampGridUniform_[3] = float(cells);
    lampParams_[1] = lightCount_ > 0 ? 1.0f : 0.0f;
    if (lightCount_ > 0) {
        core::logf("point lights: %u, on a %dx%d grid of %.0f m cells; %d cells lit, the worst "
                   "wants %d of the %d a cell holds, %d cells had to drop the farthest",
                   lightCount_, cells, cells, double(kLightCellMetres), lit, worst,
                   kLightsPerCell, crowded);
    }
}

void Renderer::setPointLightLevels(const float* levels, uint32_t count) {
    count = std::min(count, lightCount_);
    const size_t row = size_t(kMaxPointLights + 1) * 4;
    for (uint32_t i = 0; i < count; ++i) {
        for (int c = 0; c < 3; ++c) {
            lampCpu_[row + size_t(i) * 4 + c] = lampColour_[size_t(i) * 3 + c] * levels[i];
        }
    }
    lampsDirty_ = true;
}

void Renderer::submitGround(bgfx::ViewId view, bgfx::ProgramHandle program,
                            const content::Ground& g, uint64_t state, bool lit) {
    for (const content::GroundPart& part : g.parts()) {
        if (lit) {
            const float repeat[4] = {part.base.repeat, part.overlay.repeat, part.base.relief,
                                     part.overlay.relief};
            bgfx::setUniform(uGroundRepeat_, repeat);
            // The bite is MU2's own 0.35, and the second component says whether this surface
            // has an overlay at all: nine of Lorencia's forty-four are a base standing alone,
            // and those must not blend against a texture nothing meaningful is bound to.
            const float blend[4] = {0.35f, part.hasOverlay ? 1.0f : 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(uGroundBlend_, blend);
            bgfx::setTexture(0, sAlbedo_, part.base.albedo);
            bgfx::setTexture(1, sNormal_, part.base.normal);
            bgfx::setTexture(2, sOrm_, part.base.orm);
            bgfx::setTexture(9, sAlbedo2_, part.overlay.albedo);
            bgfx::setTexture(10, sNormal2_, part.overlay.normal);
            bgfx::setTexture(11, sOrm2_, part.overlay.orm);
            bindShadeInputs();
        }
        bgfx::setVertexBuffer(0, g.vertexBuffer());
        bgfx::setIndexBuffer(g.indexBuffer(), part.firstIndex, part.indexCount);
        // The land is the first single-sided surface in the project -- every material in
        // MU2's build is double sided -- so this is the one place a winding or a handedness
        // mistake shows as a hole rather than as nothing at all.
        bgfx::setState(state | BGFX_STATE_CULL_CW);
        bgfx::submit(view, program);
        ++drawCount_;
    }
}

void Renderer::screenPass(bgfx::ViewId view, bgfx::ProgramHandle program) {
    bgfx::setVertexBuffer(0, screenVb_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(view, program);
    ++drawCount_;
}

void Renderer::cameraMatrices(const Camera& camera, float* view, float* proj) const {
    bx::mtxLookAt(view, bx::Vec3(camera.position[0], camera.position[1], camera.position[2]),
                  bx::Vec3(camera.target[0], camera.target[1], camera.target[2]),
                  bx::Vec3(camera.up[0], camera.up[1], camera.up[2]), bx::Handedness::Right);
    bx::mtxProj(proj, camera.fovDegrees, float(width_) / float(height_), camera.nearPlane,
                camera.farPlane, bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
}

void Renderer::draw(const Camera& camera, const Lighting& lighting,
                    const std::vector<Drawable>& drawables, const content::Ground* ground,
                    const std::vector<Drawable>* casters) {
    drawCount_ = 0;
    // The ground has no cutout, and fs_shadow and fs_ground_prepass read this to know it.
    const float noCutout[4] = {-1.0f, 0.0f, 0.0f, 0.0f};

    // The frame's poses go up in one upload, and only the rows the game actually filled:
    // thirty-one figures at sixty bones is 95 kB, where the whole texture is 1.6 MB. Every
    // pass that follows reads them, the shadow pass included -- a figure casts the pose it
    // is in, not the pose it was bound in.
    if (bgfx::isValid(palette_) && paletteWritten_ > 0) {
        const uint32_t bytes = uint32_t(paletteWritten_) * kMaxBones * 12 * uint32_t(sizeof(float));
        bgfx::updateTexture2D(palette_, 0, 0, 0, 0, uint16_t(kMaxBones * 3),
                              uint16_t(paletteWritten_), bgfx::copy(paletteCpu_.data(), bytes));
    }

    // The lights' flicker, when it moved: two rows of the 256-wide texture, 8 kB.
    if (lampsDirty_ && bgfx::isValid(lamps_)) {
        bgfx::updateTexture2D(lamps_, 0, 0, 0, 0, uint16_t(kMaxPointLights + 1), 2,
                              bgfx::copy(lampCpu_.data(), uint32_t(lampCpu_.size() * sizeof(float))));
        lampsDirty_ = false;
    }
    lampParams_[0] = lighting.lampStrength;
    glowStrength_ = lighting.glowStrength;

    // --- the camera -------------------------------------------------------------------
    // Right-handed, said out loud. bx defaults every one of these to Handedness::Left, and
    // a left-handed view mirrors the frame left to right and makes view-space z positive in
    // front of the eye -- which silently turned the whole prepass depth negative and made
    // the SSAO take its "this is sky" path on every pixel of the screen. docs/conventions.md
    // says right-handed; this is where that has to be enforced.
    float view[16];
    float proj[16];
    cameraMatrices(camera, view, proj);
    const bool homogeneous = bgfx::getCaps()->homogeneousDepth;

    // --- the batches, and one instance buffer the whole frame reads -------------------
    // Two lists share it: what the camera draws, and what the sun's split draws. They are
    // usually the same instances, and they are not the same when the camera's chunks have
    // been culled -- a chunk behind the camera still casts into the frame.
    batches_.clear();
    casterBatches_.clear();
    const std::vector<Drawable>& casterList = casters ? *casters : drawables;
    const bool anything = !drawables.empty() || !casterList.empty() || ground != nullptr;
    if (anything) {
        // Grouped by mesh, keeping the order each mesh was first seen in, so a frame's draw
        // order does not shuffle between runs and a measurement stays comparable.
        std::vector<std::vector<const Drawable*>> groups;
        std::vector<std::vector<const Drawable*>> casterGroups;
        auto group = [](const std::vector<Drawable>& list,
                        std::vector<std::vector<const Drawable*>>& out,
                        std::vector<Batch>& batches) {
            std::unordered_map<const content::Mesh*, size_t> seen;
            out.reserve(8);
            for (const Drawable& d : list) {
                if (!d.mesh) continue;
                auto found = seen.find(d.mesh);
                if (found == seen.end()) {
                    seen.emplace(d.mesh, out.size());
                    out.emplace_back();
                    out.back().push_back(&d);
                    batches.push_back(Batch{d.mesh, 0, 0});
                } else {
                    out[found->second].push_back(&d);
                }
            }
        };
        group(drawables, groups, batches_);
        const bool separateCasters = casters != nullptr;
        if (separateCasters) group(casterList, casterGroups, casterBatches_);

        // A 4x4 matrix, the instance's baked light, and the row its pose occupies in the
        // bone palette. The depth passes read the matrix and the row and skip the light,
        // which costs them nothing: the stride is what the buffer is walked by, not what
        // each shader reads.
        const uint32_t stride = 96;
        uint32_t total = 0;
        for (const auto& g : groups) total += uint32_t(g.size());
        for (const auto& g : casterGroups) total += uint32_t(g.size());

        // bgfx will hand back fewer than asked for if the transient buffer is full. Asking
        // first and checking is the difference between a short frame and a corrupt one.
        const uint32_t available = bgfx::getAvailInstanceDataBuffer(total, stride);
        if (available < total) {
            core::logError("the instance buffer holds %u of %u instances this frame", available,
                           total);
            total = available;
        }
        if (total > 0 || ground) {
            // Allocated only when something wants it: the land is in world space already and
            // has no instances at all.
            bgfx::InstanceDataBuffer idb = {};
            if (total > 0) bgfx::allocInstanceDataBuffer(&idb, total, stride);
            uint32_t written = 0;
            auto fill = [&](std::vector<std::vector<const Drawable*>>& from,
                            std::vector<Batch>& batches) {
                for (size_t gi = 0; gi < from.size() && total > 0; ++gi) {
                    batches[gi].first = written;
                    uint32_t count = 0;
                    for (const Drawable* d : from[gi]) {
                        if (written >= total) break;
                        std::memcpy(idb.data + written * stride, d->transform,
                                    sizeof(float) * 16);
                        std::memcpy(idb.data + written * stride + sizeof(float) * 16, d->light,
                                    sizeof(float) * 4);
                        // No row of its own means the bind row, which is row 0 and is the
                        // identity. -1 would be read as a texel outside the palette.
                        const float skin[4] = {
                            float(d->paletteRow < 0 ? kBindRow : d->paletteRow), 0.0f, 0.0f,
                            0.0f};
                        std::memcpy(idb.data + written * stride + sizeof(float) * 20, skin,
                                    sizeof(skin));
                        ++written;
                        ++count;
                    }
                    batches[gi].count = count;
                }
                batches.erase(std::remove_if(batches.begin(), batches.end(),
                                             [](const Batch& b) { return b.count == 0; }),
                              batches.end());
            };
            fill(groups, batches_);
            if (separateCasters) fill(casterGroups, casterBatches_);
            // Without a list of its own, the sun draws what the camera draws.
            std::vector<Batch>& shadowBatches = separateCasters ? casterBatches_ : batches_;

            // --- view 0: the sun's split ---------------------------------------------
            float sunDir[3];
            lighting.sunDirection(sunDir);

            // Straight down would make the up vector parallel to the view; +z serves then.
            const bx::Vec3 up = std::fabs(sunDir[1]) > 0.99f ? bx::Vec3(0.0f, 0.0f, 1.0f)
                                                             : bx::Vec3(0.0f, 1.0f, 0.0f);
            // The sun's own axes through the WORLD's origin: the one frame in which a grid of
            // texels stays nailed to the ground. Its translation runs along the sun, so its x
            // and y are zero, and the split's own view below is this, moved by whole steps.
            float worldAxes[16];
            bx::mtxLookAt(worldAxes, bx::Vec3(sunDir[0], sunDir[1], sunDir[2]),
                          bx::Vec3(0.0f, 0.0f, 0.0f), up, bx::Handedness::Right);

            // How wide the split is and where it sits off the camera's target.
            float side = lighting.shadowRange;
            bx::Vec3 offset(0.0f, 0.0f, 0.0f);
            if (lighting.shadowFitBelow > 0.0f) fitSplit(camera, lighting, worldAxes, &side, &offset);

            const bx::Vec3 focus(camera.target[0] + splitSlide_[0] + offset.x,
                                 camera.target[1] + splitSlide_[1] + offset.y,
                                 camera.target[2] + splitSlide_[2] + offset.z);
            const float half = side * 0.5f;
            splitSide_ = side;
            // Far enough back that nothing casting into the frame is clipped out of it. This
            // is a reach along the sun and has nothing to do with the split's width.
            const float back = lighting.shadowRange;
            const bx::Vec3 eye(focus.x + sunDir[0] * back, focus.y + sunDir[1] * back,
                               focus.z + sunDir[2] * back);

            // Snapping: the sun's axes through the origin, moved by a whole number of steps
            // -- x and y in texels, z in the D16 map's own quantum -- to where the eye rounds
            // down to. Built from the whole numbers and not as "the eye plus its remainder",
            // because that sum rounds differently every frame: frames whose split had not
            // moved still got a matrix a few bits apart, and a blocker-search tap sitting on
            // a texel edge flipped between two texels -- a pixel jumping between lit and
            // shadowed while nothing moved. Now a split that has not stepped is the same bits.
            //
            // It snapped the focus in its own light space once, where the focus is always at
            // the origin: the floor had nothing to remove, the split crawled with the camera,
            // and float noise either side of zero flipped it a whole texel now and then. Depth
            // was never snapped, so every stored depth re-rounded each frame: flickering acne
            // on every wall facing the sun. tools/shimmer.py measured all three;
            // docs/shadow-probe.md.
            const float texel = side / float(shadowSize_);
            const float quantum = back * 2.0f / 65535.0f;
            const bx::Vec3 e = bx::mul(eye, worldAxes);
            const float stepsX = std::floor(e.x / texel);
            const float stepsY = std::floor(e.y / texel);
            const float stepsZ = std::floor(e.z / quantum);
            float snap[16];
            bx::mtxTranslate(snap, -stepsX * texel, -stepsY * texel, -stepsZ * quantum);
            float snappedView[16];
            bx::mtxMul(snappedView, worldAxes, snap);

            // The probe: the split's centre and eye read back out of the matrix that is
            // actually drawn with -- not out of the arithmetic above, which is what it tests
            // -- and put on the world's grid.
            {
                float unsnap[16];
                bx::mtxInverse(unsnap, snappedView);
                const bx::Vec3 centre =
                    bx::mul(bx::Vec3(0.0f, 0.0f, bx::mul(focus, snappedView).z), unsnap);
                const bx::Vec3 c = bx::mul(centre, worldAxes);
                const bx::Vec3 drawnEye =
                    bx::mul(bx::mul(bx::Vec3(0.0f, 0.0f, 0.0f), unsnap), worldAxes);
                split_.texel = texel;
                split_.texelX = c.x / texel;
                split_.texelY = c.y / texel;
                split_.depthQuanta = drawnEye.z / quantum;
            }

            float lightProj[16];
            // The depth range is cut to what the split can hold. MU4 measured the cost of
            // not doing it: at 79 m the D16 steps were coarse enough that the bias lifted
            // the shadow off the figure's feet.
            bx::mtxOrtho(lightProj, -half, half, -half, half, 0.0f, back * 2.0f, 0.0f,
                         homogeneous, bx::Handedness::Right);

            float lightViewProj[16];
            bx::mtxMul(lightViewProj, snappedView, lightProj);

            bgfx::setViewFrameBuffer(ViewShadow, shadowFb_);
            bgfx::setViewRect(ViewShadow, 0, 0, shadowSize_, shadowSize_);
            bgfx::setViewClear(ViewShadow, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewTransform(ViewShadow, snappedView, lightProj);

            // No front-face cull here, whatever a previous comment claimed: every material
            // in MU2's build is double sided, so there is nothing to cull and the bias has
            // to carry the whole job on its own.
            const uint64_t depthState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
            bgfx::setUniform(uMaterial_, noCutout);
            if (ground) submitGround(ViewShadow, groundShadowProgram_, *ground, depthState, false);
            if (!shadowBatches.empty()) {
                submitBatches(ViewShadow, shadowProgram_, skinnedShadowProgram_, shadowBatches,
                              idb, depthState, false);
            }

            // --- view 1: the prepass -------------------------------------------------
            bgfx::setViewFrameBuffer(ViewPrepass, prepassFb_);
            bgfx::setViewRect(ViewPrepass, 0, 0, uint16_t(width_), uint16_t(height_));
            bgfx::setViewClear(ViewPrepass, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000,
                               1.0f, 0);
            bgfx::setViewTransform(ViewPrepass, view, proj);
            const uint64_t prepassState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                          BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
            bgfx::setUniform(uMaterial_, noCutout);
            if (ground) submitGround(ViewPrepass, groundPrepassProgram_, *ground, prepassState, false);
            if (total > 0) {
                submitBatches(ViewPrepass, prepassProgram_, skinnedPrepassProgram_, batches_, idb,
                              prepassState, false);
            }

            // --- view 4's shared uniforms -------------------------------------------
            const float sunDirUniform[4] = {sunDir[0], sunDir[1], sunDir[2], lighting.sunStrength};
            const float sunColour[4] = {lighting.sunColour[0], lighting.sunColour[1],
                                        lighting.sunColour[2], lighting.ambientStrength};
            const float skyColour[4] = {lighting.skyColour[0], lighting.skyColour[1],
                                        lighting.skyColour[2], lighting.horizonPaleness};
            const float groundColour[4] = {lighting.groundColour[0], lighting.groundColour[1],
                                           lighting.groundColour[2], 0.0f};
            const float camPos[4] = {camera.position[0], camera.position[1], camera.position[2],
                                     camera.farPlane};
            const uint16_t hw = uint16_t(std::max(1, width_ / 2));
            const uint16_t hh = uint16_t(std::max(1, height_ / 2));
            // Pixels per unit at unit depth, in the SSAO target's own pixels: half its height
            // over the tangent of the half field of view. This is what turns a radius in
            // metres into a radius on screen, and without it the pass sampled itself.
            const float projScale =
                0.5f * float(hh) /
                std::tan(camera.fovDegrees * 0.5f * 3.14159265f / 180.0f);
            const float params[4] = {lighting.ssaoRadius, lighting.ssaoStrength,
                                     lighting.exposure, projScale};

            // --- views 2 and 3: SSAO and its blur, at half resolution ----------------


            bgfx::setViewFrameBuffer(ViewSsao, ssaoFb_);
            bgfx::setViewRect(ViewSsao, 0, 0, hw, hh);
            bgfx::setViewClear(ViewSsao, 0, 0, 1.0f, 0);
            bgfx::setViewTransform(ViewSsao, nullptr, nullptr);
            bgfx::setUniform(uParams_, params);
            const float tanHalfY = std::tan(camera.fovDegrees * 0.5f * 3.14159265f / 180.0f);
            const float camRay[4] = {tanHalfY * float(width_) / float(height_), tanHalfY, 0.0f,
                                     0.0f};
            bgfx::setUniform(uCamRay_, camRay);
            const float prepassSize[4] = {float(width_), float(height_), 1.0f / float(width_),
                                          1.0f / float(height_)};
            bgfx::setUniform(uPrepassSize_, prepassSize);
            bgfx::setTexture(6, sPrepass_, prepassColour_);
            screenPass(ViewSsao, msaa_ > 1 ? ssaoMsProgram_ : ssaoProgram_);

            bgfx::setViewFrameBuffer(ViewBlur, blurFb_);
            bgfx::setViewRect(ViewBlur, 0, 0, hw, hh);
            bgfx::setViewClear(ViewBlur, 0, 0, 1.0f, 0);
            bgfx::setViewTransform(ViewBlur, nullptr, nullptr);
            bgfx::setUniform(uParams_, params);
            bgfx::setUniform(uPrepassSize_, prepassSize);
            bgfx::setTexture(6, sPrepass_, prepassColour_);
            bgfx::setTexture(7, sAo_, ssaoTex_);
            screenPass(ViewBlur, msaa_ > 1 ? blurMsProgram_ : blurProgram_);

            // --- view 4: the one lit pass --------------------------------------------
            float shadowMtx[16];
            // NDC to texture space: x and y into [0,1] with y flipped, since Metal's origin
            // is the top left. z is already [0,1] where homogeneousDepth is false.
            float bias[16];
            bx::mtxIdentity(bias);
            bias[0] = 0.5f;
            bias[5] = -0.5f;
            bias[12] = 0.5f;
            bias[13] = 0.5f;
            if (homogeneous) {
                bias[10] = 0.5f;
                bias[14] = 0.5f;
            }
            bx::mtxMul(shadowMtx, lightViewProj, bias);

            // The split's depth runs 0..1 over this many metres, which is what turns a
            // depth difference back into a distance.
            const float depthRange = back * 2.0f;
            // The sun is a directional light and its split is orthographic, so the penumbra
            // is the blocker's gap times the tangent of its half angle -- a distance, turned
            // into uv by the split's width. This scale is only half the sum: the shader has
            // to multiply by it and *not* divide by the blocker's depth, which is the
            // point-light formula. That divide survived the first review here while a
            // comment on this line claimed it had gone; it was still in fs_shade.sc, still
            // widening the penumbra by about 2.1x at the bench's blocker depth, and it came
            // out on the second review. A comment is not a fix.
            const float tanHalfAngle =
                std::tan(lighting.sunAngleDegrees * 0.5f * 3.14159265f / 180.0f);
            const float penumbraScale = tanHalfAngle * depthRange / side;
            // The bias is a distance too. Held in the sheet in metres and turned into the
            // split's own 0..1 depth here, because 0.0015 of an NDC over a 120 m range is
            // 18 cm of peter-panning on a map whose D16 quantum is under 2 mm.
            const float depthBias = lighting.shadowBiasMetres / depthRange;
            const float shadowParams[4] = {depthBias, penumbraScale, 1.0f / float(shadowSize_),
                                           lighting.shadowNormalBias};

            bgfx::setViewFrameBuffer(ViewShade, shadeFb_);
            bgfx::setViewRect(ViewShade, 0, 0, uint16_t(width_), uint16_t(height_));
            // The depth is the prepass's and is kept; only the colour is cleared.
            bgfx::setViewClear(ViewShade, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
            bgfx::setViewTransform(ViewShade, view, proj);

            // Kept, and set on every shade draw by bindShadeInputs rather than once here.
            std::memcpy(shade_.sunDir, sunDirUniform, sizeof(shade_.sunDir));
            std::memcpy(shade_.sunColour, sunColour, sizeof(shade_.sunColour));
            std::memcpy(shade_.skyColour, skyColour, sizeof(shade_.skyColour));
            std::memcpy(shade_.groundColour, groundColour, sizeof(shade_.groundColour));
            std::memcpy(shade_.camPos, camPos, sizeof(shade_.camPos));
            std::memcpy(shade_.params, params, sizeof(shade_.params));
            std::memcpy(shade_.shadowMtx, shadowMtx, sizeof(shade_.shadowMtx));
            std::memcpy(shade_.shadowParams, shadowParams, sizeof(shade_.shadowParams));
            // The map's corner on the world's texel grid, which the world-anchored disc turn
            // reads. The centre is a whole number of texels by the snap; the round only drops
            // the float noise the read-back carries.
            shade_.shadowDebug[0] = shadowDebug_[0];
            shade_.shadowDebug[1] = shadowDebug_[1];
            shade_.shadowDebug[2] = std::round(split_.texelX) - float(shadowSize_) * 0.5f;
            shade_.shadowDebug[3] = std::round(split_.texelY) + float(shadowSize_) * 0.5f;
            // The blocker search and the widest penumbra, in metres and then in this split's
            // uv. They were 6 and 24 texels, which on the 60 m, 2048 split the look was judged
            // on is 17.6 cm and 70 cm; counted in texels, a finer map drew a smaller shadow --
            // the search shrank to 5 cm at 4096, the outer penumbra was cut off, and a finer
            // map looked cheaper because fewer pixels found a blocker at all.
            constexpr float kSearchMetres = 0.176f;
            constexpr float kWidestPenumbraMetres = 0.703f;
            shade_.shadowReach[0] = kSearchMetres / side;
            shade_.shadowReach[1] = kWidestPenumbraMetres / side;
            shade_.shadowReach[2] = shade_.shadowReach[3] = 0.0f;
            bgfx::setTexture(4, sShadowCompare_, shadowMap_,
                             BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP |
                                 BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(5, sShadowDepth_, shadowMap_,
                             BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(7, sAo_, blurTex_);

            // Depth EQUAL against what the prepass laid down, and no depth write: nothing
            // here is shaded twice.
            const uint64_t shadeState =
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL;
            if (ground) submitGround(ViewShade, groundShadeProgram_, *ground, shadeState, true);
            if (total > 0) {
                submitBatches(ViewShade, shadeProgram_, skinnedShadeProgram_, batches_, idb,
                              shadeState, true);
            }

            // --- view 5, first half: MU's BlendMeshes --------------------------------
            // The glow parts the three passes above skipped, added into the shade target
            // against the prepass's depth and writing none. Here and not in Effects because
            // they are meshes that ride the frame's instance buffer, which lives in this
            // block; the view's target and transform are set below with the sprites'.
            if (total > 0 && bgfx::isValid(glowProgram_) && bgfx::isValid(skinnedGlowProgram_)) {
                const uint64_t glowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
                                           BGFX_STATE_BLEND_ADD;
                submitBatches(ViewTransparent, glowProgram_, skinnedGlowProgram_, batches_, idb,
                              glowState, false, true);
            }
        }
    }

    if (!anything) {
        // Nothing was drawn, so nothing cleared the shade target and the present below would
        // hand the screen a texture that has never been written.
        bgfx::setViewFrameBuffer(ViewShade, shadeFb_);
        bgfx::setViewRect(ViewShade, 0, 0, uint16_t(width_), uint16_t(height_));
        bgfx::setViewClear(ViewShade, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
        bgfx::touch(ViewShade);
    }

    // --- view 5: the transparent pass -------------------------------------------------
    // Into the SAME target the shade pass wrote, which is the whole reason this view is here
    // and not after the present: the target is HDR and multisampled, so an additive flame
    // adds to a linear radiance, gets antialiased edges from the MSAA already being paid
    // for, and is tonemapped with the scene by the present below. Drawn after the resolve it
    // would be LDR sprites over an already-tonemapped image.
    //
    // It shares the prepass's depth as an attachment, so it can test against the world
    // without writing to it. The sort inside Effects::draw is what decides the picture.
    bgfx::setViewFrameBuffer(ViewTransparent, shadeFb_);
    bgfx::setViewRect(ViewTransparent, 0, 0, uint16_t(width_), uint16_t(height_));
    // No clear at all: the shade pass's colour and the prepass's depth are both wanted.
    bgfx::setViewClear(ViewTransparent, 0, 0, 1.0f, 0);
    effects_.setFlameStrength(lighting.flameStrength);
    effects_.draw(ViewTransparent, view, proj, camera.position);
    drawCount_ += effects_.lastDrawCount();

    bloom(lighting);

    // --- the present ------------------------------------------------------------------
    const float params[4] = {lighting.ssaoRadius, lighting.ssaoStrength, lighting.exposure, 0.0f};
    const bool bloomed = bgfx::isValid(bloomDownProgram_) && bgfx::isValid(bloomUpProgram_);
    const float bloomParams[4] = {0.0f, 0.0f, bloomed ? lighting.bloomStrength : 0.0f, 0.0f};
    bgfx::setUniform(uBloom_, bloomParams);
    bgfx::setTexture(9, sBloom_, bloomTex_[0]);
    bgfx::setViewFrameBuffer(ViewPresent, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewPresent, 0, 0, uint16_t(width_), uint16_t(height_));
    bgfx::setViewClear(ViewPresent, BGFX_CLEAR_COLOR, 0x101418ff, 1.0f, 0);
    bgfx::setViewTransform(ViewPresent, nullptr, nullptr);
    bgfx::setUniform(uParams_, params);
    bgfx::setTexture(8, sColour_, shadeColour_);
    screenPass(ViewPresent, presentProgram_);

    // View 7 is the HUD's, and is submitted empty until sprint 7 fills it. A view bgfx sees
    // nothing in is dropped, and an account with no rows reads as free rather than unbuilt.
    bgfx::setViewFrameBuffer(ViewHud, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewHud, 0, 0, uint16_t(width_), uint16_t(height_));
    bgfx::touch(ViewHud);
}

}  // namespace mu::gfx
