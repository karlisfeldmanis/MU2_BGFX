// One world's land: the terrain mesh MU2's pipeline already built, the surface table that
// says what each of its parts wears, and the grids that say how high and how walkable each
// tile is.
//
// The ground does not fit the closed material model in docs/conventions.md, and is the one
// thing allowed not to: it blends two full material sets by a per-vertex weight and carries
// MU's own baked light in the same attribute. That is a second shader, not a fourth flag.
#pragma once

#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/texture.h"

namespace mu::content {

// The ground's own vertex. No tangent: the UVs are world-axis-aligned, so the frame is
// analytic in the shader.
struct GroundVertex {
    float position[3];
    float normal[3];
    float uv[2];      // in TILES, not in [0,1]; each half multiplies by its own repeat
    // rgb: MU's baked TerrainLight, which multiplies the ALBEDO and nothing else.
    // a: the weight MU painted from base to overlay, before the height blend bites.
    float colour[4];
};
static_assert(sizeof(GroundVertex) == 48, "the ground vertex layout drifted");

// One half of a surface: a full material set and how often it repeats.
struct GroundLayer {
    bgfx::TextureHandle albedo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normal = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle orm = BGFX_INVALID_HANDLE;
    float repeat = 0.25f;
    float relief = 1.0f;
    bool water = false;
};

// One drawn part of the land: every tile wearing the same pair.
struct GroundPart {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t surface = 0;
    GroundLayer base;
    GroundLayer overlay;
    bool hasOverlay = false;
    std::string name;      // the glTF material's own name
    std::string pairName;  // the same pair as ground_surfaces.json's entry names it
};

class Ground {
public:
    // `worldDir` is the folder holding lorencia.json and the rest.
    bool load(const std::string& worldDir, const std::string& worldName, Textures& textures);
    void shutdown();

    const std::vector<GroundPart>& parts() const { return parts_; }
    bgfx::VertexBufferHandle vertexBuffer() const { return vbh_; }
    bgfx::IndexBufferHandle indexBuffer() const { return ibh_; }

    int size() const { return size_; }
    float metresPerTile() const { return metresPerTile_; }

    // The land's height in metres at a point, bilinear across the tile the point falls in.
    // Off the map returns 0.
    float heightAt(float x, float z) const;

    // MU's own attribute bits for a tile. 0 off the map -- which is also MU's value for open
    // walkable ground, so this alone never answers "may something stand here". See the note
    // on the definition, and ask walkable().
    uint8_t attributesAt(int column, int row) const;
    bool walkable(int column, int row) const;

    uint32_t triangleCount() const { return indexCount_ / 3; }

    static const bgfx::VertexLayout& layout();

private:
    bool readGrids(const std::string& worldDir, const std::string& heightFile,
                   const std::string& attributesFile);

    std::vector<GroundPart> parts_;
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    uint32_t indexCount_ = 0;

    int size_ = 0;
    float metresPerTile_ = 1.0f;
    float heightFactor_ = 1.5f;
    std::vector<float> height_;    // metres, [row * size + column]
    std::vector<uint8_t> attrs_;   // MU's own bits
};

}  // namespace mu::content
