// The town's static scenery off the GPU's own cull: the sun's split, and the camera's prepass
// and shade. gfx/scenery.h.
#include <cmath>

#include "gfx/scenery.h"
#include "gfx/renderer.h"
#include "gfx/views.h"

namespace mu::gfx {
namespace {

// The six planes out of the matrix a view is drawn with, as game/world/town.cpp's Frustum
// takes the camera's: row-vector, so m[row * 4 + column] and w is column 3, and Metal's near
// plane is z alone. Normalised, so a sphere's radius is a distance to them.
void frustumPlanes(const float* m, float planes[6][4]) {
    auto set = [&](int index, int column, float sign) {
        for (int row = 0; row < 4; ++row) {
            planes[index][row] = m[row * 4 + 3] + sign * m[row * 4 + column];
        }
    };
    set(0, 0, 1.0f);
    set(1, 0, -1.0f);
    set(2, 1, 1.0f);
    set(3, 1, -1.0f);
    set(4, 2, 1.0f);
    set(5, 2, -1.0f);
    if (!bgfx::getCaps()->homogeneousDepth) {
        for (int row = 0; row < 4; ++row) planes[4][row] = m[row * 4 + 2];
    }
    for (int i = 0; i < 6; ++i) {
        float* p = planes[i];
        const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (length > 0.0f) {
            for (int k = 0; k < 4; ++k) p[k] /= length;
        }
    }
}

uint32_t groupsOf(uint32_t n) { return uint32_t((n + 63) / 64); }

}  // namespace

bool Renderer::drawGpuCasters(const GpuScenery& c, const float* m, uint64_t state) {
    if (!c.ready() || !bgfx::isValid(castClearProgram_) ||
        !bgfx::isValid(castCullProgram_) || !bgfx::isValid(castArgsProgram_) ||
        !bgfx::isValid(whiteAo_)) {
        return false;
    }

    float planes[6][4];
    frustumPlanes(m, planes);

    // Three steps on the sun's own view. bgfx sorts a view's compute ahead of its draws, and
    // Metal runs one encoder's dispatches in order, each seeing what the last one wrote.
    const float cast[4] = {float(c.slotCount()), float(c.modelCount()), float(c.drawCount()), 0.0f};
    auto groups = groupsOf;

    bgfx::setUniform(uCast_, cast);
    bgfx::setBuffer(0, c.counts(), bgfx::Access::Write);
    bgfx::dispatch(ViewShadow, castClearProgram_, groups(c.modelCount()), 1, 1);

    bgfx::setUniform(uCast_, cast);
    bgfx::setUniform(uCastPlanes_, planes, 6);
    bgfx::setBuffer(0, c.resident(), bgfx::Access::Read);
    bgfx::setBuffer(1, c.slotModel(), bgfx::Access::Read);
    bgfx::setBuffer(2, c.models(), bgfx::Access::Read);
    bgfx::setBuffer(3, c.counts(), bgfx::Access::ReadWrite);
    bgfx::setBuffer(4, c.out(), bgfx::Access::Write);
    bgfx::dispatch(ViewShadow, castCullProgram_, groups(c.slotCount()), 1, 1);

    bgfx::setUniform(uCast_, cast);
    bgfx::setBuffer(0, c.draws(), bgfx::Access::Read);
    bgfx::setBuffer(1, c.counts(), bgfx::Access::Read);
    bgfx::setBuffer(2, c.models(), bgfx::Access::Read);
    bgfx::setBuffer(3, c.indirect(), bgfx::Access::ReadWrite);
    bgfx::dispatch(ViewShadow, castArgsProgram_, groups(c.drawCount()), 1, 1);

    // And the draws: vs_depth and fs_shadow as every static caster, with nothing to cut
    // (u_material.x -1) or thin. The albedo is bound because fs_shadow reads its alpha either
    // way; any white will do. u_sway for the water plants, as submitBatches sets it.
    const float material[4] = {-1.0f, 0.0f, 0.0f, 0.0f};
    const float sway[4] = {elapsed_, sway_, 0.0f, 0.0f};
    auto draw = [&](uint32_t first, uint32_t count, uint64_t cull) {
        if (count == 0) return;
        bgfx::setUniform(uMaterial_, material);
        bgfx::setUniform(uSway_, sway);
        bgfx::setTexture(0, sAlbedo_, whiteAo_);
        bgfx::setVertexBuffer(0, c.vertices());
        bgfx::setIndexBuffer(c.indices());
        bgfx::setInstanceDataBuffer(c.out(), 0, c.slotCount());
        bgfx::setState(state | cull);
        bgfx::submit(ViewShadow, shadowProgram_, c.indirect(), uint32_t(first), uint32_t(count));
        ++drawCount_;
    };
    draw(0, c.oneSided(), cullBit_);
    draw(c.oneSided(), c.twoSided(), 0);
    return true;
}

void Renderer::cullGpuScenery(const GpuScenery& c, const float* viewProj) {
    float planes[6][4];
    frustumPlanes(viewProj, planes);
    const float parts = float(c.cameraDrawCount());
    const float cast[4] = {float(c.modelCount()), parts, parts, 0.0f};

    // On the prepass's view, ahead of its draws: rank each model's placements in the frustum
    // in the cook's order, copy them to their ranks, write the draws.
    bgfx::setUniform(uCast_, cast);
    bgfx::setUniform(uCastPlanes_, planes, 6);
    bgfx::setBuffer(0, c.resident(), bgfx::Access::Read);
    bgfx::setBuffer(1, c.models(), bgfx::Access::Read);
    bgfx::setBuffer(2, c.cameraRank(), bgfx::Access::Write);
    bgfx::setBuffer(3, c.cameraCounts(), bgfx::Access::Write);
    bgfx::dispatch(ViewPrepass, sceneRankProgram_, groupsOf(c.modelCount()), 1, 1);

    bgfx::setUniform(uCast_, cast);
    bgfx::setBuffer(0, c.resident(), bgfx::Access::Read);
    bgfx::setBuffer(1, c.models(), bgfx::Access::Read);
    bgfx::setBuffer(2, c.cameraDraws(), bgfx::Access::Read);
    bgfx::setBuffer(3, c.cameraRank(), bgfx::Access::Read);
    bgfx::setBuffer(4, c.cameraOut(), bgfx::Access::Write);
    bgfx::dispatch(ViewPrepass, sceneCullProgram_, groupsOf(c.widestModel()), c.cameraDrawCount(), 1);

    bgfx::setUniform(uCast_, cast);
    bgfx::setBuffer(0, c.cameraDraws(), bgfx::Access::Read);
    bgfx::setBuffer(1, c.cameraCounts(), bgfx::Access::Read);
    bgfx::setBuffer(2, c.cameraIndirect(), bgfx::Access::ReadWrite);
    bgfx::dispatch(ViewPrepass, sceneArgsProgram_, groupsOf(c.cameraDrawCount()), 1, 1);
}

void Renderer::drawGpuScenery(const GpuScenery& c, bgfx::ViewId view, bgfx::ProgramHandle program,
                              uint64_t state, bool shade) {
    // What submitBatches sets a part by part, set once a group: nothing here cuts, slides or
    // shines through (GpuScenery::shadesPlain), and the material rides in the instance.
    const float material[4] = {-1.0f, 0.0f, 1.0f, 1.0f};
    const float sway[4] = {elapsed_, sway_, 0.0f, 0.0f};
    for (const GpuScenery::Group& g : c.groups()) {
        const GpuScenery::Set& set = c.sets()[g.set];
        bgfx::setUniform(uMaterial_, material);
        bgfx::setUniform(uSway_, sway);
        bgfx::setTexture(0, sAlbedo_, set.albedo);
        if (shade) {
            bgfx::setTexture(1, sNormal_, set.normal);
            bgfx::setTexture(2, sOrm_, set.orm);
            bindShadeInputs();
            bindShine();
        }
        bgfx::setVertexBuffer(0, c.vertices());
        bgfx::setIndexBuffer(c.indices());
        bgfx::setInstanceDataBuffer(c.cameraOut(), 0, c.cameraRecords());
        bgfx::setState(state | (g.twoSided ? 0 : cullBit_));
        bgfx::submit(view, program, c.cameraIndirect(), g.first, g.count);
        ++drawCount_;
    }
}

}  // namespace mu::gfx
