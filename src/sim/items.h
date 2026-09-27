// What a character carries, and the rules for putting a thing anywhere.
//
// Sprint 7's rules half, written fresh from MU2's shared/Satchel.cs, Rows.cs (Needs, Refine,
// Prize) and Beast.cs (Fits, Movable, Handful), each of which carries its OpenMU and MuMain
// lines. The shape is MU's own and is not negotiable, because every window and every packet
// MU ever had is indexed by it: twelve worn slots in MU's `EQUIPMENT_*` order, then an eight
// by eight bag, seventy-six slots in all, and an item recorded ONCE at the top-left of the
// rectangle it covers (`InsertItem`, OpenMU's `Storage`).
//
// Smaller than MU2's, on purpose:
//   * No luck and no options roll on anything yet: a piece is its row, its plus, and for a
//     stack its count. The fields are here so the save and the tooltip do not change shape.
//   * No refining, no chaos machine, no trade. The vault is its own grid: sim/vault.h.
// Zen is not a slot. MU carries it as item 14/15 with the amount in the level field, which
// makes every reader of a slot learn that one code is not an item; it is a number on the
// character instead (MU2's Held remark).
#pragma once

#include <cstdint>
#include <string>

#include "content/tables.h"
#include "sim/rules.h"

namespace mu::sim {

// MU's EQUIPMENT_* (_define.h), in its own order.
enum : int {
    kWeaponRight = 0,
    kWeaponLeft = 1,
    kHelm = 2,
    kArmour = 3,
    kPants = 4,
    kGloves = 5,
    kBoots = 6,
    kWings = 7,
    kPet = 8,
    kAmulet = 9,
    kRingRight = 10,
    kRingLeft = 11,
    kWorn = 12,  // MAX_EQUIPMENT, and the first bag slot
    kBagColumns = 8,
    kBagRows = 8,
    kSlots = kWorn + kBagColumns * kBagRows,  // 76, MAX_MY_INVENTORY_INDEX
};

// MU's item groups past the weapons (ArmorInitializerBase: `armor.Group = slot + 5`).
enum : int32_t {
    kGroupBows = 4,
    kGroupShields = 6,
    kGroupHelms = 7,
    kGroupArmours = 8,
    kGroupPants = 9,
    kGroupGloves = 10,
    kGroupBoots = 11,
    kGroupPotions = 14,
};

inline bool wearable(int slot) { return slot >= 0 && slot < kWorn; }
inline bool baggable(int slot) { return slot >= kWorn && slot < kSlots; }

// One carried thing. `item` indexes Tables::items; -1 is an empty slot.
struct Held {
    int32_t item = -1;
    int16_t refinement = 0;
    // What is left of it: the count, for the rows that stack (a potion bought as a three
    // arrives as one piece of durability three, which is MU's own arrangement and why Realm
    // Consume spends it one at a time), and the shots in a quiver.
    int16_t durability = 0;
    // Whether it carries its row's skill: a shop's `+S` line (MerchantStores), which the price
    // charges for. Nothing reads it for a fight yet -- there are no skills -- and it is carried
    // so a sale is priced as the purchase was. OpenMU's Item.HasSkill.
    bool skill = false;
    // The two options 0.75 rolls on a dropped weapon, piece of armour or shield (OpenMU's
    // `ApplyRandomOptions` over `CreateLuckOptionDefinition` and `CreateOptionDefinition`):
    // luck, and the additional option's level, 0 for none, 1 to 4 for +4 to +16 (+5 to +20 on a
    // shield's defence rate). See `takesOptions`, `optionValue` and docs/refining.md.
    bool luck = false;
    int8_t option = 0;
    // Its excellent options, a bit each, MuMain's `ExcellentFlags & 63`: bit 0 is option 1 of
    // its family (sim::excellentLine), up to bit 5. None on anything a drop did not make
    // excellent. See docs/refining.md, "Excellent".
    uint8_t excellent = 0;
    bool empty() const { return item < 0; }
};

// What an item asks of whoever would wear it, and what he is short of it. Needs.Asking:
// `(multiplier x (dropLevel + 3 x plus) x raw / 100) + 20`, three for everything and four for
// energy, and nothing at all for a raw zero -- OpenMU's ItemExtensions.CalculateRequirement and
// MuMain's CalcRequirements, which agree to the digit. The level is not scaled.
struct Needs {
    int level = 0, strength = 0, agility = 0, energy = 0, vitality = 0;
    bool none() const { return !level && !strength && !agility && !energy && !vitality; }
};
// An excellent thing asks as if it dropped 25 levels deeper (ItemExtensions.CalculateDropLevel).
Needs asks(const content::ItemRow& row, int refinement, bool excellent = false);
Needs shortOf(const Needs& asked, int level, const HeroPoints& points);

// The refinement tables, Version075's own (Weapons.DamageIncreaseByLevel and the two armour
// tables), clamped at their ends rather than run off. Rows.cs Refine.
int damageBonus(int refinement);
int defenseBonus(bool shield, int refinement);

// Which worn slot a thing goes in, or -1 for a thing that is not worn: a bow and ammunition
// by MU's own hand rule (a bow is Weapon[1], a crossbow Weapon[0]; arrows beside the bow and
// bolts beside the crossbow), a shield the left hand, armour its group less five.
int placeOf(const content::ItemRow& row);
// Whether it is ammunition: the bow group's 7 and 15.
bool ammunition(const content::ItemRow& row);
// Whether it is drunk for a pool: the apple and three healing potions, the three mana potions.
bool heals(const content::ItemRow& row);
bool restores(const content::ItemRow& row);

class Satchel {
public:
    const Held& operator[](int slot) const;
    // Counted rather than raised: a window mirrors seventy-six slots and asks whether this
    // moved. Satchel.Version.
    uint32_t version() const { return version_; }

