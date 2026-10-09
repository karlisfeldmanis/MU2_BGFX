#include "gfx/casters.h"

#include "core/log.h"

namespace mu::gfx {
namespace {

// What the compute steps read their tables as: a vec4 a row.
const bgfx::VertexLayout& rowLayout() {
    static const bgfx::VertexLayout layout = [] {
        bgfx::VertexLayout l;
        l.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
        return l;
    }();
    return layout;
}

// The tables carry whole numbers as floats; a float holds them exactly up to here.
constexpr uint32_t kExact = 1u << 24;

}  // namespace

void GpuCasters::addMesh(const content::Mesh* mesh, const content::Vertex* vertices,
                         uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount) {
    if (mesh == nullptr || vertexCount == 0 || indexCount == 0) return;
    ranges_.push_back(Range{mesh, uint32_t(vertexData_.size()), uint32_t(indexData_.size())});
    vertexData_.insert(vertexData_.end(), vertices, vertices + vertexCount);
    indexData_.insert(indexData_.end(), indices, indices + indexCount);
}

bool GpuCasters::build(std::vector<Renderer::ResidentBatch>& batches,
                       bgfx::DynamicVertexBufferHandle resident, uint32_t slots) {
    // What addMesh kept is taken before shutdown() clears it, and freed when this returns.
    std::vector<Range> ranges = std::move(ranges_);
    std::vector<content::Vertex> vertexData = std::move(vertexData_);
    std::vector<uint32_t> indexData = std::move(indexData_);
    shutdown();
    const bgfx::Caps* caps = bgfx::getCaps();
    const bool able = (caps->supported & BGFX_CAPS_COMPUTE) != 0 &&
                      (caps->supported & BGFX_CAPS_DRAW_INDIRECT) != 0;
    if (!able || !bgfx::isValid(resident) || slots == 0 || ranges.empty()) {
        core::logf("casters stay batches: %s", !able ? "no compute or indirect draws on this GPU"
                                               : ranges.empty() ? "no static models"
                                                                : "no resident buffer");
        return false;
    }
    if (indexData.size() >= kExact || vertexData.size() >= kExact) {
        core::logError("casters: %zu indices and %zu vertices is past what the tables hold",
                       indexData.size(), vertexData.size());
        return false;
    }

    std::vector<uint32_t> slotModel(slots, 0xffffffffu);
    std::vector<float> models;
    std::vector<float> oneSided, twoSided;
    uint32_t modelCount = 0;
    for (Renderer::ResidentBatch& batch : batches) {
        batch.merged = false;
        if (batch.mesh == nullptr || batch.mesh->isSkinned() || batch.count == 0) continue;
        const Range* range = nullptr;
        for (const Range& r : ranges) {
            if (r.mesh == batch.mesh) range = &r;
        }
        if (range == nullptr) continue;
        const content::Mesh& mesh = *batch.mesh;
        bool any = false;
        for (const content::Part& part : mesh.parts()) {
            const content::Material& m = mesh.materials()[part.material];
            if (!takes(m)) continue;
            std::vector<float>& to = m.twoSided ? twoSided : oneSided;
            to.insert(to.end(), {float(part.indexCount), float(range->firstIndex + part.firstIndex),
                                 float(range->baseVertex), float(modelCount)});
            any = true;
        }
        if (!any) continue;
        // The sphere grown by a quarter: the water plants sway (common.sh's swayed) past it.
        const content::Bounds& b = mesh.bounds();
        models.insert(models.end(), {b.centre[0], b.centre[1], b.centre[2], b.radius * 1.25f,
                                     float(batch.first), float(batch.count), 0.0f, 0.0f});
        for (uint32_t s = batch.first; s < batch.first + batch.count && s < slots; ++s) {
            slotModel[s] = modelCount;
        }
        batch.merged = true;
        ++modelCount;
    }
    if (modelCount == 0) {
        core::logf("casters stay batches: no solid part of a static model to take");
        return false;
    }

    oneSided_ = uint32_t(oneSided.size() / 4);
    twoSided_ = uint32_t(twoSided.size() / 4);
    std::vector<float> draws = std::move(oneSided);
    draws.insert(draws.end(), twoSided.begin(), twoSided.end());
    slots_ = slots;
    modelCount_ = modelCount;
    resident_ = resident;

    vb_ = bgfx::createVertexBuffer(
        bgfx::copy(vertexData.data(), uint32_t(vertexData.size() * sizeof(content::Vertex))),
        content::Mesh::layout());
    ib_ = bgfx::createIndexBuffer(
        bgfx::copy(indexData.data(), uint32_t(indexData.size() * sizeof(uint32_t))),
        BGFX_BUFFER_INDEX32);
    slotModel_ = bgfx::createIndexBuffer(
        bgfx::copy(slotModel.data(), uint32_t(slotModel.size() * sizeof(uint32_t))),
        BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ);
    models_ = bgfx::createVertexBuffer(bgfx::copy(models.data(), uint32_t(models.size() * sizeof(float))),
                                       rowLayout(), BGFX_BUFFER_COMPUTE_READ);
    draws_ = bgfx::createVertexBuffer(bgfx::copy(draws.data(), uint32_t(draws.size() * sizeof(float))),
                                      rowLayout(), BGFX_BUFFER_COMPUTE_READ);
    counts_ = bgfx::createDynamicIndexBuffer(modelCount, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    out_ = bgfx::createDynamicVertexBuffer(slots, Renderer::instanceLayout(), BGFX_BUFFER_COMPUTE_WRITE);
    indirect_ = bgfx::createIndirectBuffer(drawCount());

    core::logf("casters on the GPU: %u models, %u draws (%u one sided, %u two sided), "
               "%zu vertices and %zu indices merged, %.1f MB",
               modelCount_, drawCount(), oneSided_, twoSided_, vertexData.size(), indexData.size(),
               double(vertexData.size() * sizeof(content::Vertex) + indexData.size() * 4 +
                      size_t(slots) * Renderer::kInstanceFloats * 4) / (1024.0 * 1024.0));
    return ready();
}

void GpuCasters::shutdown() {
    auto drop = [](auto& handle) {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
        handle.idx = bgfx::kInvalidHandle;
    };
    drop(vb_);
    drop(ib_);
    drop(slotModel_);
    drop(models_);
    drop(draws_);
    drop(counts_);
    drop(out_);
    drop(indirect_);
    resident_ = BGFX_INVALID_HANDLE;
    ranges_.clear();
    vertexData_.clear();
    indexData_.clear();
    slots_ = modelCount_ = oneSided_ = twoSided_ = 0;
}

}  // namespace mu::gfx
