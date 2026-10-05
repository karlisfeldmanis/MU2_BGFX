#include "game/ui/endurance.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/ui/bag.h"
#include "sim/items.h"

namespace mu::game {

namespace {

using gfx::Box;

// The cells are the HUD's (Hud::wornCell): round, a little under a buff cell's width, over the
// belt.
constexpr float kIconShare = 0.56f;  // of the disc's width: inside the rim, with air round it
constexpr float kRimShare = 0.16f;   // of the radius, the durability ring
constexpr int kRound = 40;           // segments a full circle

// A filled disc, as a fan: the canvas has no circle of its own.
void disc(gfx::Canvas& canvas, float cx, float cy, float r, uint32_t abgr) {
    float xy[kRound * 2];
    uint32_t colours[kRound];
    for (int k = 0; k < kRound; ++k) {
        const float a = 6.28318531f * float(k) / float(kRound);
        xy[k * 2] = cx + std::sin(a) * r;
        xy[k * 2 + 1] = cy - std::cos(a) * r;
        colours[k] = abgr;
    }
    canvas.polygon(xy, colours, kRound);
}

// A pie slice from the top, clockwise, between shares `from` and `to` of the turn: cut in
// quarter turns at most, so every piece is convex for the canvas's fan.
void wedge(gfx::Canvas& canvas, float cx, float cy, float r, float from, float to,
           uint32_t abgr) {
    constexpr int kPerQuarter = kRound / 4;
    while (to - from > 1e-4f) {
        const float end = std::min(to, from + 0.25f);
        float xy[(kPerQuarter + 2) * 2];
        uint32_t colours[kPerQuarter + 2];
        int n = 0;
        xy[0] = cx;
        xy[1] = cy;
        colours[n++] = abgr;
        const int steps = std::max(1, int(std::ceil((end - from) * float(kRound))));
        for (int k = 0; k <= steps; ++k) {
            const float a = 6.28318531f * (from + (end - from) * float(k) / float(steps));
            xy[n * 2] = cx + std::sin(a) * r;
            xy[n * 2 + 1] = cy - std::cos(a) * r;
            colours[n++] = abgr;
        }
        canvas.polygon(xy, colours, n);
        from = end;
    }
}

// The slot's own silhouette, the one the bag draws in it (Bag.GhostFor), so the warning and the
// equipment window name a piece with the same shape -- the user, 2026-09-24: MuMain's separate
// newui_durable_* pictures were a second set of icons to learn. In the bag's warm ink, but bright:
// there it is a hint behind a piece, here it is the mark. The pet (8) and the mount are not
// warned about.
constexpr uint32_t kShapeInk = gfx::rgba(0.90f, 0.86f, 0.76f, 0.88f);

// Every worn slot, the pet's and the mount's too: they wear (Realm::wearOnTaken), and a
// Dinorant ridden down to a quarter showed nothing here (the user, 2026-10-05: "some time we made
// UI for broken items, UI is missing").
const char* artFor(int slot) { return ghostArt(slot); }

// The four bands, as the canvas drew them: MU's yellow, orange, red-orange and red, calmed.
uint32_t colourOf(sim::Worn band, float alpha = 1.0f) {
    switch (band) {
        case sim::Worn::Broken: return gfx::rgba(0.878f, 0.161f, 0.118f, alpha);
        case sim::Worn::Fifth: return gfx::rgba(0.933f, 0.353f, 0.200f, alpha);
        case sim::Worn::Third: return gfx::rgba(0.949f, 0.604f, 0.227f, alpha);
        default: return gfx::rgba(0.949f, 0.820f, 0.294f, alpha);
    }
}

tip::Tone toneOf(sim::Worn band) {
    switch (band) {
        case sim::Worn::Broken:
        case sim::Worn::Fifth: return tip::Tone::Red;
        case sim::Worn::Third: return tip::Tone::Orange;
        default: return tip::Tone::Yellow;
    }
}

// Worst first: the band, then the share left.
int severity(sim::Worn band) {
    switch (band) {
        case sim::Worn::Broken: return 4;
        case sim::Worn::Fifth: return 3;
        case sim::Worn::Third: return 2;
        case sim::Worn::Half: return 1;
        default: return 0;
    }
}

// What the slot is called on the card's second line.
const char* slotName(int slot) {
    switch (slot) {
        case sim::kWeaponRight: return "WEAPON";
        case sim::kWeaponLeft: return "LEFT HAND";
        case sim::kHelm: return "HELM";
        case sim::kArmour: return "ARMOR";
        case sim::kPants: return "PANTS";
        case sim::kGloves: return "GLOVES";
        case sim::kBoots: return "BOOTS";
        case sim::kPet: return "PET";
        case sim::kMount: return "MOUNT";
        default: return "WORN";
    }
}

const char* stateName(sim::Worn band) {
    switch (band) {
        case sim::Worn::Broken: return "BROKEN";
        case sim::Worn::Fifth: return "BADLY WORN";
        default: return "WORN";
    }
}

}  // namespace

bool Endurance::Drawn::operator==(const Drawn& o) const {
    if (count != o.count || hovered != o.hovered || width != o.width || height != o.height ||
        self != o.self) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (!(icons[i] == o.icons[i])) return false;
        const Box &a = cells[i], &b = o.cells[i];
        if (a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h) return false;
    }
    return true;
}

