// The reflection probe: a cube of the sky and the world round a point, filtered down its mips
// so a roughness picks a level. Sprint 8c built it and docs/sprints/08c-the-metal.md measured
// it at 0.3 ms; metal has nothing to reflect without it.
#include "gfx/renderer.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "gfx/renderer_internal.h"
#include "gfx/views.h"

namespace mu::gfx {

bool Renderer::createProbe(const std::string& shaderDir) {
    probeSkyProgram_ = loadProgram(shaderDir, "vs_screen", "fs_probe_sky");
    probeFilterProgram_ = loadProgram(shaderDir, "vs_screen", "fs_probe_filter");
    probeDownProgram_ = loadProgram(shaderDir, "vs_screen", "fs_probe_down");
    uProbe_ = bgfx::createUniform("u_probe", bgfx::UniformType::Vec4);
    uProbePos_ = bgfx::createUniform("u_probePos", bgfx::UniformType::Vec4);
    uProbeFace_ = bgfx::createUniform("u_probeFace", bgfx::UniformType::Vec4);
    sProbe_ = bgfx::createUniform("s_probe", bgfx::UniformType::Sampler);
    sSource_ = bgfx::createUniform("s_source", bgfx::UniformType::Sampler);

    const uint64_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    const uint8_t black[6 * 4] = {};
    blackCube_ = bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, clamp,
                                         bgfx::copy(black, sizeof(black)));
    const uint8_t white = 255;
    whiteAo_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R8, clamp,
                                     bgfx::copy(&white, 1));

    // RGBA16F: it holds radiance, and a fire's pool is over one.
    probeRaw_ = bgfx::createTextureCube(uint16_t(kProbeSize), false, 1,
                                        bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | clamp);
    probeChain_ = bgfx::createTextureCube(uint16_t(kProbeSize), true, 1,
                                          bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | clamp);
    probeDepth_ = bgfx::createTexture2D(uint16_t(kProbeSize), uint16_t(kProbeSize), false, 1,
                                        bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT_WRITE_ONLY);
    probeFiltered_ = bgfx::createTextureCube(uint16_t(kProbeSize), true, 1,
                                             bgfx::TextureFormat::RGBA16F,
                                             BGFX_TEXTURE_RT | clamp);
    bool ok = bgfx::isValid(probeSkyProgram_) && bgfx::isValid(probeFilterProgram_) &&
              bgfx::isValid(probeDownProgram_) && bgfx::isValid(probeChain_) &&
              bgfx::isValid(probeRaw_) && bgfx::isValid(probeDepth_) &&
              bgfx::isValid(probeFiltered_) && bgfx::isValid(blackCube_) &&
              bgfx::isValid(whiteAo_);
    if (!ok) {
        core::logError("probe: sky %d filter %d raw %d depth %d filtered %d black %d white %d",
                       bgfx::isValid(probeSkyProgram_), bgfx::isValid(probeFilterProgram_),
                       bgfx::isValid(probeRaw_), bgfx::isValid(probeDepth_),
                       bgfx::isValid(probeFiltered_), bgfx::isValid(blackCube_),
                       bgfx::isValid(whiteAo_));
    }
    for (int face = 0; face < 6 && ok; ++face) {
        bgfx::Attachment at[2];
        at[0].init(probeRaw_, bgfx::Access::Write, uint16_t(face), 1, 0, BGFX_ATTACHMENT_NONE);
        // Depth takes no chain, and bgfx refuses the buffer if it is asked for one --
        // Attachment::init asks by default.
        at[1].init(probeDepth_, bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);
        probeFaceFb_[face] = bgfx::createFrameBuffer(2, at, false);
        ok = ok && bgfx::isValid(probeFaceFb_[face]);
        if (!ok) core::logError("probe: face %d's frame buffer was refused", face);
    }
    for (int i = 0; i < 6 * kProbeMips; ++i) probeFilterFb_[i] = BGFX_INVALID_HANDLE;
    for (int i = 0; i < 6 * kProbeChain; ++i) probeChainFb_[i] = BGFX_INVALID_HANDLE;
    for (int level = 0; level < kProbeChain && ok; ++level) {
        for (int face = 0; face < 6 && ok; ++face) {
            bgfx::Attachment at;
            at.init(probeChain_, bgfx::Access::Write, uint16_t(face), 1, uint16_t(level),
                    BGFX_ATTACHMENT_NONE);
            probeChainFb_[level * 6 + face] = bgfx::createFrameBuffer(1, &at, false);
            ok = ok && bgfx::isValid(probeChainFb_[level * 6 + face]);
        }
    }
    for (int mip = 0; mip < kProbeMips && ok; ++mip) {
        for (int face = 0; face < 6 && ok; ++face) {
            // No chain made here: every mip of this cube is written by the filter, and a
            // generated one would overwrite the lobes with a box filter of the level above.
            bgfx::Attachment at;
            at.init(probeFiltered_, bgfx::Access::Write, uint16_t(face), 1, uint16_t(mip),
                    BGFX_ATTACHMENT_NONE);
            probeFilterFb_[mip * 6 + face] = bgfx::createFrameBuffer(1, &at, false);
            ok = ok && bgfx::isValid(probeFilterFb_[mip * 6 + face]);
            if (!ok) core::logError("probe: mip %d face %d's frame buffer was refused", mip, face);
        }
    }
    if (ok) {
        core::logf("reflection probe %d texels a face, %d filtered mips, a face and its chain every other frame, a filter every 14th",
                   kProbeSize, kProbeMips);
    }
    return ok;
}

