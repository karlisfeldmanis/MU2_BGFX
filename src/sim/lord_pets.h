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
// **Its four modes** (ePetItem_Mode, DarkSpirit.h:20-26; docs/plans/dark-lord-pets/README.md 6).
enum class RavenMode : uint8_t { Normal = 0, Random = 1, WithOwner = 2, Target = 3 };
constexpr int kRavenModes = 4;
// Its numbers (CDarkSpirit::Set, DarkSpirit.cpp:640-689): the band, its speed, its attack rate.
inline int ravenLeast(int level, int command) { return 180 + 15 * level + command / 8; }
inline int ravenMost(int level, int command) { return 200 + 15 * level + command / 4; }
inline int ravenSpeed(int level, int command) { return 20 + level * 4 / 5 + command / 50; }
inline int ravenAttackRate(int level) { return 1000 + level + level / 15; }
// Its strike every 1500 - 10 x speed ms, on WebZen's 500 ms timer (MonsterAndMsgProc), landing
// 600 ms after the swoop (gObjAddAttackProcMsgSendDelay); in ticks of 50 ms.
constexpr int kRavenTimerTicks = 10, kRavenLandTicks = 12;
// Random's reach from him, With owner's and Target's (MAX_DARKSPIRIT_ATTACK_RANGE 5, and 7), the
// ball's from its target, its four more at most, and the one in ten that is Random's dash.
constexpr int kRavenRandomReach = 5, kRavenReach = 7, kRavenSplashReach = 4, kRavenSplashMost = 4;
constexpr int kRavenDashIn10 = 3;
// Its life: its own landed hits of 50 or more, damage x 2/100 into a tally of 800 a point.
constexpr int kRavenWearFrom = 50, kRavenWearTally = 800;

// Its whole value, by its level (CItem::PetValue, zzzitem.cpp:3452-3479): the horse 2,000,000 a
// level, the raven 1,000,000. A merchant pays a third.
int64_t petValue(const content::ItemRow& row, const Held& what);

// **The Pet Trainer**, MU's NPC 226 (MONSTER_PET_TRAINER), Lorencia 122,110: his window is the Chaos
// Machine's with his own row (sim::kTrainerServices) -- Restore Life, and the two mixes MU calls
// Resurrection (mix.bmd's TRAINER recipes 13 and 14, WebZen MixSystem.cpp:3051-3245): a Spirit,
// Bless, Soul, a Chaos and the Jewel of Life where MU asks Creation (docs/plans/dark-lord.md
// decision 3), 60%, everything lost on a failure, a level-1 pet out.
constexpr int kPetTrainer = 226;
constexpr int kSpiritNumber = 31;
// A Spirit, 13/31: +0 the Dark Horse's, +1 the Dark Raven's.
bool spirit(const content::ItemRow& row);
constexpr int kBreedRate = 60;
constexpr int kHorseBless = 5, kHorseSoul = 5, kRavenBless = 2, kRavenSoul = 2;
constexpr int64_t kHorseBreedZen = 5000000, kRavenBreedZen = 1000000;
// **The Spirits' drops** (OpenMU Pets.cs:206-220; WebZen's C++ drops none): one roll a kill of its own,
// one in kSpiritDropOdds, the Horse's from monster level 102, the Raven's from 96 -- both open past 102,
// one drawn.
constexpr int kSpiritDropOdds = 1000;
constexpr int kHorseSpiritFrom = 102, kRavenSpiritFrom = 96;

// What MU asks to mend it at the Trainer (1.03.35's ConvertRepairGold over the pet's whole value):
// 3 x sqrt(V) x V^(1/4) x (1 - life/255) + 1, twice that at nought, nought when whole.
int64_t mendPrice(int64_t value, int life);

}  // namespace mu::sim
