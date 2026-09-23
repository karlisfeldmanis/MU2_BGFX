// The near field: MU's own painted grass, scattered. docs/grass.md has the research this came
// out of, the camera arithmetic that sized it, and what was measured.
//
// **This is MU2's Turf, with the placement rebuilt.** MU does not model blades. It paints a
// tuft -- four 64-pixel columns of one, in `assets/effects/grass/<world>_TileGrassNN.png` --
// and stands cut-out cards of it on the land; MU2's Godot client did the same and scattered
// them, and that is the method here. A painted column is eight or ten blades of grass for the
// fill of ONE quad, and this engine measured both ways: geometric blades cost more and read as
// less, because at MU's camera a blade is two pixels wide and there is no room in two pixels
// for the shape you paid for.
//
// What is new is everything round the card. MU stands one quad on each grass tile's edge; MU2
// scattered them at a flat density with a jitter. Here a card's place, size, lean, arch, roll,
// colour, dryness and wind response are each their own draw with their own reason, pulled
// together by two clump fields so the field reads as tufts and patches rather than confetti --
// and the density that thins with distance is a RAMP, so a card grows and shrinks rather than
// blinking on and off as the player walks. That blink is what a field of them twinkling looks
// like, and it is the one thing a scattered field must not do. The other is a rim: every
// distance the field is shaped by is measured from the EYE, not the player, so its far edge
// and its thinning are places on the screen that stay put as he walks. Measured from the
// player they were rings that walked with him, with cards standing up along the far one.
//
// The short version of why the shape is this small. MU's camera is nailed at -48.5 degrees and
// 45 of yaw, 3.5 to 8 m back, and it never turns. The whole visible ground is about 470 square
// metres and its far edge is 19.5 m out -- smaller than Ghost of Tsushima's NEAREST level of
// detail. So there is no tile ring to stream, no Hi-Z to occlude against, no chain of four
// detail levels and no impostor: one band of cards, and the ground texture under and past them.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "content/ground.h"
#include "content/texture.h"
#include "gfx/lighting.h"
#include "gfx/renderer.h"

namespace mu::game {

class Grass {
public:
    // The card strip and the world's painted sheets. The strip is built once: six vertices,
    // two at the root, two half way up and two at the top, which is four triangles. It holds
    // no geometry -- a vertex is (card index, how far up, which side) and grass.sh grows the
    // rest -- and it is the same strip for every card on every map.
    //
    // The sheets are MU's, one per grass tile slot the world's `tile_slots` names, and they
    // are found by that slot's NAME. MU's own rule is BITMAP_MAPGRASS + layer1, an index, and
    // an index is what goes wrong first on a map that orders its slots differently.
    bool build(const std::string& assetDir, const std::string& world, const content::Ground& ground,
               content::Textures& textures);
    void shutdown();

    // Fills `field` with the patches the camera can see. `eye` is where the camera stands and
    // `focus` where it looks, both in world metres. The field's reach is measured from the
    // EYE, per card, in the shader: MU's camera is rigid to the player, so a distance from the
    // eye is a place on the screen, and the far edge and the thinning stay where they are in
    // the frame as he walks. The frustum is what keeps the patches behind him from being
    // sent. Returns false when there is nothing to draw -- no grid, no grass slots, no
    // sheets, the sheet's `grass` at 0, or every patch culled.
    bool gather(const content::Ground& ground, const gfx::Lighting& look, const float* viewProj,
                const float* eye, const float* focus, float seconds, gfx::GrassField& field);

    // What the last gather did, for the readout.
    struct Counts {
        uint32_t considered = 0;  // tiles inside the square the disc is cut out of
        uint32_t grassy = 0;      // of those, tiles whose floor is one of MU's grass sheets
        uint32_t drawn = 0;       // of those, patches that survived the disc and the frustum
        uint32_t cards = 0;       // the drawn patches' card budget, before the shader thins it
    };
    const Counts& counts() const { return counts_; }