void Renderer::destroyProbe() {
    for (bgfx::FrameBufferHandle& fb : probeFaceFb_) {
        if (bgfx::isValid(fb)) bgfx::destroy(fb);
        fb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::FrameBufferHandle& fb : probeChainFb_) {
        if (bgfx::isValid(fb)) bgfx::destroy(fb);
        fb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::FrameBufferHandle& fb : probeFilterFb_) {
        if (bgfx::isValid(fb)) bgfx::destroy(fb);
        fb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::TextureHandle* t : {&probeRaw_, &probeDepth_, &probeFiltered_, &probeChain_, &blackCube_,
                                   &whiteAo_}) {
        if (bgfx::isValid(*t)) bgfx::destroy(*t);
        *t = BGFX_INVALID_HANDLE;
    }
    for (bgfx::ProgramHandle* p : {&probeSkyProgram_, &probeFilterProgram_, &probeDownProgram_}) {
        if (bgfx::isValid(*p)) bgfx::destroy(*p);
        *p = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* u : {&uProbe_, &uProbePos_, &uProbeFace_, &sProbe_, &sSource_}) {
        if (bgfx::isValid(*u)) bgfx::destroy(*u);
        *u = BGFX_INVALID_HANDLE;
    }
    probeOk_ = probeReady_ = false;
}

