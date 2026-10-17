// What a death left, lying on the grass: the models under the labels sprint 7 step 7 drew.
//
// MU2's `Drops.cs`, which is MU's `CreateItemDrop`, `MoveItems`, `RenderZen` and `ItemAngle`.
// The realm decides what lies where and for how long (`sim::Lying`); this is the showing of
// it, and it holds no rule -- an id that appears in the realm's list falls, an id that leaves
// it is gone, and nothing here can pick anything up.
//
// The toss of an item is MU's own (the user, 2026-10-17: "we need also origninal drop
// animation"): CreateItemDrop's 180 units up, thrown upward at 20 a frame against MoveItems'
// 6, turned to its speed on the way (HandleItemFalling's -Gravity * 10 degrees), and put to
// its rest and stopped dead on the grass. A heap of Zen keeps MU2's toss below.
//
// What is kept of MU2's:
//   * a heap's toss: its coins from up to 90 units, each thrown up at a speed of its own
//     against a gravity of 6, and one bounce at a third of the arrival speed;
//   * the rest pose by measurement, not by ItemAngle's table: the longest extent along the
//     ground, the shortest up, so a blade lies on its flat and a haft along its length. The
//     families that sleep are the weapons, shields, armour, trousers and gloves; a helm sits
//     on its brow and a boot on its sole, as MU's own range leaves them;
//   * a yaw of the drop's own, hashed from its id, where MU gives everything -45 degrees and
//     lays a whole field out in parallel;
//   * Zen as a heap of Gold01, coin by coin, spread by how much it is worth.
//
// And MU's sparkle, CreateShiny (ZzzObject.cpp:6271-6286, from MoveItems): every 48th frame a
// thing lies there, two BITMAP_SHINY stars at one spot 16-47 units off it along its turn and
// 16-47 up, white, one still and one at three quarters turning 12 degrees a frame, each
// 18 frames at sin(LifeTime * 10 degrees) of the sheet's 16 pixels (ZzzEffectParticle.cpp,
// BITMAP_SHINY sub-types 0 and 1).
#pragma once

#include <cstdint>
#include <vector>

#include "content/ground.h"
#include "game/item_models.h"
#include "game/shine.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"
#include "sim/items.h"
#include "sim/realm.h"

namespace mu::game {

class Litter {
public:
    void open(ItemModels* models, const content::Ground* ground,
              bgfx::TextureHandle beam = BGFX_INVALID_HANDLE,
              bgfx::TextureHandle shiny = BGFX_INVALID_HANDLE) {
        models_ = models;
        ground_ = ground;
        beam_ = beam;
        shiny_ = shiny;
        if (!bgfx::isValid(glint_)) glint_ = makeGlint();
    }
    // The stars CreateShiny throws off everything lying down. MU's timing and place, and a
    // glint of ours in place of Shiny01, whose 16 pixels read blocky at the star's size (the
    // user, 2026-10-17: "that sparkle effect on droped item is littl ebit cheap").
    void gatherSparkle(gfx::Effects& effects) const;
    // The drops a full bag's hop put back on the grass in the last update: they make their
    // landing noise again (Play::hopLanded). MU's hop is silent.
    const std::vector<uint32_t>& hopsLanded() const { return hopsLanded_; }
    // **A quest item lying down is seen from across the screen** (the user, 2026-10-04: 'quest
    // items needs to be well vissible'). Ours: MU drops a quest item as any other. Sevina's
    // treasures (sim::classTreasure) stand under a violet column of light -- MU's chasellight, the
    // lobby's streak, three body heights tall and turned to face the camera -- breathing, with a
    // brighter core, and light the ground round them violet, ahead of any jewel's glow.
    // And a legendary drop the same, in legendary green, a legendary rune in orange (the user,
    // 2026-10-04: 'we need also that light for legendary drops'); Loch's Feather in the quest
    // violet; an excellent drop in MU's excellent green ('excelnt drops needs also light').
    void gatherBeams(gfx::Effects& effects, const float eye[3]) const;
    // **A jewel lying down glows** (the user, 2026-10-02: "actualy we need minimal light emiters
    // for all jewel drops", and "some nice effect for drop jewel of creations"). Ours: MU throws
    // nothing round a lying jewel. Every jewel (the table's jewel flag, pets aside) and every
    // Rune of Creation that has touched the ground lights a little of it in its own colour: the
    // nearest to `near` take what is left of the renderer's moving lights, after the skills.
    // A rune glows as a jewel does, in its rarity's colour (Rare blue, Epic purple, Legendary
    // green since 2026-10-04); a flare and two stars over it were taken off the same day ('overkill').
    uint32_t lights(gfx::PointLight* out, uint32_t max, const float near[3]) const;