    // The stratification grid's side, and its square. A card stands in its own cell of this
    // grid with a jitter, so the field has neither a grid's regularity nor a free hash's bald
    // patches. The two must stay a number and its square: grass.sh divides the card index by
    // the side to find the cell, and a mismatch stacks every card into one row.
    // Seven, which is forty-nine cards a square metre.
    //
    // The count and the card's SIZE are one decision, not two. MU's sheet is a 64-pixel tuft of
    // about ten painted blades, so a card 44 cm wide draws each of those blades 4 cm across --
    // fat leaves, not grass. Stretching one painted tuft over a big quad is what makes a card
    // field read as foliage rather than as a lawn, and the answer is more cards, each smaller,
    // so a painted blade lands near the width a real one has.
    // Six, which is thirty-six cards a square metre.
    //
    // The count and what is PAINTED on a card are one decision. MU's tuft is one clump, so a
    // hundred of them a square metre is a sward; the blade sheet paints five to nine separate
    // blades a cell, so a hundred of those is five to nine HUNDRED blades a square metre, and
    // at that count they stop being blades and merge into a flat mass -- "plates blended
    // together". Thirty-six cells is about two hundred blades a metre, which is a sward you
    // cannot see the ground through and can still pick a blade out of.
    //
    // That judgement was made against a sheet read three mip levels blurrier than anyone
    // knew (see the note at the foot of gather), where every stroke was a fat plate. Read at
    // the level it was painted for, thirty-six left the turf showing between the roots and
    // the field read as sparse; forty-nine closes it, and each blade still comes apart from
    // its neighbour. Seven, which is forty-nine cards a square metre.
    static constexpr int kStratification = 7;
    static constexpr int kCardsPerPatch = kStratification * kStratification;
    // MU's sheets are four 64-pixel columns of tuft in a 256-wide picture. The shader cuts one
    // column a card; this is how many there are to choose from.
    static constexpr int kSheetColumns = 4;
    // The meadow's sheet is eight cells of 64 by 128 in one 512-wide picture, and only the
    // first few cards of a patch ever become one: a meadow is what stands THROUGH a sward, and
    // a field of flowers is not a field.
    static constexpr int kSwardColumns = 8;
    static constexpr int kMeadowColumns = 8;
    static constexpr int kMeadowStratification = 3;
    static constexpr int kMeadowCards = kMeadowStratification * kMeadowStratification;
    // Turf's own threshold, and the one the meadow sheet was painted to: strokes are kept at
    // least a texel and a half wide so the linear filter never thins one away at the cut.
    static constexpr float kCutout = 0.28f;
    // The sheet's width in texels, and how far down its chain a card may read. MU's four
    // tufts sit side by side in one 256-wide picture and a card's uv spans one of them; the
    // sampler clamps at the picture's edge and not at a column's, so a level blurred past a
    // few texels averages in the tufts either side. At level 3 the sheet is 32 wide, which is
    // eight texels a column -- still a tuft. Below that it is a smudge, and which smudge it
    // is changes as the camera moves. grass.sh caps it.
    static constexpr int kSheetWidth = 256;
    static constexpr float kDeepestMip = 3.0f;

private:
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;

    // One painted sheet per grass slot the world names, indexed BY that slot. Sparse on
    // purpose: Lorencia's grass is slots 0 and 1, and a world whose grass is slot 5 should
    // index at 5 rather than at whatever place it happened to be loaded in.
    std::vector<bgfx::TextureHandle> sheets_;
    // Each sheet's size in texels, beside it. The shader works a mip level out per
    // axis and MU's sheets are not square.
    std::vector<std::pair<float, float>> sizes_;
    // MU2_BGFX's own blade sheet, pipeline/sward.py: eight cells of separate blades, each its
    // own height, lean, width and green, at a resolution where a blade is six to ten pixels
    // across. It exists because MU's sheet cannot draw one -- see that file's own note, and
    // docs/grass.md for the cutout sweep that proved it. MU's tuft is still loaded and is one
    // knob away (`grass_painted`).
    bgfx::TextureHandle sward_ = BGFX_INVALID_HANDLE;
    std::pair<float, float> swardSize_ = {1024.0f, 256.0f};
    bgfx::TextureHandle meadow_ = BGFX_INVALID_HANDLE;
    std::pair<float, float> meadowSize_ = {512.0f, 128.0f};

    // The patches this frame, packed as the three vec4s vs_grass reads, SORTED by sheet so
    // that each sheet is one contiguous instanced draw. Kept between frames so a gather
    // allocates nothing once the disc has been walked once.
    std::vector<float> packed_[gfx::GrassField::kMaxSheets];
    Counts counts_;
};

}  // namespace mu::game