void Renderer::probeFaceView(int face, const float* at, float* view, float* proj) const {
    // Forward and up for each face in probe.sh's order. The up is the direction under a
    // face's top row, which cubeDir puts at t = -1: +y for the four sides, -z looking up and
    // +z looking down.
    static const float kForward[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                         {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    static const float kUp[6][3] = {{0, 1, 0},  {0, 1, 0}, {0, 0, -1},
                                    {0, 0, 1},  {0, 1, 0}, {0, 1, 0}};
    const bx::Vec3 eye(at[0], at[1], at[2]);
    const bx::Vec3 target(at[0] + kForward[face][0], at[1] + kForward[face][1],
                          at[2] + kForward[face][2]);
    bx::mtxLookAt(view, eye, target, bx::Vec3(kUp[face][0], kUp[face][1], kUp[face][2]),
                  bx::Handedness::Left);
    bx::mtxProj(proj, 90.0f, 1.0f, 0.05f, 400.0f, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Left);
}

void Renderer::drawProbe(const Camera& camera, const content::Ground* ground,
                         const std::vector<Batch>& batches, const bgfx::InstanceDataBuffer& idb) {
    // At the chest of whoever the camera follows: what a breastplate would see.
    constexpr float kProbeHeight = 1.2f;
    const float at[3] = {camera.target[0], camera.target[1] + kProbeHeight, camera.target[2]};

    // A whole new cube: filter it, in a frame of its own that draws no face, so no frame pays
    // for both. A cycle is seven turns, six faces and a filter, fourteen frames.
    // Every other frame only. The faces were a quarter of a millisecond of wall frame drawn
    // every frame, and a cube round a man walking 2.5 m/s loses nothing it can show by moving
    // on at thirteen times a second instead of twenty-six at 180 fps.
    if ((++probeTick_ & 1) != 0) return;

    if (probeFilterDue_) {
        filterProbe();
        probeFilterDue_ = false;
        std::memcpy(probeTaken_, probeAt_, sizeof(probeAt_));
        probeReady_ = true;
        return;
    }

    // One face a frame. The first cube is read once all six and the filter are done, seven
    // frames in; until then the shade pass keeps the closed-form sky.
    const int faces = 1;
    probePass_ = true;
    cullBit_ = BGFX_STATE_CULL_CCW;
    std::memcpy(probeAt_, at, sizeof(probeAt_));
    for (int k = 0; k < faces; ++k) {
        const int face = probeNextFace_;
        probeNextFace_ = (probeNextFace_ + 1) % 6;
        ++probeFacesDrawn_;
        const bgfx::ViewId view = bgfx::ViewId(ViewProbeFace + face);
        float faceView[16];
        float faceProj[16];
        probeFaceView(face, at, faceView, faceProj);
        bgfx::setViewFrameBuffer(view, probeFaceFb_[face]);
        bgfx::setViewRect(view, 0, 0, uint16_t(kProbeSize), uint16_t(kProbeSize));
        bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
        // The sky first and the town over it, in the order submitted.
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(view, faceView, faceProj);

        const float faceParams[4] = {float(face), 0.0f, float(kProbeSize), 0.0f};
        bgfx::setUniform(uProbeFace_, faceParams);
        bindShadeInputs();
        screenPass(view, probeSkyProgram_);

        const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                               BGFX_STATE_DEPTH_TEST_LESS;
        if (ground) submitGround(view, groundShadeProgram_, *ground, state, true);
        if (!batches.empty()) {
            submitBatches(view, shadeProgram_, skinnedShadeProgram_, batches, idb, state, true);
            // The glows too: the fire's own card and the lit windows are what a blade by the
            // fire should catch.
            if (bgfx::isValid(glowProgram_) && bgfx::isValid(skinnedGlowProgram_)) {
                submitBatches(view, glowProgram_, skinnedGlowProgram_, batches, idb,
                              BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
                                  BGFX_STATE_BLEND_ADD,
                              false, true);
            }
        }
    }
    probePass_ = false;
    cullBit_ = BGFX_STATE_CULL_CW;

    chainProbeFace((probeNextFace_ + 5) % 6);
    if (probeNextFace_ == 0) probeFilterDue_ = true;
}

// The chain of the face just drawn: each level the box average of the raw texels under it.
void Renderer::chainProbeFace(int face) {
    for (int level = 0; level < kProbeChain; ++level) {
        const bgfx::ViewId view = bgfx::ViewId(ViewProbeChain + level);
        const uint16_t size = uint16_t(std::max(1, kProbeSize >> level));
        bgfx::setViewFrameBuffer(view, probeChainFb_[level * 6 + face]);
        bgfx::setViewRect(view, 0, 0, size, size);
        bgfx::setViewClear(view, 0, 0, 1.0f, 0);
        bgfx::setViewTransform(view, nullptr, nullptr);
        const float params[4] = {float(face), float(level), float(kProbeSize), 0.0f};
        bgfx::setUniform(uProbeFace_, params);
        bgfx::setTexture(0, sSource_, probeRaw_);
        screenPass(view, probeDownProgram_);
    }
}

// Every mip of every face of the prefiltered copy, from the raw cube and its chain. The
// reflection moves on once a cycle, seven frames, which on lobes this blurred reads as smooth.
void Renderer::filterProbe() {
    for (int mip = 0; mip < kProbeMips; ++mip) {
        const uint16_t size = uint16_t(std::max(1, kProbeSize >> mip));
        for (int face = 0; face < 6; ++face) {
            const bgfx::ViewId view = bgfx::ViewId(ViewProbeFilter + mip * 6 + face);
            bgfx::setViewFrameBuffer(view, probeFilterFb_[mip * 6 + face]);
            bgfx::setViewRect(view, 0, 0, size, size);
            bgfx::setViewClear(view, 0, 0, 1.0f, 0);
            bgfx::setViewTransform(view, nullptr, nullptr);
            const float params[4] = {float(face), float(mip) / float(kProbeMips - 1),
                                     float(kProbeSize), float(kProbeChain - 1)};
            bgfx::setUniform(uProbeFace_, params);
            bgfx::setTexture(0, sSource_, probeChain_);
            screenPass(view, probeFilterProgram_);
        }
    }
}

}  // namespace mu::gfx
