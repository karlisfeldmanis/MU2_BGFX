// The town's static scenery, culled and drawn by the GPU itself (2026-10-09, the user: "move
// more processes to gpu", then "do next").
//
// The resident buffer (Renderer::ResidentBatch, game/world/town.h) already kept every
// placement's instance on the GPU; what was left on the CPU was encoding the draws, one a
// model part, each with its textures, uniforms and buffers bound again -- about 54% of the
// main thread in Lorencia's town, measured. Here every static model's vertices and indices
// sit in one buffer pair, and compute steps cull and write the draws:
//
// - **The sun's split** (sprint C4 of docs/perf-audit-2k.md): every placement's sphere
//   tested against the split, the ones inside copied into their model's run of `out`, one
//   indirect draw a solid part. Two submits, one and two sided.
// - **The camera's prepass and shade**: a solid part's three sheets (albedo, normal, ORM) are
//   copied into texture arrays, one set a size and format, a layer a part's sheets; each
//   part's placements in the camera's frustum are copied into its own run of `cameraOut`
//   with the instance's seventh vec4 set to its layer and its material numbers, and the
//   shaders read their sheets by that layer (vs_static's v_material, fs_*_merged, common.sh's
//   MU2_MATERIAL_ARRAYS). Two submits a size, one and two sided.
//
// A part goes to the sun when the sun draws it whole: not a glow, not a cutout or soft alpha,
// not dithered. It goes to the camera when it also has three sheets of one size, no emissive,
// no translucency and no slide. Skinned models keep their own draws, as does everything
// refused; the renderer leaves out exactly what was taken.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/mesh.h"
#include "content/texture.h"
#include "gfx/renderer.h"

namespace mu::gfx {

class GpuScenery {
public:
    // What the sun takes whole.
    static bool castsWhole(const content::Material& m) {
        return !m.glow && m.glowShadow <= 0.0f && m.cutout < 0.0f && !m.softAlpha;
    }
    // What the camera takes, sheets aside: lit by u_material's numbers alone.
    static bool shadesPlain(const content::Material& m) {
        return castsWhole(m) && m.translucency <= 0.0f && m.scrollPerSecond == 0.0f &&
               !m.waterFrames;
    }

    // At load, each static model's own vertices and indices, which are not kept anywhere else.
    void addMesh(const content::Mesh* mesh, const content::Vertex* vertices, uint32_t vertexCount,
                 const uint32_t* indices, uint32_t indexCount);
    // Once the resident buffer is made: its runs, marked `merged` where the sun takes them,
    // the buffer (made with BGFX_BUFFER_COMPUTE_READ), how many placements it holds, and the
    // textures the parts' sheets were loaded by. Frees what addMesh kept. False leaves every
    // run as it was.
    bool build(std::vector<Renderer::ResidentBatch>& batches,
               bgfx::DynamicVertexBufferHandle resident, uint32_t slots,
               const content::Textures& textures);
    void shutdown();
    bool ready() const { return bgfx::isValid(indirect_); }

    // The arrays are filled by blits on the GPU, over the first frames that want them; the
    // camera's draws wait until the last (cameraReady).
    void copyArrays(bgfx::ViewId view);

    // --- the sun's (renderer_scenery.cpp) ---------------------------------------------
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

    // --- the camera's -----------------------------------------------------------------
    bool cameraReady() const { return bgfx::isValid(cameraIndirect_) && arraysCopied_; }
    // A bit a part of `mesh`, set where the camera's draws took it; 0 for a mesh not taken.
    uint64_t cameraParts(const content::Mesh* mesh) const;
    // A run of the camera's draws that shares a set of arrays and a sidedness.
    struct Group {
        uint32_t first = 0;
        uint32_t count = 0;
        bool twoSided = false;
        uint32_t set = 0;  // into sets()
    };
    struct Set {
        bgfx::TextureHandle albedo = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle normal = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle orm = BGFX_INVALID_HANDLE;
    };
    const std::vector<Group>& groups() const { return groups_; }
    const std::vector<Set>& sets() const { return sets_; }
    // Three vec4s a draw: (index count, first index, base vertex, model), (its run's first
    // record, 0, 0, 0), (layer, u_material.y's flags, roughness factor, metal factor).
    bgfx::VertexBufferHandle cameraDraws() const { return cameraDraws_; }
    // A model's placements in the camera's frustum, and each placement's rank among them.
    bgfx::DynamicIndexBufferHandle cameraCounts() const { return cameraCounts_; }
    bgfx::DynamicIndexBufferHandle cameraRank() const { return cameraRank_; }
    bgfx::DynamicVertexBufferHandle cameraOut() const { return cameraOut_; }
    bgfx::IndirectBufferHandle cameraIndirect() const { return cameraIndirect_; }
    uint32_t cameraDrawCount() const { return cameraDrawCount_; }
    uint32_t cameraRecords() const { return cameraRecords_; }
    // The most placements any one model has: the cull's width.
    uint32_t widestModel() const { return widestModel_; }

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

    // The camera's.
    void buildCamera(const std::vector<Renderer::ResidentBatch>& batches,
                     const std::vector<Range>& ranges, const std::vector<int>& modelOf,
                     const content::Textures& textures);
    std::vector<std::pair<const content::Mesh*, uint64_t>> cameraParts_;  // sorted by mesh
    std::vector<Group> groups_;
    std::vector<Set> sets_;
    struct Blit {
        bgfx::TextureHandle to = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle from = BGFX_INVALID_HANDLE;
        uint16_t layer = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        uint8_t mips = 1;
    };
    std::vector<Blit> blits_;
    size_t nextBlit_ = 0;
    bool arraysCopied_ = false;
    bgfx::VertexBufferHandle cameraDraws_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle cameraCounts_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle cameraRank_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle cameraOut_ = BGFX_INVALID_HANDLE;
    bgfx::IndirectBufferHandle cameraIndirect_ = BGFX_INVALID_HANDLE;
    uint32_t cameraDrawCount_ = 0;
    uint32_t cameraRecords_ = 0;
    uint32_t widestModel_ = 0;
};

}  // namespace mu::gfx
