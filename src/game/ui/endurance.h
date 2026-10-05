// The worn-gear warning: one round container for each worn piece at half its durability or less,
// in a row over the potion belt, worst piece on the right, its rim a ring of what is left, and a
// small card for the one under the pointer.
//
// MuMain's `CNewUIItemEnduranceInfo` in the shape the user chose on the design canvas on
// 2026-09-24 ("A3 · MU's column, polished", tint "Frame only"). Kept of MuMain: the four bands and
// where they begin (sim::wornBand, as the bag's wash and the tooltip's bar), the 23-unit tile, and
// a column that hangs left of whatever windows are open (`SetPos(iScreenWidth)`). Changed, and the
// user's:
//   * the picture is the slot's own silhouette from the equipment window (bag.h ghostArt), not
//     MuMain's separate newui_durable_* set: one shape per slot everywhere in the game;
//   * one row over the plate's right half, the buff strip's mirror and a buff cell's size, and
//     not a column down the screen's edge (the user, 2026-10-05: "we need to find betetr place
//     and choose betetr size for broken item UI"; at the edge under the minimap it was far from
//     where a fight is watched); worst piece first rather than slot order;
//   * round, a little smaller than a buff cell (the user, 2026-10-05: "make them little bit
//     smaller and in circle containers");
//   * the shape stays in the window's warm ink and only the rim takes the band's colour --
//     MuMain's half-strength wash muddied the art -- with a red glow on a broken piece;
//   * the rim is the gauge: the band's colour clockwise from the top for what is left, the rest
//     of it the same colour dim;
//   * the hover is the item card's own glass (tip.h) rather than MU's one line: the name in its
//     band, what it has left, what the wear costs it now, and where it is mended.
//
// It draws and takes nothing: a click on a tile goes to the ground as it does in MU.
#pragma once

#include <cstdint>

#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "game/ui/tip.h"
#include "gfx/interface.h"
#include "sim/realm.h"
#include "sim/wear.h"

namespace mu::game {

class Endurance {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // A frame. The cells are the HUD's (Hud::wornCell), so the row moves with the plate.
    void update(float width, float height, const Hud& hud, const sim::Realm& realm,
                const Pointer& pointer);

    bool showing() const { return !canvas_.empty(); }
    const gfx::Canvas& canvas() const { return canvas_; }
    // The card, on its own canvas so the desk lays it over every window, as the bag's is.
    const gfx::Canvas& tipCanvas() const { return tip_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    struct Icon {
        int slot = -1;
        sim::Worn band = sim::Worn::Fine;
        int durability = 0, maximum = 0;
        bool operator==(const Icon& o) const {
            return slot == o.slot && band == o.band && durability == o.durability &&
                   maximum == o.maximum;
        }
    };
    struct Drawn {
        Icon icons[sim::kWorn];
        int count = 0;
        gfx::Box cells[sim::kWorn] = {};
        int hovered = -1;
        float width = 0.0f, height = 0.0f;
        bool self = false;  // he may mend it himself, which the card says
        bool operator==(const Drawn& o) const;
    };
    void rebuild(const sim::Realm& realm);

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    panel::Arts* arts_ = nullptr;
    Drawn drawn_, now_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
