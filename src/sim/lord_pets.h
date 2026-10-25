#pragma once

// **The Dark Lord's pets by their level**: the Dark Horse (13/4) and the Dark Raven (13/5), each 1 to
// 50 (docs/plans/dark-lord-pets/README.md, server.md section 2). WebZen keeps a pet's level and exp
// per serial on its DataServer; ours keeps the exp in the four bytes a pet leaves nought -- the three
// socket powers and the wing byte, low byte first, as the timed pets keep their second
// (sim::petUntil) -- and reads the level off MU's table. So the wire, the save, the vault and the
// Broker carry it, and a newborn, nought, is level 1.

#include <algorithm>
#include <cstdint>

#include "sim/items.h"
#include "sim/rules.h"

namespace mu::sim {

constexpr int kPetTopLevel = 50;  // MAX_DARK_HORSE_LEVEL, MAX_DARK_SPIRIT_LEVEL (zzzitem.h:348-349)

// A pet's exp at the start of level `level`: (10 + L) x L^3 x 100, nought at 1 (zzzitem.h:354-368).
uint32_t petExpAt(int level);
uint32_t petExp(const Held& pet);
void setPetExp(Held& pet, uint32_t exp);
// The level its exp has reached, 1 to kPetTopLevel.
int petLevel(const Held& pet);
// Whether it is one of his two, which level.
bool levelledPet(const content::ItemRow& row);

// What it asks with its level in: the Dark Horse 218 + 2 a level (WebZen zzzitem.cpp:580-583).
Needs asks(const content::ItemRow& row, const Held& what);
// Its powers at its level: the Dark Horse's cut, `100 - (30 + L)/2` in a hundred (ObjAttack.cpp:
// 1345-1361).
PetPower petPower(const content::ItemRow& row, const Held& what);
// The Dark Horse's defence, 5 + Agility/20 + 2 a level (ObjCalCharacter.cpp:1082-1090).
int horseDefense(int agility, int level);

// **Its share of what he gains** (gObjSetExpPetItem, user.cpp:32505-32648): a fifth to the one
// worn, a tenth each to both, on top of his own. Nothing to one at nought life or at the top.
// What it would reach is refused whole when he could no longer wear it there (PetItemLevelDown):
// `fitsAt` says whether a level is wearable. Returns the level it went up to, or 0 for none.
constexpr int kPetSharePercent = 20, kPetsSharePercent = 10;
template <typename FitsAt>
int feedPet(Held& pet, uint64_t share, FitsAt fitsAt) {
    if (pet.empty() || pet.durability <= 0 || share == 0) return 0;
    const int was = petLevel(pet);
    if (was >= kPetTopLevel) return 0;
    const uint64_t top = petExpAt(kPetTopLevel);
    const uint32_t exp = uint32_t(std::min<uint64_t>(top, uint64_t(petExp(pet)) + share));
    Held next = pet;
    setPetExp(next, exp);
    const int now = petLevel(next);
    if (now > was && !fitsAt(now)) return 0;
    pet = next;
    return now > was ? now : 0;
}

// **Dead** (life nought): it loses a tenth of its level's span, which may take it a level down
// (WebZen's DecPetItemExp; OpenMU never drops a level). Returns the level it fell to, or 0.
int bleedPet(Held& pet);

// The Dark Raven, 13/5 (OpenMU VersionSeasonSix Items/Pets.cs), worn in the left hand.
constexpr int kDarkRavenNumber = 5;
bool darkRaven(const content::ItemRow& row);
// The Command it asks to be worn and to act: 185 + 15 a level (zzzitem.cpp:506-524).
inline int ravenCommand(int level) { return 185 + 15 * level; }

// Its whole value, by its level (CItem::PetValue, zzzitem.cpp:3452-3479): the horse 2,000,000 a
// level, the raven 1,000,000. A merchant pays a third.
int64_t petValue(const content::ItemRow& row, const Held& what);

// What MU asks to mend it at the Trainer (1.03.35's ConvertRepairGold over the pet's whole value):
// 3 x sqrt(V) x V^(1/4) x (1 - life/255) + 1, twice that at nought, nought when whole.
int64_t mendPrice(int64_t value, int life);

}  // namespace mu::sim
