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

#include <algorithm>
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
    kGroupPets = 13,
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
    // Its sockets (see "sockets and the Rune of Creation" below): how many it rolled, 0 to 3,
    // and the power set in each -- 0 for an empty one. A Rune of Creation carries its own power
    // in `powers[0]`.
    uint8_t sockets = 0;
    uint8_t powers[3] = {};
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
// A Dark Knight's second weapon: a one-handed sword, axe, mace or spear may go in his LEFT hand
// as well. MuMain's IsEquipable (an EQUIPMENT_WEAPON_RIGHT item into EQUIPMENT_WEAPON_LEFT,
// knight and not TwoHand) and OpenMU Version075's either-hand slot type for a knight's one-wide
// weapon; WebZen's gObjIsItemPut lets any hand item into either hand and leaves the class to
// the client. The bow group and the staves stay where placeOf puts them.
bool offHanded(const content::ItemRow& row, Kin kin);
// Whether it may be worn in `slot`: where placeOf puts it, or the left hand by offHanded.
bool placesIn(const content::ItemRow& row, Kin kin, int slot);
// A pet's powers (sim::PetPower): the Guardian Angel 13/0 and the Imp 13/1. Nothing for any
// other row, and nothing -- the caller's to check -- for one whose life is gone.
PetPower petPower(const content::ItemRow& row);

// Whether it is ammunition: the bow group's 7 and 15.
bool ammunition(const content::ItemRow& row);
// Whether it is drunk for a pool: the apple and three healing potions, the three mana potions.
bool heals(const content::ItemRow& row);
bool restores(const content::ItemRow& row);

// The Ale (14, 9): OpenMU's AlcoholEffectInitializer, which Version075 hangs on the row as its
// ConsumeEffect -- a flat +20 on AttackSpeedAny for 80 seconds. MuMain's client says the same
// twenty from the other side: CheckHack takes 20 back off the speed it reports while
// ABILITY_FAST_ATTACK_SPEED stands (Winmain.cpp:162). A second one replaces the first rather
// than stacking (ApplyMagicEffectConsumeHandlerPlugIn disposes the effect of the same subtype).
bool ale(const content::ItemRow& row);
// The Antidote, group 14 number 8: it clears a poison and does nothing else.
bool antidote(const content::ItemRow& row);
// **The Orb of Summoning (12/11) read at its plus.** One row teaches all six summons in 0.75:
// SummoningOrbConsumeHandlerPlugIn teaches `30 + item.Level`, so +0 is the Goblin and +5 Bali.
// This is that row as the plus makes it -- the skill, its name and line, the energy the summon
// asks (0.75's SkillsInitializer.cs:68-73: 30, 60, 90, 130, 170, 210) -- and, ours, the user's
// (2026-09-28), named for what it raises: "Orb of Goblin", "Orb of Golem" and on. Every other row
// comes back as it is. Reading and the tooltip both go through here, so the two cannot differ.
bool summoningOrb(const content::ItemRow& row);
content::ItemRow asRead(const content::ItemRow& row, int refinement);
constexpr int kAleSpeed = 20;
constexpr int64_t kAleTicks = 80 * 20;
// The Town Portal Scroll (14, 10): TownPortalScrollConsumeHandlerPlugIn, a warp to the map's
// safe zone spawn gate, at once. Lorencia's own safe zone is its own (BaseMapInitializer's
// SafezoneMapNumber), so on this map it is the box a death rises in.
bool portal(const content::ItemRow& row);

// ---- stacks ---------------------------------------------------------------------------------
//
// The user's, 2026-09-27, as WoW keeps its potions: pieces of one kind pour into one cell,
// counted in `Held::durability`, up to twenty a cell. INVENTION: 0.75 caps nothing and merges
// nothing -- a bought three is its own cell for good and a dropped potion takes a fresh one.
constexpr int kStackMost = 20;
// Whether a row's pieces stack: the drinkable potions.
bool stacks(const content::ItemRow& row);
// Whether `what` may pour into `onto`: the same stacking row at the same plus, and room left.
bool tops(const content::Tables& tables, const Held& onto, const Held& what);

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
    // His wizardry band and his staff's rise (`Fighter`, `Body::staffRise`), so a scroll's card
    // can print the damage a spell will do in his hands, as the spell's own card does.
    double wizardMinimum = 0.0, wizardMaximum = 0.0, wizardryRate = 1.0;
    float staffRise = 0.0f;
    // The weapon family in his right hand and the shield's in his left (`familyOf`), so an orb's
    // card can say whether what it teaches can be thrown with what he holds, as the skill's own
    // card does (`SkillRow::suits`).
    uint32_t hand = 0, offHand = 0;
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

