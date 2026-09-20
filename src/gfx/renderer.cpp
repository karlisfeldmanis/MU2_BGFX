#include "gfx/renderer.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

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

bool Renderer::init(int width, int height, const std::string& shaderDir) {
    if (!loadPrograms(shaderDir)) return false;

    uSunDir_ = bgfx::createUniform("u_sunDir", bgfx::UniformType::Vec4);
    uSunColour_ = bgfx::createUniform("u_sunColour", bgfx::UniformType::Vec4);
    uSkyColour_ = bgfx::createUniform("u_skyColour", bgfx::UniformType::Vec4);
    uGroundColour_ = bgfx::createUniform("u_groundColour", bgfx::UniformType::Vec4);
    uCamPos_ = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);
    uParams_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
    uMaterial_ = bgfx::createUniform("u_material", bgfx::UniformType::Vec4);
    uShadowMtx_ = bgfx::createUniform("u_shadowMtx", bgfx::UniformType::Mat4);
    uShadowParams_ = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    uCamRay_ = bgfx::createUniform("u_camRay", bgfx::UniformType::Vec4);

    sAlbedo_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    sNormal_ = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
    sOrm_ = bgfx::createUniform("s_orm", bgfx::UniformType::Sampler);
    sEmissive_ = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
    sShadowCompare_ = bgfx::createUniform("s_shadowCompare", bgfx::UniformType::Sampler);
    sShadowDepth_ = bgfx::createUniform("s_shadowDepth", bgfx::UniformType::Sampler);
    sPrepass_ = bgfx::createUniform("s_prepass", bgfx::UniformType::Sampler);
    sAo_ = bgfx::createUniform("s_ao", bgfx::UniformType::Sampler);
    sColour_ = bgfx::createUniform("s_colour", bgfx::UniformType::Sampler);

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

    return createTargets(width, height);
}

bool Renderer::loadPrograms(const std::string& dir) {
    shadowProgram_ = loadProgram(dir, "vs_depth", "fs_shadow");
    prepassProgram_ = loadProgram(dir, "vs_static", "fs_prepass");
    ssaoProgram_ = loadProgram(dir, "vs_screen", "fs_ssao");
    blurProgram_ = loadProgram(dir, "vs_screen", "fs_blur");
    shadeProgram_ = loadProgram(dir, "vs_static", "fs_shade");
    presentProgram_ = loadProgram(dir, "vs_screen", "fs_present");
    const bool ok = bgfx::isValid(shadowProgram_) && bgfx::isValid(prepassProgram_) &&
                    bgfx::isValid(ssaoProgram_) && bgfx::isValid(blurProgram_) &&
                    bgfx::isValid(shadeProgram_) && bgfx::isValid(presentProgram_);
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

    // The shadow map is read twice: through the compare sampler for the filtered result,
    // and as plain depth to find the blocker. docs/conventions.md.
    shadowMap_ = bgfx::createTexture2D(kShadowSize, kShadowSize, false, 1,
                                       bgfx::TextureFormat::D16, rt | clamp);
    shadowFb_ = bgfx::createFrameBuffer(1, &shadowMap_, true);

    // View normal and view depth in one target. RGBA16F because the depth is in world units
    // and Lorencia is 25 600 of them across: a half float runs out of precision at that
    // range, so what is stored is the distance from the eye, which stays small.
    prepassColour_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, rt | clamp);
    sceneDepth_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F, rt | clamp);
    bgfx::TextureHandle prepassAttachments[] = {prepassColour_, sceneDepth_};
    prepassFb_ = bgfx::createFrameBuffer(2, prepassAttachments, false);

    ssaoTex_ = bgfx::createTexture2D(hw, hh, false, 1, bgfx::TextureFormat::R8, rt | clamp);
    ssaoFb_ = bgfx::createFrameBuffer(1, &ssaoTex_, true);
    blurTex_ = bgfx::createTexture2D(hw, hh, false, 1, bgfx::TextureFormat::R8, rt | clamp);
    blurFb_ = bgfx::createFrameBuffer(1, &blurTex_, true);

    // The shade pass shares the prepass's depth so it can test EQUAL against it. That
    // sharing is the whole reason nothing is shaded twice.
    shadeColour_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, rt | clamp);
    bgfx::TextureHandle shadeAttachments[] = {shadeColour_, sceneDepth_};
    shadeFb_ = bgfx::createFrameBuffer(2, shadeAttachments, false);

    const bool ok = bgfx::isValid(shadowFb_) && bgfx::isValid(prepassFb_) &&
                    bgfx::isValid(ssaoFb_) && bgfx::isValid(blurFb_) && bgfx::isValid(shadeFb_);
    if (!ok) core::logError("a render target did not survive creation at %dx%d", width, height);
    return ok;
}

