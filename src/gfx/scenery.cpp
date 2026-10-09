#include "gfx/scenery.h"

#include <algorithm>
#include <map>
#include <tuple>

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

void GpuScenery::addMesh(const content::Mesh* mesh, const content::Vertex* vertices,
                         uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount) {
    if (mesh == nullptr || vertexCount == 0 || indexCount == 0) return;
    ranges_.push_back(Range{mesh, uint32_t(vertexData_.size()), uint32_t(indexData_.size())});
    vertexData_.insert(vertexData_.end(), vertices, vertices + vertexCount);
    indexData_.insert(indexData_.end(), indices, indices + indexCount);
}

bool GpuScenery::build(std::vector<Renderer::ResidentBatch>& batches,
                       bgfx::DynamicVertexBufferHandle resident, uint32_t slots,
                       const content::Textures& textures) {
    // What addMesh kept is taken before shutdown() clears it, and freed when this returns.
    std::vector<Range> ranges = std::move(ranges_);
    std::vector<content::Vertex> vertexData = std::move(vertexData_);
    std::vector<uint32_t> indexData = std::move(indexData_);
    shutdown();
    const bgfx::Caps* caps = bgfx::getCaps();
    const bool able = (caps->supported & BGFX_CAPS_COMPUTE) != 0 &&
                      (caps->supported & BGFX_CAPS_DRAW_INDIRECT) != 0;
    if (!able || !bgfx::isValid(resident) || slots == 0 || ranges.empty()) {
        core::logf("scenery stays batches: %s", !able ? "no compute or indirect draws on this GPU"
                                                : ranges.empty() ? "no static models"
                                                                 : "no resident buffer");
        return false;
    }
    if (indexData.size() >= kExact || vertexData.size() >= kExact) {
        core::logError("scenery: %zu indices and %zu vertices is past what the tables hold",
                       indexData.size(), vertexData.size());
        return false;
    }

    std::vector<uint32_t> slotModel(slots, 0xffffffffu);
    std::vector<float> models;
    std::vector<float> oneSided, twoSided;
    std::vector<int> modelOf(batches.size(), -1);  // a run's model in the tables, or -1
    std::vector<Range> rangeOf(batches.size());
    uint32_t modelCount = 0;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        Renderer::ResidentBatch& batch = batches[bi];
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
            if (!castsWhole(m)) continue;
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
        modelOf[bi] = int(modelCount);
        rangeOf[bi] = *range;
        ++modelCount;
    }
    if (modelCount == 0) {
        core::logf("scenery stays batches: no solid part of a static model to take");
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

    core::logf("scenery on the GPU: %u models, the sun's %u draws (%u one sided, %u two sided), "
               "%zu vertices and %zu indices merged, %.1f MB",
               modelCount_, drawCount(), oneSided_, twoSided_, vertexData.size(), indexData.size(),
               double(vertexData.size() * sizeof(content::Vertex) + indexData.size() * 4 +
                      size_t(slots) * Renderer::kInstanceFloats * 4) / (1024.0 * 1024.0));

    buildCamera(batches, rangeOf, modelOf, textures);
    return ready();
}

