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

// A material as a file can hold one: four texture paths under assets/, and the two flags a
// path cannot carry. The handles are made later, by whoever has a device.
struct CookedMaterial {
    std::string name;
    std::string albedo;
    std::string normal;
    std::string orm;
    std::string emissive;
    float cutout = -1.0f;
    bool twoSided = false;
};

struct CookedPart {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t material = 0;
};

struct CookedMesh {
    std::vector<CookedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<CookedPart> parts;
    std::vector<CookedMaterial> materials;
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
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

}  // namespace mu::content
