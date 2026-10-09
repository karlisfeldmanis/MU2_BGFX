// The town's solid casters, culled and drawn by the GPU itself (2026-10-09, the user: "move
// more processes to gpu").
//
// The resident buffer (Renderer::ResidentBatch, game/world/town.h) already kept every
// placement's instance on the GPU; what was left on the CPU was encoding the sun's pass, one
// draw a model part -- 161 of its 280 draws in Lorencia's town, and each one a texture, two
// uniforms and three buffers bound again. And the pass drew all 4,195 instances whatever the
// split held, against the camera's 1,046.
//
// Here every static model's vertices and indices sit in one buffer pair, and three compute
// steps on the sun's own view do the rest each frame (shaders/cs_cast_*.sc): nought each
// model's count, test each placement's sphere against the split and copy the ones inside into
// their model's run of `out`, then write one indirect draw a solid part. The renderer submits
// them as two draws, one sided and two sided, which Metal runs back to back with nothing
// bound between them.
//
// A part goes in when the sun draws it whole: not a glow, not a cutout or soft alpha (those
// read their sheet), not dithered. Skinned models keep their own draws, as does everything
// takes() refuses; the renderer leaves out only what was taken (Batch::merged).
#pragma once

#include <cstdint>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/mesh.h"
#include "gfx/renderer.h"

namespace mu::gfx {

class GpuCasters {
public:
    static bool takes(const content::Material& m) {
        return !m.glow && m.glowShadow <= 0.0f && m.cutout < 0.0f && !m.softAlpha;
    }

    // At load, each static model's own vertices and indices, which are not kept anywhere else.
    void addMesh(const content::Mesh* mesh, const content::Vertex* vertices, uint32_t vertexCount,
                 const uint32_t* indices, uint32_t indexCount);
    // Once the resident buffer is made: its runs, marked `merged` where this took them, the
    // buffer (made with BGFX_BUFFER_COMPUTE_READ) and how many placements it holds. Frees
    // what addMesh kept. False leaves every run as it was.
    bool build(std::vector<Renderer::ResidentBatch>& batches,
               bgfx::DynamicVertexBufferHandle resident, uint32_t slots);
    void shutdown();
    bool ready() const { return bgfx::isValid(indirect_); }

    // What the renderer reads (renderer_casters.cpp).
    bgfx::VertexBufferHandle vertices() const { return vb_; }
    bgfx::IndexBufferHandle indices() const { return ib_; }
    bgfx::DynamicVertexBufferHandle resident() const { return resident_; }
    bgfx::IndexBufferHandle slotModel() const { return slotModel_; }
    bgfx::VertexBufferHandle models() const { return models_; }
    bgfx::VertexBufferHandle draws() const { return draws_; }
    bgfx::DynamicIndexBufferHandle counts() const { return counts_; }
    bgfx::DynamicVertexBufferHandle out() const { return out_; }
    bgfx::IndirectBufferHandle indirect() const { return indirect_; }
    uint32_t slotCount() const { return slots_; }
    uint32_t modelCount() const { return modelCount_; }
    uint32_t drawCount() const { return oneSided_ + twoSided_; }
    // The draws are one sided first, then two sided.
    uint32_t oneSided() const { return oneSided_; }
    uint32_t twoSided() const { return twoSided_; }

private:
    struct Range {
        const content::Mesh* mesh = nullptr;
        uint32_t baseVertex = 0;
        uint32_t firstIndex = 0;
    };
    std::vector<Range> ranges_;
    std::vector<content::Vertex> vertexData_;
    std::vector<uint32_t> indexData_;

    bgfx::VertexBufferHandle vb_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ib_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle resident_ = BGFX_INVALID_HANDLE;  // the town's, borrowed
    bgfx::IndexBufferHandle slotModel_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle models_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle draws_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle counts_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle out_ = BGFX_INVALID_HANDLE;
    bgfx::IndirectBufferHandle indirect_ = BGFX_INVALID_HANDLE;
    uint32_t slots_ = 0;
    uint32_t modelCount_ = 0;
    uint32_t oneSided_ = 0;
    uint32_t twoSided_ = 0;
};

}  // namespace mu::gfx