void GpuScenery::buildCamera(const std::vector<Renderer::ResidentBatch>& batches,
                             const std::vector<Range>& rangeOf, const std::vector<int>& modelOf,
                             const content::Textures& textures) {
    // A set is the three sheets' shared size and each one's format, mips and flags: what one
    // array a role must agree in. A layer is one part's three sheets.
    using SetKey = std::tuple<uint32_t, uint32_t, int, int, int, int, int, int, uint64_t,
                              uint64_t, uint64_t>;
    struct SetBuild {
        content::Textures::Info albedo, normal, orm;
        std::map<std::tuple<uint16_t, uint16_t, uint16_t>, uint16_t> layerOf;
        std::vector<std::tuple<bgfx::TextureHandle, bgfx::TextureHandle, bgfx::TextureHandle>> layers;
    };
    std::map<SetKey, uint32_t> setOf;
    std::vector<SetBuild> builds;
    struct Entry {
        uint32_t set = 0;
        bool twoSided = false;
        float draw[4];      // index count, first index, base vertex, model
        float material[4];  // layer, flags, roughness factor, metal factor
        uint32_t records = 0;  // the model's placements: its run's room
    };
    std::vector<Entry> entries;
    uint32_t refused = 0;
    uint32_t widest = 0;

    for (size_t bi = 0; bi < batches.size(); ++bi) {
        if (modelOf[bi] < 0) continue;
        const Renderer::ResidentBatch& batch = batches[bi];
        const content::Mesh& mesh = *batch.mesh;
        uint64_t mask = 0;
        for (size_t pi = 0; pi < mesh.parts().size() && pi < 64; ++pi) {
            const content::Part& part = mesh.parts()[pi];
            const content::Material& m = mesh.materials()[part.material];
            if (!shadesPlain(m)) continue;
            content::Textures::Info a, n, o, e;
            if (!textures.infoOf(m.albedo, &a) || !textures.infoOf(m.normal, &n) ||
                !textures.infoOf(m.orm, &o) || textures.infoOf(m.emissive, &e) ||
                a.width != n.width || a.width != o.width || a.height != n.height ||
                a.height != o.height) {
                ++refused;
                continue;
            }
            const SetKey key{a.width, a.height, int(a.format), int(n.format), int(o.format),
                             a.mips, n.mips, o.mips, a.flags, n.flags, o.flags};
            auto found = setOf.find(key);
            if (found == setOf.end()) {
                found = setOf.emplace(key, uint32_t(builds.size())).first;
                builds.push_back(SetBuild{a, n, o, {}, {}});
            }
            SetBuild& set = builds[found->second];
            const auto triple = std::make_tuple(m.albedo.idx, m.normal.idx, m.orm.idx);
            auto layer = set.layerOf.find(triple);
            if (layer == set.layerOf.end()) {
                layer = set.layerOf.emplace(triple, uint16_t(set.layers.size())).first;
                set.layers.emplace_back(m.albedo, m.normal, m.orm);
            }
            Entry entry;
            entry.set = found->second;
            entry.twoSided = m.twoSided;
            const Range& range = rangeOf[bi];
            entry.draw[0] = float(part.indexCount);
            entry.draw[1] = float(range.firstIndex + part.firstIndex);
            entry.draw[2] = float(range.baseVertex);
            entry.draw[3] = float(modelOf[bi]);
            // u_material.y's flags as submitBatches packs them: 1 two-sided, 2 calibrated.
            entry.material[0] = float(layer->second);
            entry.material[1] = (m.twoSided ? 1.0f : 0.0f) + (m.calibrated ? 2.0f : 0.0f);
            entry.material[2] = m.roughnessFactor;
            entry.material[3] = m.metalFactor;
            entry.records = batch.count;
            entries.push_back(entry);
            mask |= uint64_t(1) << pi;
        }
        if (mask != 0) {
            cameraParts_.emplace_back(&mesh, mask);
            widest = std::max(widest, batch.count);
        }
    }
    if (entries.empty()) {
        core::logf("the camera's scenery stays batches: no part with three sheets of one size");
        cameraParts_.clear();
        return;
    }

    // The arrays, made empty and filled by copyArrays.
    for (SetBuild& b : builds) {
        Set set;
        const uint16_t layers = uint16_t(b.layers.size());
        // Never fewer than two: bgfx makes one layer a plain 2D texture, which an array sampler
        // reads as nothing -- Noria's mushrooms drew grey, each the one part of its size.
        const uint16_t made = std::max<uint16_t>(layers, 2);
        auto make = [&](const content::Textures::Info& info) {
            const uint64_t flags = info.flags | BGFX_TEXTURE_BLIT_DST;
            if (!bgfx::isTextureValid(0, info.mips > 1, made, info.format, flags)) {
                return bgfx::TextureHandle{bgfx::kInvalidHandle};
            }
            return bgfx::createTexture2D(uint16_t(info.width), uint16_t(info.height),
                                         info.mips > 1, made, info.format, flags);
        };
        set.albedo = make(b.albedo);
        set.normal = make(b.normal);
        set.orm = make(b.orm);
        sets_.push_back(set);
        for (uint16_t l = 0; l < layers; ++l) {
            const auto& [albedo, normal, orm] = b.layers[l];
            const uint16_t w = uint16_t(b.albedo.width), h = uint16_t(b.albedo.height);
            blits_.push_back(Blit{set.albedo, albedo, l, w, h, b.albedo.mips});
            blits_.push_back(Blit{set.normal, normal, l, w, h, b.normal.mips});
            blits_.push_back(Blit{set.orm, orm, l, w, h, b.orm.mips});
        }
    }
    for (const Set& s : sets_) {
        if (!bgfx::isValid(s.albedo) || !bgfx::isValid(s.normal) || !bgfx::isValid(s.orm)) {
            core::logError("the camera's scenery: an array was refused; it stays batches");
            for (Set& d : sets_) {
                for (bgfx::TextureHandle* t : {&d.albedo, &d.normal, &d.orm}) {
                    if (bgfx::isValid(*t)) bgfx::destroy(*t);
                }
            }
            sets_.clear();
            blits_.clear();
            cameraParts_.clear();
            return;
        }
    }

    // The draws by set, then one sided before two, so a group is a run.
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.set != b.set ? a.set < b.set : (!a.twoSided && b.twoSided);
    });
    std::vector<float> table;
    table.reserve(entries.size() * 12);
    uint32_t records = 0;
    for (uint32_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (groups_.empty() || groups_.back().set != e.set || groups_.back().twoSided != e.twoSided) {
            groups_.push_back(Group{i, 0, e.twoSided, e.set});
        }
        ++groups_.back().count;
        table.insert(table.end(), e.draw, e.draw + 4);
        table.insert(table.end(), {float(records), 0.0f, 0.0f, 0.0f});
        table.insert(table.end(), e.material, e.material + 4);
        records += e.records;
    }
    if (records >= kExact) {
        core::logError("the camera's scenery: %u records is past what the tables hold", records);
        return;
    }
    std::sort(cameraParts_.begin(), cameraParts_.end());
    cameraDrawCount_ = uint32_t(entries.size());
    cameraRecords_ = records;
    widestModel_ = widest;
    cameraDraws_ = bgfx::createVertexBuffer(
        bgfx::copy(table.data(), uint32_t(table.size() * sizeof(float))), rowLayout(),
        BGFX_BUFFER_COMPUTE_READ);
    cameraCounts_ = bgfx::createDynamicIndexBuffer(
        modelCount_, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    cameraRank_ = bgfx::createDynamicIndexBuffer(
        slots_, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    cameraOut_ = bgfx::createDynamicVertexBuffer(records, Renderer::instanceLayout(), BGFX_BUFFER_COMPUTE_WRITE);
    cameraIndirect_ = bgfx::createIndirectBuffer(cameraDrawCount_);

    size_t layers = 0;
    for (const SetBuild& b : builds) layers += b.layers.size();
    core::logf("the camera's scenery on the GPU: %u draws in %zu groups over %zu sets of "
               "arrays, %zu layers, %u records; %u parts kept as batches",
               cameraDrawCount_, groups_.size(), sets_.size(), layers, records, refused);
}

void GpuScenery::copyArrays(bgfx::ViewId view) {
    if (arraysCopied_ || blits_.empty()) return;
    // A frame holds 1,024 blits (BGFX_CONFIG_MAX_BLIT_ITEMS) and Lorencia's arrays want about
    // 1,750, a level of a sheet each; so a few frames, and the camera's draws wait for the last.
    constexpr uint32_t kBlitsAFrame = 768;
    uint32_t issued = 0;
    while (nextBlit_ < blits_.size()) {
        const Blit& b = blits_[nextBlit_];
        if (issued + b.mips > kBlitsAFrame) return;
        issued += b.mips;
        ++nextBlit_;
        for (uint8_t mip = 0; mip < b.mips; ++mip) {
            const uint16_t w = uint16_t(std::max(1, b.width >> mip));
            const uint16_t h = uint16_t(std::max(1, b.height >> mip));
            bgfx::TextureRegion to;
            to.handle = b.to;
            to.mip = mip;
            to.z = b.layer;
            to.width = w;
            to.height = h;
            to.depth = 1;
            bgfx::TextureRegion from = to;
            from.handle = b.from;
            from.z = 0;
            bgfx::blit(view, to, from);
        }
    }
    blits_.clear();
    nextBlit_ = 0;
    arraysCopied_ = true;
}

uint64_t GpuScenery::cameraParts(const content::Mesh* mesh) const {
    auto found = std::lower_bound(
        cameraParts_.begin(), cameraParts_.end(), mesh,
        [](const std::pair<const content::Mesh*, uint64_t>& p, const content::Mesh* m) {
            return p.first < m;
        });
    return found != cameraParts_.end() && found->first == mesh ? found->second : 0;
}

void GpuScenery::shutdown() {
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
    drop(cameraDraws_);
    drop(cameraCounts_);
    drop(cameraRank_);
    drop(cameraOut_);
    drop(cameraIndirect_);
    for (Set& s : sets_) {
        drop(s.albedo);
        drop(s.normal);
        drop(s.orm);
    }
    sets_.clear();
    groups_.clear();
    blits_.clear();
    nextBlit_ = 0;
    cameraParts_.clear();
    arraysCopied_ = false;
    resident_ = BGFX_INVALID_HANDLE;
    ranges_.clear();
    vertexData_.clear();
    indexData_.clear();
    slots_ = modelCount_ = oneSided_ = twoSided_ = 0;
    cameraDrawCount_ = cameraRecords_ = widestModel_ = 0;
}

}  // namespace mu::gfx