// Pours as much of `what` as fits into the stack at `at`, and says how many went. Nothing
// where `tops` says no. The satchel or the vault: both keep a thing at its top-left.
template <class Grid>
int topUp(const content::Tables& tables, Grid& grid, int at, const Held& what) {
    if (!tops(tables, grid[at], what)) return 0;
    Held onto = grid[at];
    const int went = std::min<int>(what.durability, kStackMost - onto.durability);
    onto.durability = int16_t(onto.durability + went);
    grid.put(at, onto);
    return went;
}

// Puts a thing away as WoW does: a stacking thing tops up the stacks of its kind in `[first,
// last)` in cell order, and what is left takes free cells a full stack at a time; anything else
// takes the first free cell. All of it or none of it -- the cell the first piece went to, or -1
// with the grid as it was.
template <class Grid>
int pour(const content::Tables& tables, Grid& grid, int first, int last, Held what) {
    if (what.item < 0 || size_t(what.item) >= tables.items.size()) return -1;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    if (!stacks(row)) {
        const int at = grid.free(tables, row.width, row.height);
        if (at >= 0) grid.put(at, what);
        return at;
    }
    Grid after = grid;
    int landed = -1;
    what.durability = int16_t(std::max<int>(1, what.durability));
    for (int at = first; at < last && what.durability > 0; ++at) {
        const int went = topUp(tables, after, at, what);
        if (went > 0 && landed < 0) landed = at;
        what.durability = int16_t(what.durability - went);
    }
    while (what.durability > 0) {
        const int at = after.free(tables, row.width, row.height);
        if (at < 0) return -1;
        Held stack = what;
        stack.durability = int16_t(std::min<int>(what.durability, kStackMost));
        after.put(at, stack);
        what.durability = int16_t(what.durability - stack.durability);
        if (landed < 0) landed = at;
    }
    grid = after;
    return landed;
}

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
// cannot disagree with it. Ours on top: the Rune of Creation and anything with a socket.
bool expensive(const content::Tables& tables, const Held& what);

// ---- luck and the additional option -------------------------------------------------------
//
// Who may carry them: every weapon, armour piece and shield (Weapons.cs:305-313,
// ArmorInitializerBase.cs:185, 401-406), which is the refinable set -- the arrows and bolts
// have neither. A drop's rolls are WebZen's, not OpenMU's quarter and quarter
// (gObjMonster.cpp:4690-4714, 1.00.93): luck at 4 in 100, and the option by one of three draws,
// each a third -- +12 under 4 in 100, +8 under 8, +4 under 12 -- so 8% in all, the higher the
// rarer. An excellent thing draws luck at 1 in 100 and the option the same way. The skill's
// 6 in 100 has no place here: skills are orbs.
constexpr int kLuckIn100 = 4;
constexpr int kExcellentLuckIn100 = 1;
constexpr int kOptionUnder[3] = {4, 8, 12};  // for option level 3, 2, 1
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
// and this game takes them on the user's word (2026-09-27). WebZen's rules (gObjMonster.cpp,
// 1.00.93): one kill in 2000 (m_wExcellentDropRate) is an excellent draw, which then drops only
// as an item would (rand()%ItemRate < ItemDropPer) -- here the user's item chance, so 1/2000 of
// it. From what a monster 25 levels lower drops (GetItem(Level-25)), always at +0, with
// NewOptionRand's options (gObjMonster.cpp:3125-3142): one of six, the second bit (0x02) drawn
// again half the time, and a quarter of the time a second bit or'ed on, which may be the same.
constexpr double kExcellentShareOfItem = 1.0 / 2000.0;
constexpr int kExcellentLevelDelta = 25;          // GetItem(lpObj->Level-25)
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

// ---- a complete set -------------------------------------------------------------------------
//
// **Five pieces of one set worn together** -- helm, armour, pants, gloves and boots of one
// number in groups 7 to 11, as MuMain's CheckFullSet matches them (`Type % MAX_ITEM_INDEX`) --
// raise his whole defence by kSetDefense, and by kExcellentSetDefense when all five are
// excellent. The user's, 2026-09-30. INVENTION: CheckFullSet pays nothing below +10 on every
// piece, then 5% at +10 and 5% more a plus to 30% at +15 (ZzzInfomation.cpp:3306-3330, on the
// final defence as here); a set of any plus, and the excellent step, are ours.
constexpr double kSetDefense = 0.05;
constexpr double kExcellentSetDefense = 0.10;
// The set a row belongs to -- its number -- or -1 for anything that is not one of the five.
int setOf(const content::ItemRow& row);
// What the five worn pieces raise the defence by: 0, kSetDefense or kExcellentSetDefense.
double setDefense(const content::Tables& tables, const Satchel& bag);

