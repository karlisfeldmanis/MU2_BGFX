// The item stages' one pass: sprint 7's pictures. Kept out of renderer.cpp because it shares
// nothing with the frame but the meshes, the instance layout and the bone palette -- no
// shadow, no prepass, no AO, no probe -- and a window's picture should not be able to break
// the town's frame or be broken by it.
#include <cstring>
#include <unordered_map>

#include "core/log.h"
#include "gfx/program.h"
#include "gfx/renderer.h"

namespace mu::gfx {

bool Renderer::openStages(const std::string& shaderDir) {
    // Idempotent: the first window to want a picture opens them, and there is no order
    // between that and anything else in the frame.
    if (bgfx::isValid(stageProgram_) && bgfx::isValid(skinnedStageProgram_)) return true;
    stageProgram_ = loadProgramFiles(shaderDir, "vs_static", "fs_stage");
    skinnedStageProgram_ = loadProgramFiles(shaderDir, "vs_skinned", "fs_stage");
    // Without these an item's glow is left off its picture, as it was before there were any.
    stageGlowProgram_ = loadProgramFiles(shaderDir, "vs_static", "fs_stage_glow");
    skinnedStageGlowProgram_ = loadProgramFiles(shaderDir, "vs_skinned", "fs_stage_glow");
    const bool ok = bgfx::isValid(stageProgram_) && bgfx::isValid(skinnedStageProgram_);
    if (!ok) core::logError("the stage programs did not link; the windows draw names, not items");
    return ok;
}

void Renderer::closeStages() {
    if (bgfx::isValid(stageProgram_)) bgfx::destroy(stageProgram_);
    if (bgfx::isValid(skinnedStageProgram_)) bgfx::destroy(skinnedStageProgram_);
    if (bgfx::isValid(stageGlowProgram_)) bgfx::destroy(stageGlowProgram_);
    if (bgfx::isValid(skinnedStageGlowProgram_)) bgfx::destroy(skinnedStageGlowProgram_);
    stageProgram_ = BGFX_INVALID_HANDLE;
    skinnedStageProgram_ = BGFX_INVALID_HANDLE;
    stageGlowProgram_ = BGFX_INVALID_HANDLE;
    skinnedStageGlowProgram_ = BGFX_INVALID_HANDLE;
}

void Renderer::drawStage(bgfx::ViewId viewId, bgfx::FrameBufferHandle target, uint16_t width,
                         uint16_t height, const float* view, const float* proj,
                         const std::vector<Drawable>& drawables) {
    bgfx::setViewName(viewId, "stage");
    bgfx::setViewFrameBuffer(viewId, target);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    // Clear to nothing, so the window shows through wherever there is no item.
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
    bgfx::setViewTransform(viewId, view, proj);
    // An empty stage must still clear: a bag emptied by a sale keeps no ghost of what was in it.
    bgfx::touch(viewId);
    if (drawables.empty() || !bgfx::isValid(stageProgram_)) return;

    // One instance each: a bag holds a few dozen things at most, and grouping them into
    // batches would save nothing a stage drawn on change only could measure.
    const uint32_t stride = 96;
    const uint32_t total = uint32_t(drawables.size());
    if (bgfx::getAvailInstanceDataBuffer(total, stride) < total) {
        core::logError("stage: the instance buffer is full; %u items not drawn", total);
        return;
    }
    bgfx::InstanceDataBuffer idb = {};
    bgfx::allocInstanceDataBuffer(&idb, total, stride);
    for (uint32_t i = 0; i < total; ++i) {
        const Drawable& d = drawables[i];
        std::memcpy(idb.data + i * stride, d.transform, sizeof(float) * 16);
        std::memcpy(idb.data + i * stride + sizeof(float) * 16, d.light, sizeof(float) * 4);
        const float skin[4] = {float(d.paletteRow < 0 ? kBindRow : d.paletteRow), 1.0f,
                               float(d.refine), packRefineColour(d.refineColour)};
        std::memcpy(idb.data + i * stride + sizeof(float) * 20, skin, sizeof(skin));
    }

    const uint64_t base = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                          BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
    for (uint32_t i = 0; i < total; ++i) {
        const Drawable& d = drawables[i];
        if (!d.mesh) continue;
        const content::Mesh& mesh = *d.mesh;
        const bool skinned = mesh.isSkinned();
        for (const content::Part& part : mesh.parts()) {
            const content::Material& material = mesh.materials()[part.material];
            // A glow is MU's additive BlendMesh. It is drawn last-come and added, its colour
            // alone: the picture's alpha stays what the solid parts made it, and the interface
            // lays the picture on premultiplied (Art::premultiplied), so over an empty cell the
            // glow is added to the window. Skipped once, which left the Bluewing Crossbow --
            // every mesh a glow -- an empty cell, and the Legendary Staff without its shaft.
            if (material.glow) {
                const bgfx::ProgramHandle glowProgram =
                    skinned ? skinnedStageGlowProgram_ : stageGlowProgram_;
                if (!bgfx::isValid(glowProgram)) continue;
                // MU's BlendMeshLight at the middle of its breathing: sin() * a + b, at sin 0.
                // y: 8 for a glow a levelled item's chrome stays off, as fs_glow reads it.
                const float glow[4] = {material.cutout, material.noChrome ? 8.0f : 0.0f,
                                       material.pulse[1], 0.0f};
                bgfx::setUniform(uMaterial_, glow);
                bindShine(true);
                bgfx::setTexture(0, sAlbedo_, material.albedo,
                                 material.glowClamped()
                                     ? BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
                                     : UINT32_MAX);
                if (skinned) bgfx::setTexture(12, sBones_, palette_);
                bgfx::setVertexBuffer(0, mesh.vertexBuffer());
                bgfx::setIndexBuffer(mesh.indexBuffer(), part.firstIndex, part.indexCount);
                bgfx::setInstanceDataBuffer(&idb, i, 1);
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
                               BGFX_STATE_MSAA |
                               BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE));
                bgfx::submit(viewId, glowProgram);
                ++drawCount_;
                continue;
            }
            const float params[4] = {material.cutout,
                                     (material.twoSided ? 1.0f : 0.0f) +
                                         (material.calibrated ? 2.0f : 0.0f),
                                     material.roughnessFactor, material.metalFactor};
            bgfx::setUniform(uMaterial_, params);
            bgfx::setTexture(0, sAlbedo_, material.albedo);
            bgfx::setTexture(1, sNormal_, material.normal);
            bgfx::setTexture(2, sOrm_, material.orm);
            bgfx::setTexture(3, sEmissive_, material.emissive);
            if (skinned) bgfx::setTexture(12, sBones_, palette_);
            bindShine(true);
            uint64_t state = base;
            if (!material.twoSided) state |= cullBit_;
            bgfx::setVertexBuffer(0, mesh.vertexBuffer());
            bgfx::setIndexBuffer(mesh.indexBuffer(), part.firstIndex, part.indexCount);
            bgfx::setInstanceDataBuffer(&idb, i, 1);
            bgfx::setState(state);
            bgfx::submit(viewId, skinned ? skinnedStageProgram_ : stageProgram_);
            ++drawCount_;
        }
    }
}

}  // namespace mu::gfx
