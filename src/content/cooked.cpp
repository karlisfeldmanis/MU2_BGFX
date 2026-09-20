#include "content/cooked.h"

#include <cstring>

namespace mu::content {
namespace {

// A cursor that cannot be walked off the end of. Every read is checked and the first
// failure makes every later one fail too, so the parsing below reads straight down the
// file and asks once, at the end, whether any of it was out of bounds. A file this engine
// wrote is not a file this engine may trust: it is on disk, where anything can edit it.
class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool take(void* out, size_t bytes) {
        if (failed_ || at_ + bytes > size_) {
            failed_ = true;
            return false;
        }
        std::memcpy(out, data_ + at_, bytes);
        at_ += bytes;
        return true;
    }

    template <typename T>
    bool read(T& value) {
        return take(&value, sizeof(T));
    }

    // A length-prefixed string, as write_string() in tools/cook.py writes it.
    bool readString(std::string& value) {
        uint16_t length = 0;
        if (!read(length)) return false;
        if (failed_ || at_ + length > size_) {
            failed_ = true;
            return false;
        }
        value.assign(reinterpret_cast<const char*>(data_ + at_), length);
        at_ += length;
        return true;
    }

    bool failed() const { return failed_; }
    size_t left() const { return failed_ ? 0 : size_ - at_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t at_ = 0;
    bool failed_ = false;
};

// A count is the one number in these files that can make the reader allocate, so it is
// checked against what is left to read before anything is reserved. Without this a file
// claiming four billion vertices asks for 192 GB before it fails.
bool plausible(const Reader& reader, uint32_t count, size_t each) {
    return each == 0 || count <= reader.left() / each;
}

}  // namespace

bool parseCookedMesh(const std::vector<uint8_t>& bytes, CookedMesh& out, std::string& error) {
    Reader reader(bytes.data(), bytes.size());

    char magic[4] = {};
    uint32_t version = 0;
    uint32_t vertices = 0, indices = 0, parts = 0, materials = 0;
    reader.take(magic, 4);
    reader.read(version);
    reader.read(vertices);
    reader.read(indices);
    reader.read(parts);
    reader.read(materials);
    reader.take(out.min, sizeof(out.min));
    reader.take(out.max, sizeof(out.max));
    if (reader.failed() || std::memcmp(magic, "MU2M", 4) != 0) {
        error = "not a .mum";
        return false;
    }
    if (version != 1) {
        error = "a .mum of version " + std::to_string(version) + ", and this reads version 1";
        return false;
    }
    if (!plausible(reader, vertices, sizeof(CookedVertex)) || !plausible(reader, indices, 4)) {
        error = "claims more vertices or indices than it holds";
        return false;
    }

    out.vertices.resize(vertices);
    reader.take(out.vertices.data(), size_t(vertices) * sizeof(CookedVertex));
    out.indices.resize(indices);
    reader.take(out.indices.data(), size_t(indices) * 4);

    if (!plausible(reader, parts, 12)) {
        error = "claims more parts than it holds";
        return false;
    }
    out.parts.resize(parts);
    for (CookedPart& part : out.parts) {
        reader.read(part.firstIndex);
        reader.read(part.indexCount);
        reader.read(part.material);
    }

    out.materials.resize(materials);
    for (CookedMaterial& material : out.materials) {
        reader.read(material.cutout);
        uint8_t twoSided = 0;
        reader.read(twoSided);
        material.twoSided = twoSided != 0;
        reader.readString(material.name);
        reader.readString(material.albedo);
        reader.readString(material.normal);
        reader.readString(material.orm);
        reader.readString(material.emissive);
        if (reader.failed()) break;
    }
    if (reader.failed()) {
        error = "ends in the middle of itself";
        return false;
    }

    // The invariants a draw will rely on without testing them again.
    for (uint32_t index : out.indices) {
        if (index >= vertices) {
            error = "an index reaches vertex " + std::to_string(index) + " of " +
                    std::to_string(vertices);
            return false;
        }
    }
    uint64_t covered = 0;
    for (const CookedPart& part : out.parts) {
        if (uint64_t(part.firstIndex) + part.indexCount > indices) {
            error = "a part runs past the end of the indices";
            return false;
        }
        if (part.material >= materials) {
            error = "a part names material " + std::to_string(part.material) + " of " +
                    std::to_string(materials);
            return false;
        }
        covered += part.indexCount;
    }
    if (covered != indices) {
        error = "its parts cover " + std::to_string(covered) + " indices of " +
                std::to_string(indices);
        return false;
    }
    return true;
}

bool parseCookedTown(const std::vector<uint8_t>& bytes, CookedTown& out, std::string& error) {
    Reader reader(bytes.data(), bytes.size());

    char magic[4] = {};
    uint32_t version = 0, models = 0, chunks = 0, instances = 0;
    reader.take(magic, 4);
    reader.read(version);
    reader.read(models);
    reader.read(chunks);
    reader.read(instances);
    reader.read(out.size);
    reader.read(out.chunkTiles);
    reader.read(out.metresPerTile);
    if (reader.failed() || std::memcmp(magic, "MU2T", 4) != 0) {
        error = "not a .mut";
        return false;
    }
    if (version != 1) {
        error = "a .mut of version " + std::to_string(version) + ", and this reads version 1";
        return false;
    }
    if (out.chunkTiles == 0) {
        error = "says its chunks are zero tiles across";
        return false;
    }

    out.models.resize(models);
    for (TownModel& model : out.models) {
        reader.readString(model.name);
        reader.readString(model.mesh);
        reader.take(model.min, sizeof(model.min));
        reader.take(model.max, sizeof(model.max));
        reader.read(model.instances);
        if (reader.failed()) break;
    }
    if (reader.failed()) {
        error = "ends inside its model table";
        return false;
    }

    if (!plausible(reader, chunks, 36)) {
        error = "claims more chunks than it holds";
        return false;
    }
    out.chunks.resize(chunks);
    for (TownChunk& chunk : out.chunks) {
        reader.take(chunk.min, sizeof(chunk.min));
        reader.take(chunk.max, sizeof(chunk.max));
        reader.read(chunk.firstInstance);
        reader.read(chunk.instanceCount);
        reader.read(chunk.column);
        reader.read(chunk.row);
    }

    if (!plausible(reader, instances, sizeof(TownInstance))) {
        error = "claims more placements than it holds";
        return false;
    }
    out.instances.resize(instances);
    reader.take(out.instances.data(), size_t(instances) * sizeof(TownInstance));
    if (reader.failed()) {
        error = "ends in the middle of its placements";
        return false;
    }

    // What the frame will assume, checked once here instead of every frame there: the runs
    // are contiguous and cover every instance, and a model index names a model.
    uint32_t cursor = 0;
    for (const TownChunk& chunk : out.chunks) {
        if (chunk.firstInstance != cursor) {
            error = "chunk " + std::to_string(chunk.column) + "," + std::to_string(chunk.row) +
                    " starts at " + std::to_string(chunk.firstInstance) + ", not at " +
                    std::to_string(cursor);
            return false;
        }
        if (uint64_t(cursor) + chunk.instanceCount > instances) {
            error = "a chunk runs past the end of the placements";
            return false;
        }
        cursor += chunk.instanceCount;
    }
    if (cursor != instances) {
        error = "its chunks cover " + std::to_string(cursor) + " placements of " +
                std::to_string(instances);
        return false;
    }
    for (const TownInstance& instance : out.instances) {
        if (instance.model >= models) {
            error = "a placement names model " + std::to_string(instance.model) + " of " +
                    std::to_string(models);
            return false;
        }
    }
    return true;
}

}  // namespace mu::content
