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

#include "content/grid.h"
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

    // A bench's land: the same shader, vertex layout and surface pair out of the world's own
    // ground_surfaces.json, over a small synthetic heightfield instead of a whole map. Real
    // terrain to judge a material on, without raising 256 squares of Lorencia to do it. See
    // the note at its definition for why it is synthetic rather than a corner of the map.
    bool buildPlot(const std::string& worldDir, const std::string& worldName, int tiles,
                   int surfaceIndex, Textures& textures);
    void shutdown();

    const std::vector<GroundPart>& parts() const { return parts_; }
    bgfx::VertexBufferHandle vertexBuffer() const { return vbh_; }
    bgfx::IndexBufferHandle indexBuffer() const { return ibh_; }

    int size() const { return size_; }
    float metresPerTile() const { return metresPerTile_; }

    // The land's height in metres at a point, bilinear across the tile the point falls in.
    // Off the map returns 0.
    float heightAt(float x, float z) const;

    // MU's own attribute word for a tile. 0 off the map -- which is also MU's value for open
    // walkable ground, so this alone never answers "may something stand here". Ask walkable(),
    // which is Grid::open and is the same function the sim walks by.
    uint16_t attributesAt(int column, int row) const;
    bool walkable(int column, int row) const;

    // The land's own grid, which is what the sim is handed when the window plays. There is one
    // of these and one passability test in the engine; see content/grid.h for why that had to
    // be said out loud.
    const Grid& grid() const { return grid_; }

    // The tile texture a square is floored with -- tiles.png's red, MU's base layer -- or -1
    // off the map and on a world whose json names no tile grid. It is what MU's indoor test
    // reads: see World::indoors.
    int floorAt(int column, int row) const;

    // The other two channels of the same grid, which nothing read until the grass did.
    // tiles.png is (layer1, layer2, alpha): MU's base slot, its overlay slot, and how far the
    // overlay has been painted over the base. pipeline/terrain.py writes all three and the
    // ground mesh bakes the alpha into its vertex colour, which is why the engine never
    // needed the grid itself before. Grass does: where the paving has been painted over the
    // lawn there should be less lawn, and only this says so per tile.
    int overlayAt(int column, int row) const;
    float blendAt(int column, int row) const;

    // Whether a tile slot is one of MU's grass sheets, out of the world's own `tile_slots`
    // table. Asked by name rather than by number: Lorencia's grass is slots 0 and 1 and
    // Noria's is not the same pair, and MU's own rule -- BITMAP_MAPGRASS + layer1, so slots
    // 0, 1 and 2 -- would grow grass on Lorencia's TileGround01 because it is third in the
    // table rather than because it is grass.
    bool grassFloor(int slot) const;

    // What the world's `tile_slots` calls a slot -- "TileGrass01" -- or empty. The grass reads
    // it to find the painted sheet MU floors that slot with: MU's own rule is
    // BITMAP_MAPGRASS + layer1, an index, and an index is exactly what goes wrong when a world
    // orders its slots differently. The name does not.
    const std::string& floorName(int slot) const;

    // MU's baked TerrainLight at a tile, linear, 0..1 a channel. The same light the ground
    // mesh carries in its vertex colour, read off the grid instead so that something standing
    // ON the ground can be given the light of the tile it stands on. NOT sRGB-decoded: it is
    // a lit result and not an albedo. docs/conventions.md.
    void lightAt(int column, int row, float* rgb) const;

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
    std::vector<float> height_;  // metres, [row * size + column]
    std::vector<uint8_t> floors_;   // tiles.png red: MU's base slot; empty when absent
    std::vector<uint8_t> overlays_; // tiles.png green: MU's overlay slot
    std::vector<uint8_t> blends_;   // tiles.png blue: how far the overlay is painted over it
    std::vector<uint8_t> light_;    // light.png, three bytes a tile; empty when absent
    std::vector<bool> grassSlots_;  // which entries of the world's tile_slots are TileGrass*
    std::vector<std::string> slotNames_;  // and what each of them is called
    Grid grid_;
};

}  // namespace mu::content
