#include "content/cooked.h"

#include <cstring>

#include "content/reader.h"

namespace mu::content {

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
    if (version < 3 || version > 4) {
        error = "a .mum of version " + std::to_string(version) +
                ", and this reads 3 and 4. Versions 1 and 2 are the same file without glTF's "
                "roughness and metal factors, which 195 of this content's material slots "
                "carry their whole answer in; recook.";
        return false;
    }
    // Version 4 is a skinned mesh: a bone count here, 56-byte vertices below, and the skin's
    // own bone table after the materials. Version 3 is the static mesh the town is made of
    // -- a figure needs a bigger vertex and a skeleton, and the town's 2753 placements have
    // no business paying eight bytes each for joints they do not have.
    const bool skinned = version == 4;
    uint32_t bones = 0;
    if (skinned) reader.read(bones);
    const size_t vertexSize = skinned ? sizeof(CookedSkinnedVertex) : sizeof(CookedVertex);
    if (!plausible(reader, vertices, vertexSize) || !plausible(reader, indices, 4)) {
        error = "claims more vertices or indices than it holds";
        return false;
    }

    if (skinned) {
        out.skinned.resize(vertices);
        reader.take(out.skinned.data(), size_t(vertices) * sizeof(CookedSkinnedVertex));
    } else {
        out.vertices.resize(vertices);
        reader.take(out.vertices.data(), size_t(vertices) * sizeof(CookedVertex));
    }
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
        reader.read(material.roughnessFactor);
        reader.read(material.metalFactor);
        if (reader.failed()) break;
    }

    if (bones > 0) {
        if (!plausible(reader, bones, 4 + 64)) {
            error = "claims more bones than it holds";
            return false;
        }
        out.bones.resize(bones);
        for (CookedBone& bone : out.bones) {
            reader.readString(bone.name);
            reader.read(bone.parent);
            reader.take(bone.inverseBind, sizeof(bone.inverseBind));
            if (reader.failed()) break;
        }
    }
    if (reader.failed()) {
        error = "ends in the middle of itself";
        return false;
    }

    // A pose is one walk of this array and never a recursion, which is only right if a
    // parent always precedes its child. The cook orders them so; this is where that is
    // relied upon, so this is where it is checked.
    for (size_t i = 0; i < out.bones.size(); ++i) {
        const int32_t parent = out.bones[i].parent;
        if (parent >= int32_t(i) || parent < -1) {
            error = "bone " + std::to_string(i) + " names parent " + std::to_string(parent) +
                    ", which does not stand before it";
            return false;
        }
    }
    if (bones > 0) {
        for (const CookedSkinnedVertex& vertex : out.skinned) {
            for (uint8_t joint : vertex.joints) {
                if (joint >= bones) {
                    error = "a vertex names joint " + std::to_string(joint) + " of " +
                            std::to_string(bones);
                    return false;
                }
            }
        }
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

bool parseCookedClips(const std::vector<uint8_t>& bytes, CookedClips& out, std::string& error) {
    Reader reader(bytes.data(), bytes.size());

    char magic[4] = {};
    uint32_t version = 0, clips = 0, bones = 0;
    reader.take(magic, 4);
    reader.read(version);
    reader.read(clips);
    reader.read(bones);
    if (reader.failed() || std::memcmp(magic, "MU2C", 4) != 0) {
        error = "not a .muc";
        return false;
    }
    if (version != 1) {
        error = "a .muc of version " + std::to_string(version) + ", and this reads version 1";
        return false;
    }
    if (bones == 0) {
        error = "says it animates no bones at all";
        return false;
    }
    out.bones = bones;

    out.boneNames.resize(bones);
    for (std::string& name : out.boneNames) {
        if (!reader.readString(name)) break;
    }
    if (reader.failed()) {
        error = "ends inside its bone names";
        return false;
    }

    out.clips.resize(clips);
    uint64_t frames = 0;
    for (CookedClip& clip : out.clips) {
        reader.readString(clip.name);
        reader.readString(clip.label);
        reader.read(clip.slot);
        reader.read(clip.frames);
        reader.read(clip.duration);
        reader.read(clip.travel);
        uint32_t hold = 0;
        reader.read(hold);
        clip.hold = hold != 0;
        clip.firstRow = uint32_t(frames * bones);
        frames += clip.frames;
        if (reader.failed()) break;
    }
    if (reader.failed()) {
        error = "ends inside its clip table";
        return false;
    }

    const size_t floats = size_t(frames) * bones * CookedClips::kFloatsPerBone;
    if (floats * sizeof(float) != reader.left()) {
        error = "claims " + std::to_string(frames) + " frames of " + std::to_string(bones) +
                " bones and holds " + std::to_string(reader.left()) + " bytes of pose";
        return false;
    }
    out.rows.resize(floats);
    reader.take(out.rows.data(), floats * sizeof(float));
    if (reader.failed()) {
        error = "ends in the middle of its frames";
        return false;
    }

    // A clip of no frames has no pose to read and would be played as a division by zero;
    // a duration of zero is the same thing wearing a clock.
    for (const CookedClip& clip : out.clips) {
        if (clip.frames == 0) {
            error = "clip " + clip.name + " holds no frames";
            return false;
        }
        if (clip.frames > 1 && !(clip.duration > 0.0f)) {
            error = "clip " + clip.name + " runs " + std::to_string(clip.frames) +
                    " frames in no time at all";
            return false;
        }
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