void Endurance::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

void Endurance::update(float width, float height, const Hud& hud, const sim::Realm& realm,
                       const Pointer& pointer) {
    now_ = Drawn{};
    now_.width = width;
    now_.height = height;
    now_.self = realm.selfMending();
    if (const content::Tables* tables = realm.tables()) {
        const sim::Satchel& bag = realm.satchel();
        for (int slot = 0; slot < sim::kWorn; ++slot) {
            const sim::Held& held = bag[slot];
            if (held.empty() || !artFor(slot)) continue;
            const content::ItemRow& row = tables->items[size_t(held.item)];
            // Ammunition's count is its shots and not its wear, and MuMain skips both quivers.
            if (!sim::wears(row)) continue;
            const int maximum = sim::maximumDurability(row, held);
            const sim::Worn band = sim::wornBand(held.durability, maximum);
            if (band == sim::Worn::Fine) continue;
            now_.icons[now_.count++] = Icon{slot, band, held.durability, maximum};
        }
        std::stable_sort(now_.icons, now_.icons + now_.count, [](const Icon& a, const Icon& b) {
            if (severity(a.band) != severity(b.band)) return severity(a.band) > severity(b.band);
            return a.durability * b.maximum < b.durability * a.maximum;
        });
    }
    for (int i = 0; i < now_.count; ++i) {
        now_.cells[i] = hud.wornCell(i);
        if (now_.cells[i].has(pointer.x, pointer.y)) now_.hovered = i;
    }
    if (now_ == drawn_ && rebuilds_ > 0) return;
    drawn_ = now_;
    rebuild(realm);
}

