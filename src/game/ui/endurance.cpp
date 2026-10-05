#include "game/ui/endurance.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/ui/bag.h"
#include "sim/items.h"

namespace mu::game {

namespace {

using gfx::Box;

// The cells are the HUD's (Hud::wornCell): a buff cell's size, over the belt.
constexpr float kIconShare = 0.74f;  // of the tile's width, the rest is frame and air

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
        // A soft shade under the tile, and a broken piece's glow outside it: three rings
        // stepping out and fading, which is as near a blur as the canvas has.
        canvas_.rect(box.grown(line), gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f));
        if (one.band == sim::Worn::Broken) {
            for (int ring = 1; ring <= 3; ++ring) {
                canvas_.outline(box.grown(line * float(ring + 1)), line,
                                colourOf(one.band, 0.42f / float(ring)));
            }
        }
        canvas_.rect(box, gfx::rgba(0.043f, 0.035f, 0.031f, 0.96f));
        // Fitted at its own aspect, as the bag fits it: a boot is tall and a helm nearly square.
        const gfx::Art& art = arts_->get(artFor(one.slot));
        if (art.valid() && art.width > 0.0f && art.height > 0.0f) {
            const float room = box.w * kIconShare;
            const float k = std::min(room / art.width, box.h * kIconShare / art.height);
            const float w = art.width * k, h = art.height * k;
            canvas_.image(art, {box.midX() - w * 0.5f, box.midY() - h * 0.5f, w, h}, kShapeInk);
        }
        // The frame in the band's colour over a black hairline inside it, so the colour holds its
        // edge against the grey art as well as against the ground.
        canvas_.outline(box.grown(-line), line, gfx::rgba(0.0f, 0.0f, 0.0f, 1.0f));
        canvas_.outline(box, line, colourOf(one.band));
        // What is left, down the right edge, filled from the foot.
        const float barW = std::max(2.0f, std::round(box.w / 14.0f));
        const Box track{box.right() - line * 2.0f - barW, box.y + line * 2.5f, barW,
                        box.h - line * 5.0f};
        canvas_.rect(track, gfx::rgba(1.0f, 1.0f, 1.0f, 0.10f));
        const float left = one.maximum > 0 ? float(one.durability) / float(one.maximum) : 0.0f;
        const float fill = std::max(track.h * 0.04f, track.h * std::clamp(left, 0.0f, 1.0f));
        canvas_.rect({track.x, track.bottom() - fill, track.w, fill}, colourOf(one.band));
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