// ---- sockets and the Rune of Creation ----------------------------------------------------
//
// **invention**, the user's (2026-09-28): an item may roll **+Socket** as it rolls +Luck -- up to
// three of them -- and each socket takes one epic rune. The rune is the **Rune of Creation**,
// MU's Jewel of Creation (14, 22, Season 2's) renamed on the user's word, carrying a **power**,
// which it gives the item it is set into. Very rare on purpose: the first completion of a city
// quest gives one, never a repeat, and later the highest drops. Set by dropping the rune on the
// item, as a Bless goes on (`Realm::refine`), into its first empty socket. Each power set rolls
// on its own. The design page is claude.ai/artifact/DPhyHWRcYTo97PHpa2FaAu.
enum class Power : uint8_t {
    None = 0,
    Stormcall = 1,
    Meteor = 2,
    Ice = 3,
    Poison = 4,
    Frost = 5,
    Echo = 6,
    Undying = 7,
    KeenEye = 8,
    Bloodwell = 9,
    Frenzy = 10,
    Renewal = 11,
    Spirits = 12
};
struct PowerRow {
    Power power;
    const char* name;
    const char* tells;
    bool weapon;  // true a weapon's socket, false armour's or a shield's
    Kin kin;      // who may set it
    bool everyone = false;  // every class may, and `kin` is not read
    bool shieldOnly = false;  // a shield's socket and no armour's
};

// **The Undying**, the first armour power and every class's (the user, 2026-09-30: "its for
// all classes the same"): x1.2 on maximum health for each set in anything worn, armour or
// shield. Devin's first clear pays it (sim/quests.cpp). Invention, as every rune is.
constexpr double kUndyingHealth = 1.2;
// The Dungeon's three, the Golden Archer's chain (the user, 2026-09-30: "same runes for all, for
// helm, pants, boots ... life steal, mana gain back, increase attack speed, increased chance of
// critical hit"). Every class's, in any armour's socket, as the Undying. Invention, all of it.
// **Keen Eye** (the Catacombs' helm): critical chance, on top of luck's kLuckCritical a piece.
constexpr double kKeenEyeCritical = 0.10;
// **Bloodwell** (the Halls' pants): a share of every wound he deals comes back as life, spells
// and arrows too, and a share of his mana after every kill (beside the excellent's eighth).
constexpr double kBloodwellLife = 0.03;
constexpr double kBloodwellMana = 0.05;
// **Frenzy** (the Pit's boots): a blow he lands has this chance to raise his attack speed and a
// spell's MagicSpeed by kFrenzySpeed for kFrenzyTicks -- the Ale's twenty, which rides beside it.
constexpr double kFrenzyChance = 0.15;
constexpr int kFrenzySpeed = 20;
constexpr int64_t kFrenzyTicks = 60;  // three seconds of the realm's twenty ticks
// **Renewal**, Devin's since 2026-09-30, in the Undying's place (the user: "life regeneration
// rune which is much more important i think than just max hp"): this share of maximum health
// every three seconds (kRecoverEveryTicks), anywhere, in a fight too -- 0.75 gives health back
// only on a safe tile, at kHealthRecoveryInSafeZone, which it rides beside. Each one worn adds.
constexpr double kRenewalShare = 0.03;
// **Evil Spirit**, the Pit's shield (the user, 2026-10-01: "shield with socket and rune which on
// miss has chance to cast evil spirits"), every class's and in a shield alone: a monster's blow
// that misses him has this chance to let MU's Evil Spirit go round him for nothing -- the
// wizard's skill 9 (Version075 SkillsInitializer.cs:51-52: damage 45, no element). MuMain throws
// four spirits that wander round the caster for 49 frames, each striking all within 150 units
// whenever its LifeTime is a multiple of 15 -- frames 4, 19 and 34 (ZzzCharacter.cpp:4585-4603,
// ZzzEffectJoint.cpp:3737-3765); here every monster within kSpiritReach tiles and in his sight
// takes kSpiritPulses wizardry blows that far apart, his energy's band on top as every magic rune
// takes it (kRuneEnergyLow). A miss while any is going rolls nothing. The wizard casts the same
// spirits off the Scroll of Evil Spirit (sim/skills.cpp). Invention: the trigger, the reach and
// the rune are ours, the spell and its beat MU's.
constexpr double kSpiritChance = 0.15;
constexpr int kSpiritPulses = 3;
constexpr int64_t kSpiritFirstTicks = 3;   // MU's four frames, 0.16 s
constexpr int64_t kSpiritEveryTicks = 12;  // MU's fifteen, 0.6 s
constexpr float kSpiritReach = 4.0f;
// Nullptr for none and for a number no row has.
const PowerRow* powerOf(uint8_t power);
// The Rune of Creation's row: 14, 22.
bool creation(const content::ItemRow& row);
// Who may roll sockets: the option-bearing set, weapons, armour and shields.
inline bool takesSockets(const content::ItemRow& row) { return takesOptions(row); }
constexpr int kMostSockets = 3;
// A drop's chance of a socket, drawn after luck and the option, and then of each further one
// (the user, 2026-09-28: "item drop with +socket is rare"). invention.
constexpr double kSocketChance = 0.005;
constexpr double kMoreSocketChance = 0.25;
// A kill's jewels (the user, 2026-10-01: "increase drop rate for jewel of bless, jewel of soul,
// jewel of chaos and jewel of creation"). invention, all of it. MU drops the three from its
// jewel group at 1 in 1000 (Loot.Jewel, the group shared with the Ale, the Town Portal and the
// pets, which keep that rate); here they have their own roll, five times it, still bound by
// their drop levels -- the Chaos from 12 to 66, the Bless from 25, the Soul from 30. The Rune of
// Creation, which fell from nothing, falls from a monster at kCreationLevel or over, one kill in
// 2000, carrying a power the killer's class may set.
constexpr double kJewelChance = 0.005;
constexpr double kJewelGroupChance = 0.001;
constexpr double kCreationChance = 0.0005;
constexpr int kCreationLevel = 15;
// The Bless (14, 13), the Soul (14, 14) and the Chaos (12, 15): the three kJewelChance draws.
bool refiningJewel(const content::ItemRow& row);
// The first socket with nothing set in it, or -1.
int freeSocket(const Held& thing);
// Whether this rune may be set into that thing by this class: a Creation with a power, a thing
// with a free socket, of the power's kind (weapon or not), and the class the power names.
bool settable(const content::Tables& tables, const Held& jewel, const Held& target, Kin kin);