    // Follows the realm's list and moves what is still in the air. Called once a frame, with
    // the realm already stepped.
    // `held` are drops the realm has and the drawing is not showing yet: see Play::heldDrops.
    // `tossed` hop where they lie, a pickup refused for a full bag: see Play::tossedDrops.
    void update(const sim::Realm& realm, double seconds, const std::vector<uint32_t>& held,
                const std::vector<uint32_t>& tossed);
    // Adds every piece lying or falling, and the same to the sun's list: a dropped axe casts
    // a shadow, which is most of what says it is on the ground rather than over it.
    void gather(std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters) const;
    // The drops that are down -- every piece has touched the grass once -- as of the last update.
    // What gets a label: MU2's name goes up over a thing lying on the grass, not over one
    // still in the air.
    const std::vector<uint32_t>& settled() const { return settled_; }
    // The one drop `id` names, for the hover ring: the same pieces `gather` would have added
    // for it, and nothing else. A no-op where `id` is not lying (it fell, or was picked up
    // between the pointer's pick and this call).
    void gatherOne(uint32_t id, std::vector<gfx::Drawable>& out) const;

    size_t count() const { return drops_.size(); }
    size_t pieces() const;

private:
    // One model in the air or at rest: a whole item, or one coin of a heap.
    struct Piece {
        const content::Mesh* mesh = nullptr;
        float rest[16];        // model -> world at rest, its own yaw and lie already in it
        float above = 0.0f;    // metres above that rest, while it is still falling
        float speed = 0.0f;    // metres a second, upward
        int bounced = 0;
        // A whole item's rest in two halves, its lie about its middle and then its yaw and
        // place, so a hop can tumble it between them. A coin has none (`flips` false).
        float lie[16];
        float turn[16];
        bool flips = false;
        bool roll = false;     // a shield turns about its length, as a coin; the rest end over end
        bool tumbling = false;  // falling as MU's items fall, turned to its speed, until it lands
        bool hopped = false;    // and that fall is a full bag's hop, which rings on landing
    };
    // A piece's model -> world this frame: its rest, tumbled while it hops, lifted by its fall.
    static void placed(const Piece& piece, float* out);
    struct Drop {
        uint32_t id = 0;
        bool present = false;
        ShineLook shine;  // how the item's plus shows; a heap of Zen has none
        int glow = 0;     // 0 none, 1 a jewel, 2 a Rune of Creation, 3 the column (quest, legendary)
        float glowColour[3] = {1.0f, 1.0f, 1.0f};
        // The column's added light times this: the streak's sheet is pale blue, so an orange's
        // red came out of it at about half and the legendary column read as nothing.
        float beamLift = 1.0f;
        float at[3] = {};          // on the ground under it, world metres
        float yaw = 0.0f;          // its turn, which the sparkle's offset follows
        float sparkleIn = 0.0f;    // reference frames to its next sparkle: the first at once
        bool down = false;         // touched the grass once: labelled through a hop after it
        float length = 0.0f;       // metres from its middle to its ends, along its turn
        float tall = 0.0f;         // metres from the grass to its top, lying
        std::vector<Piece> pieces;
    };
    struct Twinkle {
        float at[3] = {};
        float left = 0.0f;   // reference frames
        float spin = 0.0f;   // radians, the turning star's
        float turn = 0.0f;   // radians, the still star's own lean
        float colour[3] = {1.0f, 1.0f, 1.0f};
    };
    static bgfx::TextureHandle makeGlint();

    void build(const sim::Lying& one, Drop& drop);
    void buildItem(const sim::Lying& one, Drop& drop);
    void buildHeap(const sim::Lying& one, Drop& drop);

    bgfx::TextureHandle beam_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glint_ = BGFX_INVALID_HANDLE;
    std::vector<uint32_t> hopsLanded_;
    std::vector<Twinkle> twinkles_;
    uint32_t dice_ = 0x9e3779b9u;
    std::vector<uint32_t> settled_;
    float clock_ = 0.0f;  // seconds, wrapped: the glow's breath
    ItemModels* models_ = nullptr;
    const content::Ground* ground_ = nullptr;
    std::vector<Drop> drops_;
};

}  // namespace mu::game
