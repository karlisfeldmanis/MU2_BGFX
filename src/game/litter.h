// What a death left, lying on the grass: the models under the labels sprint 7 step 7 drew.
//
// MU2's `Drops.cs`, which is MU's `CreateItemDrop`, `MoveItems`, `RenderZen` and `ItemAngle`.
// The realm decides what lies where and for how long (`sim::Lying`); this is the showing of
// it, and it holds no rule -- an id that appears in the realm's list falls, an id that leaves
// it is gone, and nothing here can pick anything up.
//
// What is kept of MU2's:
//   * the toss: 90 units up (MU's 180 halved, so a drop stays over the corpse), thrown
//     upward at 8 a reference frame against a gravity of 6, and one bounce at a third of the
//     arrival speed, because MU stops an item dead and that reads as placing rather than
//     dropping;
//   * the rest pose by measurement, not by ItemAngle's table: the longest extent along the
//     ground, the shortest up, so a blade lies on its flat and a haft along its length. The
//     families that sleep are the weapons, shields, armour, trousers and gloves; a helm sits
//     on its brow and a boot on its sole, as MU's own range leaves them;
//   * a yaw of the drop's own, hashed from its id, where MU gives everything -45 degrees and
//     lays a whole field out in parallel;
//   * Zen as a heap of Gold01, coin by coin, spread by how much it is worth.
// Not kept yet: the sparkle (`CreateShiny`), which is owed with the effects it would use.
#pragma once

#include <cstdint>
#include <vector>

#include "content/ground.h"
#include "game/item_models.h"
#include "gfx/renderer.h"
#include "sim/items.h"
#include "sim/realm.h"

namespace mu::game {

class Litter {
public:
    void open(ItemModels* models, const content::Ground* ground) {
        models_ = models;
        ground_ = ground;
    }

    // Follows the realm's list and moves what is still in the air. Called once a frame, with
    // the realm already stepped.
    void update(const sim::Realm& realm, double seconds);
    // Adds every piece lying or falling, and the same to the sun's list: a dropped axe casts
    // a shadow, which is most of what says it is on the ground rather than over it.
    void gather(std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters) const;

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
    };
    struct Drop {
        uint32_t id = 0;
        bool present = false;
        std::vector<Piece> pieces;
    };

    void build(const sim::Lying& one, Drop& drop);
    void buildItem(const sim::Lying& one, Drop& drop);
    void buildHeap(const sim::Lying& one, Drop& drop);

    ItemModels* models_ = nullptr;
    const content::Ground* ground_ = nullptr;
    std::vector<Drop> drops_;
};

}  // namespace mu::game
