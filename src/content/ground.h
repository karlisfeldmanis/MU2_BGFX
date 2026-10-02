// One world's land: the terrain mesh MU2's pipeline already built, the surface table that
// says what each of its parts wears, and the grids that say how high and how walkable each
// tile is.
//
// The ground does not fit the closed material model in docs/conventions.md, and is the one
// thing allowed not to: it blends up to three full material sets by weights shared across
// every tile on a corner, and carries MU's own baked light in its vertex colour. That is a
// second shader, not a fourth flag.
#pragma once

#include <array>
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
    // a: the weight MU painted from base to overlay, as the glb carried it. Read into
    // `weight` at load and zeroed; not drawn by.
    float colour[4];
    // How much of each of its part's three layers this corner is, before the height blend
    // bites. Summing to one. See Ground::splat for why a corner is shared by every tile on it.
    float weight[4];
};
static_assert(sizeof(GroundVertex) == 64, "the ground vertex layout drifted");

// One half of a surface: a full material set and how often it repeats.
struct GroundLayer {
    bgfx::TextureHandle albedo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normal = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle orm = BGFX_INVALID_HANDLE;
    float repeat = 0.25f;
    float relief = 1.0f;
    bool water = false;
};

// One drawn part of the land: every tile wearing the same set of up to three materials.
struct GroundPart {
    static constexpr int kLayers = 3;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t surface = 0;
    // layers[0] is always real; the rest repeat it where the part has fewer, so every
    // sampler is bound to something, and `layerCount` says how many the vertices weigh.
    GroundLayer layers[kLayers];
    int layerCount = 1;
    // Each layer's slot in Ground::weights(), or -1 where the part is drawn by the vertex
    // weights instead (a bench plot, or a world with no tile grid).
    int slots[kLayers] = {-1, -1, -1};
    std::string name;      // the glTF material's own name, or the set of slots it wears
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

    // Every tile slot's weight at every grid corner, four slots to an RGBA8 band, or invalid
    // where the land is drawn by pair. weightSize() is (width, height, rows a band) in texels.
    static constexpr int kWeightPad = 2;
    bgfx::TextureHandle weights() const { return weights_; }
    // The level each point of the map is dark below, in metres: its own height on ground, the
    // nearest rim's across a NoGround chasm. Invalid on a world with no chasm. With it, u_abyss:
    // x where the dark starts below that level, y over how many metres it is whole (0 is off),
    // zw the texture's uv as world x and -z times z plus w. abyss() in common.sh.
    bgfx::TextureHandle abyss() const { return abyss_; }
    const float* abyssParams() const { return abyssParams_; }
    const float* weightSize() const { return weightSize_; }
    // Which texel row of weights() the water's flow band starts on, kWeightPad included, or
    // -1 where the world names no river: its water then slides along U as MU's does. A
    // corner's texel is (dx, dy) of its flow in tiles a cycle over kFlowReach, stored
    // 128 + 127 v, and z a smooth noise that staggers the cycle. buildFlow.
    float flowRow() const { return flowRow_; }
    // A flowing sheet is two copies cross-faded, each dragged along the flow for kFlowCycle
    // seconds and then taken back; kFlowReach is the furthest a copy is dragged, in tiles.
    static constexpr float kFlowReach = 1.2f;
    static constexpr float kFlowCycle = 3.0f;

    static const bgfx::VertexLayout& layout();

private:
    bool readGrids(const std::string& worldDir, const std::string& heightFile,
                   const std::string& attributesFile);
    // Re-cuts the land by material rather than by pair. False, and nothing touched, when
    // the tile grid or the slot table is missing or the mesh is not one quad a tile.
    bool splat(std::vector<GroundVertex>& vertices, std::vector<uint32_t>& indices,
               const std::vector<GroundLayer>& slotLayers);

    std::vector<GroundPart> parts_;
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    uint32_t indexCount_ = 0;
    bgfx::TextureHandle weights_ = BGFX_INVALID_HANDLE;
    float weightSize_[3] = {1.0f, 1.0f, 1.0f};
    // Where each river is fed and where it drains, as tile (column, row), out of the world
    // json's "water_flow". Empty on a world that names none.
    std::vector<std::array<int, 2>> flowSources_;
    std::vector<std::array<int, 2>> flowSinks_;
    float flowRow_ = -1.0f;

    int size_ = 0;
    float metresPerTile_ = 1.0f;
    float heightFactor_ = 1.5f;
    // How a NoGround chasm's edge goes into the dark: the land sinks `voidSink_` metres over
    // `voidFade_` tiles from the last drawn ground (Ground::splat), and everything below the
    // rim's level is taken to black between `abyssStart_` and `abyssStart_ + abyssDepth_`
    // metres down (buildAbyss). The world json's "void" overrides all four. Invention: MU
    // cuts the tile and shows the clear colour.
    void buildAbyss();
    float voidFade_ = 0.0f;
    float voidSink_ = 8.0f;
    float abyssStart_ = 1.5f;
    float abyssDepth_ = 5.0f;
    // A corner touching the void takes the highest ground beside it as its rim, not its own
    // height: the Lost Tower's causeways, whose sides fall to the void's corners at 0 and with
    // no wall over them stood up as lit grey slopes. The world's `void.rim`.
    bool abyssRim_ = false;
    // How many tiles in from the void the ground fades to its black, in the frame and after
    // the haze (fs_ground's abyssEdge), so a floor's last metres reach the void's own black:
    // the Lost Tower's causeways, which MU's terrain light takes down to its black fog. 0 off.
    // The world's `void.blend`.
    float abyssBlend_ = 0.0f;
    // How far above the rim the void's own points read their level, in metres, so whatever
    // stands in a pit is already that far into the dark at the lip: the Dungeon's worms,
    // whose crowns sit level with the floor and stood fully lit in the hole. 0 off. The
    // world's `void.lift`. Ours.
    float abyssLift_ = 0.0f;
    bgfx::TextureHandle abyss_ = BGFX_INVALID_HANDLE;
    float abyssParams_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
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
