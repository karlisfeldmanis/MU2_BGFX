// The sun's split draws the town's solid casters off the GPU's own cull. gfx/casters.h.
#include <cmath>

#include "gfx/casters.h"
#include "gfx/renderer.h"
#include "gfx/views.h"

namespace mu::gfx {

bool Renderer::drawGpuCasters(const GpuCasters& c, const float* m, uint64_t state) {
    if (!gpuCasting_ || !c.ready() || !bgfx::isValid(castClearProgram_) ||
        !bgfx::isValid(castCullProgram_) || !bgfx::isValid(castArgsProgram_) ||
        !bgfx::isValid(whiteAo_)) {
        return false;
    }

    // The split's six planes out of the matrix it is drawn with, as game/world/town.cpp's
    // Frustum takes the camera's: row-vector, so m[row * 4 + column] and w is column 3, and
    // Metal's near plane is z alone. Normalised, so a sphere's radius is a distance to them.
    float planes[6][4];
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
    for (float* p : planes) {
        const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (length > 0.0f) {
            for (int i = 0; i < 4; ++i) p[i] /= length;
        }
    }

    // Three steps on the sun's own view. bgfx sorts a view's compute ahead of its draws, and
    // Metal runs one encoder's dispatches in order, each seeing what the last one wrote.
    const float cast[4] = {float(c.slotCount()), float(c.modelCount()), float(c.drawCount()), 0.0f};
    auto groups = [](uint32_t n) { return uint32_t((n + 63) / 64); };

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

}  // namespace mu::gfx
