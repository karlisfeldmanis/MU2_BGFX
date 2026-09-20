// The cook's own formats, read back: .mum (one model) and .mut (one town).
//
// This header knows nothing about bgfx on purpose. Parsing a file and uploading buffers are
// two jobs, and only the second needs a device: split, the first can be tested without a
// window, which is what `tests/cooked_test.cpp` does and what foundation 9 of PLAN.md asks
// of everything that can manage it. `content/mesh.cpp` does the upload from what comes out
// of here.
//
// The formats themselves are written down in tools/cook.py, which is the only thing that
// writes them. Every count in a file is treated as hostile here: a truncated or edited file
// must be refused with a reason, never walked off the end of.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::content {

// A vertex exactly as the cook writes it and as the static draw's layout wants it. The
// assert is the contract between tools/cook.py and content/mesh.h.
struct CookedVertex {
    float position[3];
    float normal[3];
    float tangent[4];
    float uv[2];
};
static_assert(sizeof(CookedVertex) == 48, "the cooked vertex layout drifted from the cook's");

// A material as a file can hold one: four texture paths under assets/, the two flags a path
// cannot carry, and glTF's two scalar factors. The handles are made later, by whoever has a
// device.
//
// The factors are not decoration. glTF defines roughness and metal as `factor * texture`, and
// MU2's pipeline uses exactly that split: a surface whose relief was derived from its art gets
// a texture and factors of 1.0, and a surface that declares no grain -- foliage, water, every
// leaf and blade in Lorencia -- gets no texture at all and carries its whole answer in the
// factor. 195 of this content's 729 material slots are the second kind, and reading only the
// texture threw every one of them away.
struct CookedMaterial {
    std::string name;
    std::string albedo;
    std::string normal;
    std::string orm;
    std::string emissive;
    float cutout = -1.0f;
    bool twoSided = false;
    float roughnessFactor = 1.0f;
    float metalFactor = 1.0f;
};

// A skinned vertex is the static one with four joint bytes and four weight bytes on the end,
// 56 bytes. The joints are bytes rather than the shorts glTF stores because bgfx has no
// unsigned 16-bit vertex attribute at all, and the largest rig in this content is 115 joints;
// the cook does that conversion and refuses a rig that would not survive it.
struct CookedSkinnedVertex {
    float position[3];
    float normal[3];
    float tangent[4];
    float uv[2];
    uint8_t joints[4];
    uint8_t weights[4];  // normalised, and the cook made them sum to 255 exactly
};
static_assert(sizeof(CookedSkinnedVertex) == 56, "the skinned vertex layout drifted");

// One bone of one skin, in the skin's own joint order. `parent` is an index into that same
// order, or -1: the cook checks that a parent always precedes its child, so a pose is one
// walk of a flat array rather than a recursion.
struct CookedBone {
    std::string name;
    int32_t parent = -1;
    float inverseBind[16];
};

struct CookedPart {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t material = 0;
};

struct CookedMesh {
    std::vector<CookedVertex> vertices;         // one of these two is filled and the other
    std::vector<CookedSkinnedVertex> skinned;   // is empty; `bones` says which
    std::vector<uint32_t> indices;
    std::vector<CookedPart> parts;
    std::vector<CookedMaterial> materials;
    std::vector<CookedBone> bones;
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};

    bool isSkinned() const { return !bones.empty(); }
    size_t vertexCount() const { return isSkinned() ? skinned.size() : vertices.size(); }
};

// One clip, baked flat: no key times and no samplers, because every channel of every clip in
// this content shares one key-time list and every sampler is LINEAR. `frames` is the source's
// own key count at MU's own rate (`action_speeds` x 25 Hz, never above 25), kept rather than
// resampled -- resampling the player library to a fixed 25 Hz trebles it and adds nothing.
//
// A looping clip carries one extra key holding the first pose again, so that the wrap has an
// interval to happen over. It is kept, and the clock wraps in [0, duration) rather than the
// frame index wrapping in [0, frames): that is what makes the wrap an interpolation instead
// of the first pose played twice. `hold` says the clip stops on its last frame instead --
// MU's `monster_holds`, which is the death and nothing else.
struct CookedClip {
    std::string name;   // MU's own, `action15`
    std::string label;  // what index.json calls that slot: "Walk male"
    int32_t slot = -1;  // 15, or -1 for a clip whose name is not MU's
    uint32_t frames = 0;
    float duration = 0.0f;  // seconds
    float travel = 0.0f;    // metres the clip is meant to carry the figure over one cycle
    bool hold = false;
    uint32_t firstRow = 0;  // into `rows`, counted in bones
};

// One clip library: the player's 283, an NPC's 2, or a monster's 7. `rows` is frame-major --
// a frame's bones are contiguous, because what reads this walks two whole frames and blends
// them -- and each row is a rotation quaternion (x, y, z, w) and a translation, local to the
// bone's parent. No scale: nothing in this content animates one.
struct CookedClips {
    std::vector<std::string> boneNames;
    std::vector<CookedClip> clips;
    std::vector<float> rows;  // (frames x bones) x 7 floats
    uint32_t bones = 0;

    static constexpr uint32_t kFloatsPerBone = 7;
};

// One model the town places, and where its mesh is.
struct TownModel {
    std::string name;
    std::string mesh;   // a path under assets/
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    uint32_t instances = 0;
};

// A block of tiles with its own box. Culling tests this, and what survives is a run of
// instances rather than a list of objects.
struct TownChunk {
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
    uint16_t column = 0;
    uint16_t row = 0;
};

// 36 bytes, and the cook packs exactly these fields in exactly this order.
struct TownInstance {
    float position[3];
    float yaw;
    float pitch;
    float roll;
    float scale;
    uint16_t model;
    uint16_t flags;    // bit 0: laid on the terrain rather than placed as the map stores it
    uint8_t light[3];  // MU's baked terrain light at this tile
    uint8_t spare;
};
static_assert(sizeof(TownInstance) == 36, "the town instance layout drifted from the cook's");

struct CookedTown {
    uint32_t size = 0;        // tiles a side
    uint32_t chunkTiles = 0;  // a chunk's side, in tiles
    float metresPerTile = 1.0f;
    std::vector<TownModel> models;
    std::vector<TownChunk> chunks;
    std::vector<TownInstance> instances;
};

// Both return false and fill `error` with a sentence rather than throwing or logging: the
// caller knows which file it asked for and the test wants the reason.
bool parseCookedMesh(const std::vector<uint8_t>& bytes, CookedMesh& out, std::string& error);
bool parseCookedTown(const std::vector<uint8_t>& bytes, CookedTown& out, std::string& error);
bool parseCookedClips(const std::vector<uint8_t>& bytes, CookedClips& out, std::string& error);

}  // namespace mu::content
