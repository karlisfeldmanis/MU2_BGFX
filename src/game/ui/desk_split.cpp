// The desk's part of a stack (the user, 2026-10-20: "we need stacking jewels", then a tooltip
// for the Shift-click): Shift and a press on a stack opens the number box, its OK puts that many
// on the pointer, and wherever they are let go the realm is asked for that many
// (sim/realm_commands.cpp splits them off). And how many of a stack a drop into the Chaos
// Machine's box gives: what the open service wants.
#include "game/ui/desk.h"

#include "sim/machine.h"

namespace mu::game {

namespace {

// Whether a thing is a stack of more than one that may be split: a quiver's shots are its wear.
bool splittable(const content::Tables& tables, const sim::Held& what) {
    if (what.empty() || what.durability <= 1 || size_t(what.item) >= tables.items.size()) return false;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    return sim::stacks(row) && !sim::ammunition(row);
}

}  // namespace

bool Desk::splitAsked(const Play& play) {
    if (!play.isOpen() || amount_.up() || !play.realm().tables()) return false;
    if (bag_.dragging() || chest_.dragging() || mixer_.dragging()) return false;
    const content::Tables& tables = *play.realm().tables();
    const sim::Realm& realm = play.realm();
    Splitting at;
    if (inventoryOpen_ && bag_.hovered() >= 0) {
        at = {0, bag_.hovered(), realm.satchel()[bag_.hovered()].item};
    } else if (banking_ && chest_.hovered() >= 0) {
        at = {1, chest_.hovered(), realm.vault()[chest_.hovered()].item};
    } else if (mixing_ && !realm.mixed() && mixer_.hovered() >= 0) {
        at = {2, mixer_.hovered(), realm.machine()[mixer_.hovered()].item};
    } else {
        return false;
    }
    const sim::Held& what = at.from == 0 ? realm.satchel()[at.slot]
                            : at.from == 1 ? realm.vault()[at.slot]
                                           : realm.machine()[at.slot];
    if (!splittable(tables, what)) return false;
    splitting_ = at;
    amount_.showSplit(what.durability);
    return true;
}

void Desk::takePart(const Play& play, int pieces) {
    const sim::Realm& realm = play.realm();
    const Splitting at = splitting_;
    splitting_ = Splitting{};
    if (at.from < 0 || pieces <= 0) return;
    const sim::Held& what = at.from == 0 ? realm.satchel()[at.slot]
                            : at.from == 1 ? realm.vault()[at.slot]
                                           : realm.machine()[at.slot];
    // The stack changed under the box -- drunk, sold, moved: nothing is picked.
    if (what.empty() || what.item != at.item) return;
    const int part = pieces >= what.durability ? 0 : pieces;
    if (at.from == 0) bag_.split(at.slot, part);
    else if (at.from == 1) chest_.split(at.slot, part);
    else mixer_.split(at.slot, part);
}

int Desk::piecesForBox(const Play& play, int slot, int part) const {
    if (part > 0) return part;
    const sim::Realm& realm = play.realm();
    if (!realm.tables() || slot < 0 || slot >= sim::kSlots) return 0;
    const sim::Held& what = realm.satchel()[slot];
    if (!splittable(*realm.tables(), what)) return 0;
    const int wanted = sim::piecesWanted(*realm.tables(), realm.machine(), what, mixer_.service(),
                                         mixer_.socket(), realm.wearer().kin);
    return wanted >= what.durability ? 0 : wanted;
}

int Desk::piecesForSale(const Play& play, int slot, int part) const {
    if (part > 0) return part;
    const sim::Realm& realm = play.realm();
    if (!realm.tables() || slot < 0 || slot >= sim::kSlots) return 0;
    const sim::Held& what = realm.satchel()[slot];
    if (what.empty() || size_t(what.item) >= realm.tables()->items.size()) return 0;
    const content::ItemRow& row = realm.tables()->items[size_t(what.item)];
    // The pets ride in the jewel drop group and are not jewels (describe's kindOf).
    const bool jewel = row.jewel() && row.group != sim::kGroupPets;
    return jewel && what.durability > 1 ? 1 : 0;
}

}  // namespace mu::game