    void put(int slot, const Held& what);
    Held lift(int slot);
    void clear();

    // Which slot's item covers a cell, or -1. An item is recorded at its top-left only, so this
    // walks every carried thing -- MU's own walk, and cheaper than a second grid that can fall
    // out of step with this one.
    int holder(const content::Tables& tables, int cell) const;
    // The bag cells an item of this size covers from a slot, into `out` (up to 8 wide by 8
    // tall), or 0 when it would run off the grid. A worn slot covers itself.
    int covered(int slot, int width, int height, int* out) const;
    // Whether it would sit at a slot, ignoring one item already there (the one being moved).
    bool room(const content::Tables& tables, int slot, int width, int height,
              int ignoring = -1) const;
    // The first bag slot a thing of this size fits in, top-left first, or -1.
    // FindEmptySlotIncludingExtensions, less the extensions.
    int free(const content::Tables& tables, int width, int height) const;

private:
    Held slots_[kSlots];
    uint32_t version_ = 0;
};

// The gates, asked by the realm's refusal AND by a window's drop-target colour, so the two
// cannot drift apart (MU2's interface-is-a-mirror). `kin`, `level` and `points` are the
// character's own.
struct Wearer {
    Kin kin = Kin::DarkKnight;
    int level = 1;
    HeroPoints points;
    // What he has read already, as `Body::learned` keeps it -- a bit per row of the skill
    // table, by INDEX and not by skill number. No gate here asks it: `fits` has nothing to do
    // with it, and an orb of something he knows is refused by `Realm::useItem` and not by
    // `movable`, because it can still be carried and sold. It is here because a card is drawn
    // from this struct and the one thing an orb's card must say is whether he has read it.
    uint32_t learned = 0;
    // The shield in his hand's defence, plus and wear counted, for Defense's card
    // (`guardShare`).
    int shieldDefense = 0;
};
// Whether this character may wear it at all: his class, and every requirement met.
bool fits(const content::Tables& tables, const Wearer& who, const Held& what);
// Whether a thing in one hand and a thing going into the other are one hand too many: a
// two-handed weapon wants the other hand empty, and ammunition does not count. Beast.Handful.
bool handful(const content::Tables& tables, const Satchel& bag, const Held& what, int hand);
// Whether a move from one slot to another would be taken -- into a worn slot only from the
// bag and only where it goes and only if it fits; into the bag if the rectangle is clear of
// everything but the thing itself and at most one other it would swap with. Beast.Movable.
bool movable(const content::Tables& tables, const Wearer& who, const Satchel& bag, int from,
             int to);
// The move itself, after `movable` said yes: what is in the way comes back to where this one
// was. Beast.Move.
bool move(const content::Tables& tables, const Wearer& who, Satchel& bag, int from, int to);

// ---- refining with jewels (docs/refining.md) ----------------------------------------------
//
// The highest plus anything is carried at until the Chaos Machine exists: the Soul stops at +9
// on its own, and a drop must not hand out what nothing can make. MU2's Refine.Cap; eleven,
// Version075's MaximumItemLevel, once the machine's +10 and +11 are built.
constexpr int kRefineCap = 9;

// Which of the two refining jewels a row is: the Bless (14, 13) or the Soul (14, 14). The
// Chaos (12, 15) is a jewel too and goes on nothing -- it is the machine's.
enum class Jewel : uint8_t { None, Bless, Soul };
Jewel jewelOf(const content::ItemRow& row);

// Whether a jewel would go on a thing, asked by the realm's refusal and by the bag's drop
// colour. OpenMU's `CanLevelBeUpgraded` and the two handlers' ranges, which MuMain's
// `CanUpgradeItem` paints by to the level: the weapon and armour groups up to the boots, less
// the arrows and the bolts, a Bless on +0 to +5 and a Soul on +0 to +8.
bool refinable(const content::Tables& tables, const Held& jewel, const Held& target);

// Whether it is too dear to throw on the ground: MuMain's `IsHighValueItem`
// (ZzzInventory.cpp:7407), which both of its drop paths refuse with "You are not allowed to drop
// this expensive item". Of its list the rows this tree has are the three jewels, anything
// below the wings at +7 or more, and anything excellent; the wings, pets and ancient are not.
// A client rule -- OpenMU's DropItemAction takes anything -- kept in the realm so the window
// cannot disagree with it.
bool expensive(const content::Tables& tables, const Held& what);

// ---- luck and the additional option -------------------------------------------------------
//
// Who may carry them: every weapon, armour piece and shield (Weapons.cs:305-313,
// ArmorInitializerBase.cs:185, 401-406), which is the refinable set -- the arrows and bolts
// have neither. A drop draws luck at a quarter, then the option at a quarter and its level
// evenly from 1 to 3 (`MaximumItemOptionLevelDrop = 3`).
constexpr double kLuckChance = 0.25;    // CreateLuckOptionDefinition's AddChance
constexpr double kOptionChance = 0.25;  // CreateOptionDefinition's AddChance
constexpr int kMostOptionDropped = 3;   // GameConfiguration.MaximumItemOptionLevelDrop
constexpr int kMostOption = 4;          // Version075 Constants.MaximumOptionLevel
constexpr double kLuckCritical = 0.05;  // Stats.CriticalDamageChance, a lucky thing worn
bool takesOptions(const content::ItemRow& row);
// What the option adds at its level: four a level on a weapon's damage, a staff's wizardry
// damage and a piece of armour's defence, five a level on a shield's defence rate
// (`CreateOptionDefinition(Stats.DefenseRatePvm, ..., 5)`).
int optionValue(const content::ItemRow& row, int level);

// ---- excellent ----------------------------------------------------------------------------
//
// Not 0.75: OpenMU adds excellent options in 0.95d (GameConfigurationInitializer.cs:33-35),
// and this game takes them on the user's word (2026-09-27). OpenMU's rules, ExcellentOptions.cs
// and DefaultDropGenerator: a group of its own at 0.0001 a drop slot, from a monster 25 levels
// or more above what it drops (ExcellentItemDropLevelDelta), always at +0; one option always
// and a second at 0.001, never the same one twice.
constexpr double kExcellentChance = 0.0001;       // the excellent DropItemGroup's Chance
constexpr int kExcellentLevelDelta = 25;          // GameConfiguration.ExcellentItemDropLevelDelta
constexpr double kSecondExcellentChance = 0.001;  // the option definitions' AddChance
constexpr int kExcellentOptions = 6;
// Which six a row draws from: the defence family for armour and shields, the attack family
// for weapons -- a staff's reading wizardry for damage -- and none for anything else.
bool excellentable(const content::ItemRow& row);
// The option at `bit` (0 to 5, OpenMU's option number less one) in MuMain's own English
// (GT 622-635, docs/mu-tooltip-lines.md section 3(d)), or empty.
std::string excellentLine(const content::ItemRow& row, int bit);
// What being excellent adds to the thing itself, whatever its options:
// ItemPowerUpFactory.CreateExcellentAndAncientBasePowerUpWrappers, which MuMain's CalcDamageMin,
// CalcDefense and CalcSuccessfulBlocking agree with. All off the row's drop level.
//   weapon   both ends of the band + min x 25 / drop level + 5 (OpenMU takes the minimum for
//            both; MuMain the maximum for the top -- the server's is the one that hits)
//   armour   defence + defence x 12 / drop level + drop level / 5 + 4 (not a shield)
//   shield   defence rate + rate x 25 / drop level + 5
int excellentDamage(const content::ItemRow& row);
int excellentDefense(const content::ItemRow& row);
int excellentBlock(const content::ItemRow& row);
inline int excellentCount(uint8_t mask) {
    int n = 0;
    for (int bit = 0; bit < kExcellentOptions; ++bit) n += (mask >> bit) & 1;
    return n;
}

}  // namespace mu::sim