void Endurance::rebuild(const sim::Realm& realm) {
    ++rebuilds_;
    canvas_.clear();
    tip_.clear();
    if (!arts_ || !realm.tables() || now_.count == 0) return;
    const content::Tables& tables = *realm.tables();
    const float s = now_.height / panel::kReferenceHeight;
    const float line = std::max(1.0f, std::round(0.6f * s));

    for (int i = 0; i < now_.count; ++i) {
        const Icon& one = now_.icons[i];
        const Box box = now_.cells[i];
        // A round container (the user, 2026-10-05: "make them little bit smaller and in circle
        // containers"): a soft shade under it, and a broken piece's glow outside it, three
        // discs stepping out and fading, which is as near a blur as the canvas has.
        const float cx = box.midX(), cy = box.midY(), r = box.w * 0.5f;
        if (one.band == sim::Worn::Broken) {
            for (int ring = 3; ring >= 1; --ring) {
                disc(canvas_, cx, cy, r + line * float(ring + 1),
                     colourOf(one.band, 0.30f / float(ring)));
            }
        }
        disc(canvas_, cx, cy, r + line, gfx::rgba(0.0f, 0.0f, 0.0f, 0.6f));
        // What is left as the rim: the band's colour clockwise from the top for the share left,
        // the rest of the rim the same colour dim -- so even a broken piece reads in its red.
        const float left = one.maximum > 0
                               ? std::clamp(float(one.durability) / float(one.maximum), 0.0f, 1.0f)
                               : 0.0f;
        disc(canvas_, cx, cy, r, colourOf(one.band, 0.28f));
        wedge(canvas_, cx, cy, r, 0.0f, left, colourOf(one.band));
        disc(canvas_, cx, cy, r - kRimShare * r, gfx::rgba(0.0f, 0.0f, 0.0f, 1.0f));
        disc(canvas_, cx, cy, r - kRimShare * r - line, gfx::rgba(0.043f, 0.035f, 0.031f, 0.97f));
        // Fitted at its own aspect inside the disc, as the bag fits it: a boot is tall and a helm
        // nearly square.
        const gfx::Art& art = arts_->get(artFor(one.slot));
        if (art.valid() && art.width > 0.0f && art.height > 0.0f) {
            const float room = box.w * kIconShare;
            const float k = std::min(room / art.width, room / art.height);
            const float w = art.width * k, h = art.height * k;
            canvas_.image(art, {cx - w * 0.5f, cy - h * 0.5f, w, h}, kShapeInk);
        }
    }

    if (now_.hovered >= 0) {
        // The card, in the item card's own glass: the name in its band, the slot and its state,
        // the count, what the wear costs it right now, and where it is mended -- with the
        // durability bar in its foot, as every worn item's card already carries.
        const Icon& one = now_.icons[now_.hovered];
        const sim::Held& held = realm.satchel()[one.slot];
        const content::ItemRow& row = tables.items[size_t(held.item)];
        tip::Sheet sheet;
        sheet.name = row.label + (held.refinement > 0 ? " +" + std::to_string(held.refinement)
                                                      : std::string());
        sheet.nameTone = toneOf(one.band);
        sheet.base = std::string(slotName(one.slot)) + " \xC2\xB7 " + stateName(one.band);
        sheet.wide = 250.0f;
        tip::Section what;
        const float cut = sim::wearCut(one.durability, one.maximum);
        const bool swung = row.weapon() && !row.shield();
        std::string cost;
        if (cut >= 1.0f) {
            cost = "Gives nothing until mended";
        } else if (cut > 0.0f) {
            // A hyphen for the minus: the face has no U+2212 and drops it silently.
            cost = "-" + std::to_string(int(std::lround(cut * 100.0f))) + "% " +
                   (swung ? "damage" : "defense");
        } else {
            cost = "No loss yet";
        }
        what.rows.push_back(tip::Row{"Wear", {tip::Value{cost, cut > 0.0f ? tip::Tone::Red
                                                                           : tip::Tone::Gray}},
                                     "", tip::Tone::White});
        // Where it is mended, which depends on who is reading: from kSelfRepairLevel he can do it
        // himself from the bag at two and a half times, below it only the blacksmith can.
        const bool self = now_.self;
        what.rows.push_back(tip::Row{"", {},
                                     self ? "Repair at Hanzo the Blacksmith, or yourself from "
                                            "the inventory (level 50+) at 2.5x the price"
                                          : "Repair at Hanzo the Blacksmith, Lorencia. From "
                                            "level 50 you can repair it yourself",
                                     tip::Tone::Gray});
        sheet.sections.push_back(what);
        sheet.wear = "Durability " + std::to_string(one.durability) + " / " +
                     std::to_string(one.maximum);
        sheet.worn = one.maximum > 0 ? float(one.durability) / float(one.maximum) : 0.0f;
        sheet.wearTone = toneOf(one.band);
        // Standing on the cell, as a buff's card stands on its cell (Hud's boon card).
        tip::draw(tip_, sheet, now_.cells[now_.hovered], now_.width, now_.height);
    }
}

}  // namespace mu::game