void Renderer::destroyTargets() {
    for (bgfx::FrameBufferHandle* fb : {&shadowFb_, &prepassFb_, &ssaoFb_, &blurFb_, &shadeFb_}) {
        if (bgfx::isValid(*fb)) bgfx::destroy(*fb);
        *fb = BGFX_INVALID_HANDLE;
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
    for (bgfx::ProgramHandle* p : {&shadowProgram_, &prepassProgram_, &ssaoProgram_, &blurProgram_,
                                   &shadeProgram_, &presentProgram_}) {
        if (bgfx::isValid(*p)) bgfx::destroy(*p);
        *p = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* u :
         {&uSunDir_, &uSunColour_, &uSkyColour_, &uGroundColour_, &uCamPos_, &uParams_,
          &uMaterial_, &uShadowMtx_, &uShadowParams_, &uCamRay_, &sAlbedo_,
          &sNormal_, &sOrm_, &sEmissive_, &sShadowCompare_, &sShadowDepth_, &sPrepass_, &sAo_,
          &sColour_}) {
        if (bgfx::isValid(*u)) bgfx::destroy(*u);
        *u = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(screenVb_)) bgfx::destroy(screenVb_);
    screenVb_ = BGFX_INVALID_HANDLE;
}

void Renderer::submitBatches(bgfx::ViewId view, bgfx::ProgramHandle program,
                             const std::vector<Batch>& batches, const bgfx::InstanceDataBuffer& idb,
                             uint64_t state, bool bindMaterial) {
    for (const Batch& batch : batches) {
        const content::Mesh& mesh = *batch.mesh;
        for (const content::Part& part : mesh.parts()) {
            const content::Material& material = mesh.materials()[part.material];

            // A cutout discards in every pass, this one included, or a leaf casts a card.
            const float materialParams[4] = {material.cutout, material.twoSided ? 1.0f : 0.0f,
                                             0.0f, 0.0f};
            bgfx::setUniform(uMaterial_, materialParams);
            // The albedo is bound even in the depth passes, because the cutout reads its alpha.
            bgfx::setTexture(0, sAlbedo_, material.albedo);
            if (bindMaterial) {
                bgfx::setTexture(1, sNormal_, material.normal);
                bgfx::setTexture(2, sOrm_, material.orm);
                bgfx::setTexture(3, sEmissive_, material.emissive);
            }

            uint64_t drawState = state;
            // MU's figures are single sheets of mixed winding and are drawn two-sided.
            if (!material.twoSided) drawState |= BGFX_STATE_CULL_CW;

            bgfx::setVertexBuffer(0, mesh.vertexBuffer());
            bgfx::setIndexBuffer(mesh.indexBuffer(), part.firstIndex, part.indexCount);
            bgfx::setInstanceDataBuffer(&idb, batch.first, batch.count);
            bgfx::setState(drawState);
            bgfx::submit(view, program);
            ++drawCount_;
        }
    }
}

void Renderer::screenPass(bgfx::ViewId view, bgfx::ProgramHandle program) {
    bgfx::setVertexBuffer(0, screenVb_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(view, program);
    ++drawCount_;
}

void Renderer::draw(const Camera& camera, const Lighting& lighting,
                    const std::vector<Drawable>& drawables) {
    drawCount_ = 0;

    // --- the camera -------------------------------------------------------------------
    // Right-handed, said out loud. bx defaults every one of these to Handedness::Left, and
    // a left-handed view mirrors the frame left to right and makes view-space z positive in
    // front of the eye -- which silently turned the whole prepass depth negative and made
    // the SSAO take its "this is sky" path on every pixel of the screen. docs/conventions.md
    // says right-handed; this is where that has to be enforced.
    float view[16];
    bx::mtxLookAt(view, bx::Vec3(camera.position[0], camera.position[1], camera.position[2]),
                  bx::Vec3(camera.target[0], camera.target[1], camera.target[2]),
                  bx::Vec3(camera.up[0], camera.up[1], camera.up[2]), bx::Handedness::Right);
    float proj[16];
    const bool homogeneous = bgfx::getCaps()->homogeneousDepth;
    bx::mtxProj(proj, camera.fovDegrees, float(width_) / float(height_), camera.nearPlane,
                camera.farPlane, homogeneous, bx::Handedness::Right);

    // --- the batches, and one instance buffer the whole frame reads -------------------
    batches_.clear();
    if (!drawables.empty()) {
        // Grouped by mesh, keeping the order each mesh was first seen in, so a frame's draw
        // order does not shuffle between runs and a measurement stays comparable.
        std::unordered_map<const content::Mesh*, size_t> seen;
        std::vector<std::vector<const Drawable*>> groups;
        groups.reserve(8);
        for (const Drawable& d : drawables) {
            if (!d.mesh) continue;
            auto found = seen.find(d.mesh);
            if (found == seen.end()) {
                seen.emplace(d.mesh, groups.size());
                groups.emplace_back();
                groups.back().push_back(&d);
                batches_.push_back(Batch{d.mesh, 0, 0});
            } else {
                groups[found->second].push_back(&d);
            }
        }

        const uint32_t stride = 64;  // one 4x4 matrix
        uint32_t total = 0;
        for (const auto& g : groups) total += uint32_t(g.size());

        // bgfx will hand back fewer than asked for if the transient buffer is full. Asking
        // first and checking is the difference between a short frame and a corrupt one.
        const uint32_t available = bgfx::getAvailInstanceDataBuffer(total, stride);
        if (available < total) {
            core::logError("the instance buffer holds %u of %u instances this frame", available,
                           total);
            total = available;
        }
        if (total > 0) {
            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer(&idb, total, stride);
            uint32_t written = 0;
            for (size_t gi = 0; gi < groups.size(); ++gi) {
                batches_[gi].first = written;
                uint32_t count = 0;
                for (const Drawable* d : groups[gi]) {
                    if (written >= total) break;
                    std::memcpy(idb.data + written * stride, d->transform, sizeof(float) * 16);
                    ++written;
                    ++count;
                }
                batches_[gi].count = count;
            }
            batches_.erase(std::remove_if(batches_.begin(), batches_.end(),
                                          [](const Batch& b) { return b.count == 0; }),
                           batches_.end());

            // --- view 0: the sun's split ---------------------------------------------
            float sunDir[3];
            lighting.sunDirection(sunDir);

            // Framed on the camera's ground point, not on the whole map: one split covering
            // what is looked at is sharper than two covering what is not.
            const bx::Vec3 focus(camera.target[0], camera.target[1], camera.target[2]);
            const float half = lighting.shadowRange * 0.5f;
            // Far enough back that nothing casting into the frame is clipped out of it.
            const float back = lighting.shadowRange;
            const bx::Vec3 eye(focus.x + sunDir[0] * back, focus.y + sunDir[1] * back,
                               focus.z + sunDir[2] * back);
            // Straight down would make the up vector parallel to the view; +z serves then.
            const bx::Vec3 up = std::fabs(sunDir[1]) > 0.99f ? bx::Vec3(0.0f, 0.0f, 1.0f)
                                                             : bx::Vec3(0.0f, 1.0f, 0.0f);
            float lightView[16];
            bx::mtxLookAt(lightView, eye, focus, up, bx::Handedness::Right);

            // Texel snapping: the focus is quantised in the light's own space, or the split
            // crawls with the camera and every shadow edge shimmers.
            const float texel = lighting.shadowRange / float(kShadowSize);
            float focusInLight[3];
            bx::Vec3 f = bx::mul(focus, lightView);
            focusInLight[0] = std::floor(f.x / texel) * texel;
            focusInLight[1] = std::floor(f.y / texel) * texel;
            focusInLight[2] = f.z;
            float snap[16];
            // Minus the remainder, not plus it. Adding it moved the focus to f + frac, whose
            // own fraction is twice the original: the crawl it was meant to remove stayed,
            // and a full-texel pop was added every time the focus crossed a boundary.
            bx::mtxTranslate(snap, focusInLight[0] - f.x, focusInLight[1] - f.y, 0.0f);
            float snappedView[16];
            bx::mtxMul(snappedView, lightView, snap);

            float lightProj[16];
            // The depth range is cut to what the split can hold. MU4 measured the cost of
            // not doing it: at 79 m the D16 steps were coarse enough that the bias lifted
            // the shadow off the figure's feet.
            bx::mtxOrtho(lightProj, -half, half, -half, half, 0.0f, back * 2.0f, 0.0f,
                         homogeneous, bx::Handedness::Right);

            float lightViewProj[16];
            bx::mtxMul(lightViewProj, snappedView, lightProj);

            bgfx::setViewFrameBuffer(ViewShadow, shadowFb_);
            bgfx::setViewRect(ViewShadow, 0, 0, kShadowSize, kShadowSize);
            bgfx::setViewClear(ViewShadow, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewTransform(ViewShadow, snappedView, lightProj);

            // No front-face cull here, whatever a previous comment claimed: every material
            // in MU2's build is double sided, so there is nothing to cull and the bias has
            // to carry the whole job on its own.
            submitBatches(ViewShadow, shadowProgram_, batches_, idb,
                          BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS, false);

            // --- view 1: the prepass -------------------------------------------------
            bgfx::setViewFrameBuffer(ViewPrepass, prepassFb_);
            bgfx::setViewRect(ViewPrepass, 0, 0, uint16_t(width_), uint16_t(height_));
            bgfx::setViewClear(ViewPrepass, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000,
                               1.0f, 0);
            bgfx::setViewTransform(ViewPrepass, view, proj);
            submitBatches(ViewPrepass, prepassProgram_, batches_, idb,
                          BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                              BGFX_STATE_DEPTH_TEST_LESS,
                          false);

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
            bgfx::setTexture(6, sPrepass_, prepassColour_);
            screenPass(ViewSsao, ssaoProgram_);

            bgfx::setViewFrameBuffer(ViewBlur, blurFb_);
            bgfx::setViewRect(ViewBlur, 0, 0, hw, hh);
            bgfx::setViewClear(ViewBlur, 0, 0, 1.0f, 0);
            bgfx::setViewTransform(ViewBlur, nullptr, nullptr);
            bgfx::setUniform(uParams_, params);
            bgfx::setTexture(6, sPrepass_, prepassColour_);
            bgfx::setTexture(7, sAo_, ssaoTex_);
            screenPass(ViewBlur, blurProgram_);

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
            // into uv by the split's width. The similar-triangles divide by the blocker's own
            // depth is the point-light formula and does not belong here: it made the
            // penumbra depend on where the caster sat in the split rather than on its gap.
            const float tanHalfAngle =
                std::tan(lighting.sunAngleDegrees * 0.5f * 3.14159265f / 180.0f);
            const float penumbraScale = tanHalfAngle * depthRange / lighting.shadowRange;
            // The bias is a distance too. Held in the sheet in metres and turned into the
            // split's own 0..1 depth here, because 0.0015 of an NDC over a 120 m range is
            // 18 cm of peter-panning on a map whose D16 quantum is under 2 mm.
            const float depthBias = lighting.shadowBiasMetres / depthRange;
            const float shadowParams[4] = {depthBias, penumbraScale, 1.0f / float(kShadowSize),
                                           lighting.shadowNormalBias};

            bgfx::setViewFrameBuffer(ViewShade, shadeFb_);
            bgfx::setViewRect(ViewShade, 0, 0, uint16_t(width_), uint16_t(height_));
            // The depth is the prepass's and is kept; only the colour is cleared.
            bgfx::setViewClear(ViewShade, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
            bgfx::setViewTransform(ViewShade, view, proj);

            bgfx::setUniform(uSunDir_, sunDirUniform);
            bgfx::setUniform(uSunColour_, sunColour);
            bgfx::setUniform(uSkyColour_, skyColour);
            bgfx::setUniform(uGroundColour_, groundColour);
            bgfx::setUniform(uCamPos_, camPos);
            bgfx::setUniform(uParams_, params);
            bgfx::setUniform(uShadowMtx_, shadowMtx);
            bgfx::setUniform(uShadowParams_, shadowParams);
            bgfx::setTexture(4, sShadowCompare_, shadowMap_,
                             BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP |
                                 BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(5, sShadowDepth_, shadowMap_,
                             BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(7, sAo_, blurTex_);

            // Depth EQUAL against what the prepass laid down, and no depth write: nothing
            // here is shaded twice.
            submitBatches(ViewShade, shadeProgram_, batches_, idb,
                          BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL,
                          true);
        }
    }

    if (drawables.empty()) {
        // Nothing was drawn, so nothing cleared the shade target and the present below would
        // hand the screen a texture that has never been written.
        bgfx::setViewFrameBuffer(ViewShade, shadeFb_);
        bgfx::setViewRect(ViewShade, 0, 0, uint16_t(width_), uint16_t(height_));
        bgfx::setViewClear(ViewShade, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
        bgfx::touch(ViewShade);
    }

    // --- view 5: present ------------------------------------------------------------
    const float params[4] = {lighting.ssaoRadius, lighting.ssaoStrength, lighting.exposure, 0.0f};
    bgfx::setViewFrameBuffer(ViewPresent, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewPresent, 0, 0, uint16_t(width_), uint16_t(height_));
    bgfx::setViewClear(ViewPresent, BGFX_CLEAR_COLOR, 0x101418ff, 1.0f, 0);
    bgfx::setViewTransform(ViewPresent, nullptr, nullptr);
    bgfx::setUniform(uParams_, params);
    bgfx::setTexture(8, sColour_, shadeColour_);
    screenPass(ViewPresent, presentProgram_);

    // View 6 is the HUD's, and is submitted empty until sprint 7 fills it. A view bgfx sees
    // nothing in is dropped, and an account with no rows reads as free rather than unbuilt.
    bgfx::setViewFrameBuffer(ViewHud, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewHud, 0, 0, uint16_t(width_), uint16_t(height_));
    bgfx::touch(ViewHud);
}

}  // namespace mu::gfx
