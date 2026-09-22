// The point lights, and the grid the shade pass reads them out of: the town's static lamps
// and fires laid once (setPointLights), their levels flickered per frame
// (setPointLightLevels), and what burns and MOVES handed over every frame including the frame
// it becomes none (setTransientLights). docs/sprints/08a-the-lamps.md.
#include "gfx/renderer.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "gfx/renderer_internal.h"
#include "gfx/views.h"

namespace mu::gfx {

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

void Renderer::setTransientLights(const PointLight* lights, uint32_t count) {
    if (lights == nullptr) count = 0;
    if (count > kMaxTransientLights) {
        core::logError("%u transient lights and the frame holds %u; the rest are dark", count,
                       kMaxTransientLights);
        count = kMaxTransientLights;
    }
    transientCount_ = count;
    // Packed as setPointLights packs a static one into the lamp texture's two rows: position
    // and reach, then colour and height. The difference is that there is no separate level
    // here -- a static lamp's flicker arrives later, per frame, through setPointLightLevels
    // and multiplies a colour kept aside in lampColour_, while a mover is set whole every
    // frame anyway, so whatever is flickering it has already multiplied it in.
    for (uint32_t i = 0; i < transientCount_; ++i) {
        const PointLight& one = lights[i];
        float* at = &transientAt_[size_t(i) * 4];
        at[0] = one.position[0];
        at[1] = one.position[1];
        at[2] = one.position[2];
        at[3] = one.reach;
        float* lit = &transientColour_[size_t(i) * 4];
        lit[0] = one.colour[0];
        lit[1] = one.colour[1];
        lit[2] = one.colour[2];
        lit[3] = one.height;
    }
    // What the shader loops to. The rest of u_lampParams is the static path's and is untouched.
    lampParams_[2] = float(transientCount_);
}

}  // namespace mu::gfx
