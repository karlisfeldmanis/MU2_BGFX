// The gold ring round whatever the pointer is over: a silhouette, measured by drawing the
// hovered thing a second time into a mask of its own rather than by inflating a hull, and
// sized in screen pixels so it reads the same on a spider at your feet and one across the
// square. MU2's Outline.cs, migrated off Godot; see docs/sprints/09-the-ring.md.
#include "gfx/renderer.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "gfx/renderer_internal.h"
#include "gfx/views.h"

namespace mu::gfx {

bool Renderer::createOutline(const std::string& shaderDir) {
    outlineProgram_ = loadProgram(shaderDir, "vs_screen", "fs_outline");
    uOutlineEdge_ = bgfx::createUniform("u_outlineEdge", bgfx::UniformType::Vec4);
    uOutlineParams_ = bgfx::createUniform("u_outlineParams", bgfx::UniformType::Vec4);
    uOutlinePixel_ = bgfx::createUniform("u_outlinePixel", bgfx::UniformType::Vec4);
    uOutlineDrift_ = bgfx::createUniform("u_outlineDrift", bgfx::UniformType::Vec4);
    uOutlineScale_ = bgfx::createUniform("u_outlineScale", bgfx::UniformType::Vec4);
    sOutlineMask_ = bgfx::createUniform("s_mask", bgfx::UniformType::Sampler);

    // R8: coverage is all this holds. Clamped, so a tap that strays past the mask's own edge
    // -- widening the search rings near the box's border -- finds the empty margin rather
    // than wrapping onto the far side of the texture.
    const uint64_t clamp = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    outlineMaskTex_ = bgfx::createTexture2D(uint16_t(kOutlineMaskSize), uint16_t(kOutlineMaskSize),
                                            false, 1, bgfx::TextureFormat::R8, clamp);
    outlineMaskFb_ = bgfx::createFrameBuffer(1, &outlineMaskTex_, true);

    const bool ok = bgfx::isValid(outlineProgram_) && bgfx::isValid(outlineMaskTex_) &&
                    bgfx::isValid(outlineMaskFb_);
    if (!ok) {
        core::logError("outline: program %d mask %d buffer %d", bgfx::isValid(outlineProgram_),
                       bgfx::isValid(outlineMaskTex_), bgfx::isValid(outlineMaskFb_));
    }
    return ok;
}

void Renderer::destroyOutline() {
    if (bgfx::isValid(outlineMaskFb_)) bgfx::destroy(outlineMaskFb_);
    outlineMaskFb_ = BGFX_INVALID_HANDLE;
    outlineMaskTex_ = BGFX_INVALID_HANDLE;  // the frame buffer owned it
    if (bgfx::isValid(outlineProgram_)) bgfx::destroy(outlineProgram_);
    outlineProgram_ = BGFX_INVALID_HANDLE;
    for (bgfx::UniformHandle* u : {&uOutlineEdge_, &uOutlineParams_, &uOutlinePixel_,
                                   &uOutlineDrift_, &uOutlineScale_, &sOutlineMask_}) {
        if (bgfx::isValid(*u)) bgfx::destroy(*u);
        *u = BGFX_INVALID_HANDLE;
    }
    outlineOk_ = false;
}

void Renderer::drawOutline(const float* mainView, const Camera& camera,
                           const OutlineParams& params, const std::vector<Drawable>& hovered) {
    if (!outlineOk_ || hovered.empty() || width_ <= 0 || height_ <= 0) return;
    if (params.screenW <= 0 || params.screenH <= 0) return;

    const int maskW = std::min(params.screenW, kOutlineMaskSize);
    const int maskH = std::min(params.screenH, kOutlineMaskSize);

    // --- the mask's own camera: the SAME eye, a frustum narrowed to the thing's own
    // rectangle of the main picture -- Outline.Fit's off-axis trick, done here with bx's
    // asymmetric mtxProj instead of Godot's FrustumOffset. Any other way of getting a
    // smaller picture (a plain viewport into the full render, or a narrower plain FOV)
    // either costs the whole screen's worth of pixels or stretches the box into a shape
    // that no longer matches the real one, and the ring would sit beside the thing rather
    // than round it.
    const float halfH = std::tan(bx::toRad(camera.fovDegrees) * 0.5f);
    const float halfW = halfH * (float(width_) / float(height_));
    const float fTop = float(params.screenY) / float(height_);
    const float fBottom = float(params.screenY + maskH) / float(height_);
    const float fLeft = float(params.screenX) / float(width_);
    const float fRight = float(params.screenX + maskW) / float(width_);
    // bx::mtxProj's asymmetric form wants these as physical coordinates AT the near plane,
    // not bare tangents -- its own width/height come out as 2*near/(rt-lt), which only
    // reduces to the plain tan(fovy/2) form when the tangent is first scaled by near. Passed
    // unscaled, as this did at first, the frustum came out wrong by a factor of 1/near --
    // invisible at near 1, and this project's near is 0.05, so every ring drew off the mask
    // entirely. bx's own symmetric fovy overload never multiplies by near because it never
    // goes through this path at all; it sets the matrix's scale terms directly.
    const float ut = halfH * (1.0f - 2.0f * fTop) * camera.nearPlane;
    const float dt = halfH * (1.0f - 2.0f * fBottom) * camera.nearPlane;
    const float lt = -halfW * (1.0f - 2.0f * fLeft) * camera.nearPlane;
    const float rt = -halfW * (1.0f - 2.0f * fRight) * camera.nearPlane;
    float maskProj[16];
    bx::mtxProj(maskProj, ut, dt, lt, rt, camera.nearPlane, camera.farPlane,
               bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);

    // --- the batches: grouped by mesh, the same way draw() does it, but over a handful of
    // instances rather than a town, so no fade or caster split is worth the code.
    outlineBatches_.clear();
    std::vector<std::vector<const Drawable*>> groups;
    {
        std::unordered_map<const content::Mesh*, size_t> seen;
        for (const Drawable& d : hovered) {
            if (!d.mesh) continue;
            auto found = seen.find(d.mesh);
            if (found == seen.end()) {
                seen.emplace(d.mesh, groups.size());
                groups.emplace_back();
                groups.back().push_back(&d);
                outlineBatches_.push_back(Batch{d.mesh, 0, 0, false});
            } else {
                groups[found->second].push_back(&d);
            }
        }
    }
    if (groups.empty()) return;

    uint32_t total = 0;
    for (const auto& g : groups) total += uint32_t(g.size());
    const uint32_t stride = 96;
    const uint32_t available = bgfx::getAvailInstanceDataBuffer(total, stride);
    if (available < total) total = available;
    if (total == 0) return;
    bgfx::InstanceDataBuffer idb = {};
    bgfx::allocInstanceDataBuffer(&idb, total, stride);
    uint32_t written = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        outlineBatches_[gi].first = written;
        uint32_t count = 0;
        for (const Drawable* d : groups[gi]) {
            if (written >= total) break;
            std::memcpy(idb.data + written * stride, d->transform, sizeof(float) * 16);
            const float light[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            std::memcpy(idb.data + written * stride + sizeof(float) * 16, light, sizeof(light));
            // y is the FADE, and it must be 1 here rather than 0. vs_skinned_depth sends
            // `2 + fade` in v_light.w and fs_shadow dithers the fragment away when that fade
            // is under 1 -- which at 0 discards every pixel, so the mask came back empty and
            // the ring drew nothing at all. The silhouette is the shape of the thing, not of
            // how far in it has faded: a corpse going out under the pointer is still ringed
            // by its own outline while it is there to be ringed.
            const float skin[4] = {float(d->paletteRow < 0 ? kBindRow : d->paletteRow), 1.0f, 0.0f,
                                   0.0f};
            std::memcpy(idb.data + written * stride + sizeof(float) * 20, skin, sizeof(skin));
            ++written;
            ++count;
        }
        outlineBatches_[gi].count = count;
    }

    // --- the mask: the hovered thing's meshes, and nothing else, over nothing -------------
    bgfx::setViewFrameBuffer(ViewOutlineMask, outlineMaskFb_);
    bgfx::setViewRect(ViewOutlineMask, 0, 0, uint16_t(maskW), uint16_t(maskH));
    bgfx::setViewClear(ViewOutlineMask, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    bgfx::setViewTransform(ViewOutlineMask, mainView, maskProj);
    submitBatches(ViewOutlineMask, shadowProgram_, skinnedShadowProgram_, outlineBatches_, idb,
                 BGFX_STATE_WRITE_RGB, false);

    // --- the ring: one screen pass, its view rect the thing's own box of the backbuffer ---
    bgfx::setViewFrameBuffer(ViewOutline, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewOutline, uint16_t(std::max(0, params.screenX)),
                      uint16_t(std::max(0, params.screenY)), uint16_t(params.screenW),
                      uint16_t(params.screenH));
    bgfx::setViewClear(ViewOutline, 0, 0, 1.0f, 0);
    bgfx::setViewTransform(ViewOutline, nullptr, nullptr);

    // Gold, MU2's own: Outline.Width, Outline.Shade and the shader's own drift and spread,
    // carried over unchanged since they were the tuned numbers, not guesses.
    const float edge[4] = {1.0f, 0.78f, 0.28f, 1.0f};
    const float outlineParams[4] = {kOutlineWidth, 0.9f, params.shadow ? 0.5f : 0.0f, 0.0f};
    // One texel of the PHYSICAL mask texture, not of the box: the box fills only its own
    // corner of the fixed kOutlineMaskSize square (see u_outlineScale in fs_outline.sc), and
    // a step sized to the box's own width would search too far or too little depending on
    // how much smaller than the cap the box happened to be.
    const float pixel[4] = {1.0f / float(kOutlineMaskSize), 1.0f / float(kOutlineMaskSize), 0.0f,
                            0.0f};
    const float drift[4] = {2.0f, 3.0f, 4.0f, 0.0f};
    const float scale[4] = {float(maskW) / float(kOutlineMaskSize),
                            float(maskH) / float(kOutlineMaskSize), 0.0f, 0.0f};
    bgfx::setUniform(uOutlineEdge_, edge);
    bgfx::setUniform(uOutlineParams_, outlineParams);
    bgfx::setUniform(uOutlinePixel_, pixel);
    bgfx::setUniform(uOutlineDrift_, drift);
    bgfx::setUniform(uOutlineScale_, scale);
    bgfx::setTexture(0, sOutlineMask_, outlineMaskTex_);
    bgfx::setVertexBuffer(0, screenVb_);
    bgfx::setState(BGFX_STATE_WRITE_RGB |
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(ViewOutline, outlineProgram_);
    ++drawCount_;
}

}  // namespace mu::gfx