// **Stormcall**, the Dark Knight's first power: a swing that lands has this chance to call
// lightning down on another monster within `kStormcallReach` tiles of him, which takes his
// swing's roll at `kStormcallForce` and is pushed as Lightning pushes. invention. 20%, as the
// first quests' three runes all are (the user, 2026-10-01).
constexpr double kStormcallChance = 0.20;
constexpr float kStormcallReach = 4.0f;
constexpr float kStormcallForce = 1.0f;
// And a magic rune's blow is magic: Stormcall's lightning and Frost Arrow's second wound add
// his energy on top, the wizard's own band -- `energy / 9` to `energy / 4` (ClassDarkWizard.cs:
// 72-73) -- whatever his class (the user, 2026-10-01: "dmg scale with his energy"). Arcane Echo
// needs none: it is the spell again, which is the wizardry sum already. invention.
constexpr double kRuneEnergyLow = 1.0 / 9.0;
constexpr double kRuneEnergyHigh = 1.0 / 4.0;
// **Meteor**, his second: the same chance and reach, and a burning rock -- the wizard's
// Meteorite, drawn as his -- lands its fall later at `kMeteorForce` of his swing's roll. invention.
constexpr double kMeteorChance = 0.15;
constexpr float kMeteorForce = 1.5f;
// **Ice** and **Poison**, his third and fourth (the user, 2026-09-29: "lets create Ice and Poision
// also"; a knight's weapon powers, on the monster he struck): the same chance, and the wizard's
// spell's own element on what his swing left standing -- Ice's chill (its `chillTicks`, walking
// at `kChillFactor`), Poison's pulses (its `poisonTicks`, each a quarter of the swing's wound).
// No blow of their own. invention.
constexpr double kIceRuneChance = 0.15;
constexpr double kPoisonRuneChance = 0.15;
// **Frost Arrow**, the Fairy Elf's first (the user, 2026-09-29: "15% chance to freeze monster
// with some extra damage"), in a bow's or a crossbow's socket: an arrow that lands has the same
// chance to freeze what it struck -- no step and no swing for `kFrostTicks` -- and to wound it
// again for `kFrostWound` of the arrow's own. Drawn as the wizard's Ice on it. invention.
constexpr double kFrostChance = 0.20;  // 20% since 2026-10-01, as Stormcall
constexpr int64_t kFrostTicks = 40;  // two seconds of the realm's twenty ticks
constexpr float kFrostWound = 0.5f;
// **Arcane Echo**, the Dark Wizard's first (the user, 2026-09-29: "casting abilities has 15%
// chance to cast twice"), in a staff's socket: a spell he lets go has this chance to be let go
// again `kEchoTicks` later -- the same spell, at the same aim and force, for no mana and no
// cooldown, and an echo never echoes. Lightning's echo is its sweep run once more when the
// channel ends, without his arm and without holding him. invention.
constexpr double kEchoChance = 0.20;  // 20% since 2026-10-01, as Stormcall
constexpr int64_t kEchoTicks = 6;  // 0.3 s: two throws, read apart

}  // namespace mu::sim
