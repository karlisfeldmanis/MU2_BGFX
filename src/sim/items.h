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
    // The mount's own slot, past MU's twelve: there Uniria and Dinorant are helpers in slot 8
    // and shut the pets out. Ours, the user's (2026-10-02, docs/mount.md): a pet and a mount
    // together.
    kMount = 12,
    kWorn = 13,  // MU's MAX_EQUIPMENT and the mount, and the first bag slot
    kBagColumns = 8,
    kBagRows = 8,
    kSlots = kWorn + kBagColumns * kBagRows,  // 77, MAX_MY_INVENTORY_INDEX and the mount
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
    // A powered ring's or pendant's further powers (sim::Affix), past the one its row carries:
    // 0 for none. How many it has is its colour (sim::affixCount).
    uint8_t affixes[3] = {};
    // A 2nd level wing's extras, MU's m_NewOption on it (sim::kWingMaxLife and its fellows): 0 on
    // everything else. Its own field, since `excellent` would make the wing read as excellent.
    uint8_t wing = 0;
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

// Which worn slot a thing goes in, or -1 for a thing that is not worn: every weapon, bow and
// crossbow the right hand and its quiver the left (ours, not MU's split), a shield the left
// hand, armour its group less five.
int placeOf(const content::ItemRow& row);
// A Dark Knight's second weapon: a one-handed sword, axe, mace or spear may go in his LEFT hand
// as well. MuMain's IsEquipable (an EQUIPMENT_WEAPON_RIGHT item into EQUIPMENT_WEAPON_LEFT,
// knight and not TwoHand) and OpenMU Version075's either-hand slot type for a knight's one-wide
// weapon; WebZen's gObjIsItemPut lets any hand item into either hand and leaves the class to
// the client. The bow group and the staves stay where placeOf puts them.
bool offHanded(const content::ItemRow& row, Kin kin);
// Whether it may be worn in `slot`: where placeOf puts it, or the left hand by offHanded.
bool placesIn(const content::ItemRow& row, Kin kin, int slot);
// A pet's powers (sim::PetPower): the Guardian Angel 13/0, the Imp 13/1 and the Horns of Uniria
// 13/2 and Dinorant 13/3. Nothing for any other row, and nothing -- the caller's to check -- for one whose life is gone.
PetPower petPower(const content::ItemRow& row);

// ---- the 1st level wings (docs/wings.md) ----------------------------------------------------
// Wings of Elf, Heaven and Satan, 12/0-2, slot 7, on WebZen 1.00.93's base 0.97d branch.
bool firstWing(const content::ItemRow& row);
// Its defence at a plus: the row's, 3 a plus, and from +10 the triangle every worn thing takes
// (zzzitem.cpp:880-896).
int wingDefense(const content::ItemRow& row, int refinement);
// What it does while it has life (gObjWingSprite, user.cpp:10432-10467): his blows x(112 + 2 a
// plus)%, at 1 Life a blow for the wizard's wing and 3 for the others' -- the class's, and each
// wing has one (ObjAttack.cpp:1140-1210, NEW_FORSKYLAND3 not taken, which lowers the elf's to
// 1) -- and what reaches him x(88 - 2 a plus)% (:1219-1280). As a PetPower so it folds in with
// the Imp's price and the Angel's cut.
PetPower wingPower(const content::ItemRow& row, int refinement);
// Its wear by the hour (gObjSecondDurDown, user.cpp:10866-10912): DurabilityDown(1) every tenth
// second it is worn, a point gone at the 565th -- one point in 94 minutes, 200 in 313 hours.
constexpr int64_t kWingWearTicks = 10 * 20;
constexpr double kWingWearSteps = 565.0;

// ---- the 2nd level wings (docs/second-wings.md) ----------------------------------------------
// Wings of Spirits, Soul and Dragon: the Muse Elf's, the Soul Master's and the Blade Knight's
// (OpenMU VersionSeasonSix Wings.cs, class level 2). MU's 12/3, 12/4 and 12/5, which this project
// gave the knight's orbs (OrbDefense and its fellows, 12/3-6), so **ours** are 12/13, 12/14 and
// 12/16, numbers 0.75 leaves empty (the user, 2026-10-04: 'Move the wings'). The Magic
// Gladiator's Darkness (MU's 12/6) is not in this game.
constexpr int kSpiritsNumber = 13, kSoulNumber = 14, kDragonNumber = 16;
bool secondWing(const content::ItemRow& row);
// Either level: what slot 7 takes and every rule below reads.
bool anyWing(const content::ItemRow& row);
// What a worn wing's additional option is (zzzitem.cpp:1150-1222): the 1st wings' by the wing,
// the 2nd wings' by their PLUS_WING_OP1_TYPE bit (Held::wing's kWingOptionKind) -- Spirits
// regeneration with it and damage without, Soul wizardry or regeneration, Dragon damage or
// regeneration. Its value is 1% a level of regeneration, else 4 a level.
enum class WingOption : uint8_t { Regeneration, Damage, Wizardry };
WingOption wingOption(const content::ItemRow& row, uint8_t bits);
int wingOptionValue(const content::ItemRow& row, const Held& held);
// The extras' numbers: max life and mana 50 and 5 a plus (zzzitem.cpp:3039-3044), and the chance
// a blow ignores the defence it meets -- the card's 3%; WebZen's `rand()%100 <= 3` is 4 (ours,
// the card's number).
constexpr int kWingExtraBase = 50, kWingExtraPerPlus = 5;
constexpr double kWingIgnoreChance = 0.03;
// Its extras, Held::wing, WebZen's PLUS_WING_* (zzzitem.cpp:1488-1505): max life and max mana
// +50 and 5 a plus, a 3% chance a blow ignores the defence it meets (ObjBaseAttack.cpp:1322-
// 1339), and which of its two options it carries (`PLUS_WING_OP1_TYPE`, zzzitem.cpp:1168-1222).
enum : uint8_t {
    kWingMaxLife = 0x01,
    kWingMaxMana = 0x02,
    kWingIgnoreDefense = 0x04,
    kWingOptionKind = 0x20,
};
// **Loch's Feather** (13/14), the 2nd wings' ingredient. MU drops it in Icarus alone
// (gObjMonster.cpp:4620); this game has no Icarus, so a kill in Atlans or the Lost Tower of a
// monster of kFeatherFromLevel or over leaves one in kFeatherOdds, on its own roll beside the
// rest (the user, 2026-10-04: 'Rare drop, high maps'). The maps, the level and the rate are ours.
bool lochsFeather(const content::ItemRow& row);
constexpr int kFeatherOdds = 500;
constexpr int kFeatherFromLevel = 60;
inline bool featherMap(uint32_t map) { return map == 4 || map == 7; }

// ---- rings and pendants (docs/jewellery.md) ------------------------------------------------
//
// The four 0.75 pieces in group 13 beside the pets: the Rings of Ice (8) and Poison (9), worn in
// either ring slot, and the Pendants of Lightning (12) and Fire (13), worn as the amulet. A
// dropped one is +0 to +4 (GetLevelItem's MODIFY_DROP_PREVENT_OF_RING_N_NECKLACE_LV_5_OVER) and its
// option is life regeneration, +1% to +3% of maximum life every kJewelleryRegenTicks
// (gObjRestPotionFill off rest, both rings and the pendant summed). Its resistance is WebZen's
// item.txt 1 in its own element times the plus (CItem::Convert), the largest worn counting
// (ObjCalCharacter's Max3); only Ice and Poison act on him in 0.75. A ring takes the armour's
// excellent family and a pendant the weapon's -- Lightning the staff's.
enum class Element : uint8_t { None, Ice, Poison, Lightning, Fire, Wind };
constexpr int kElements = 6;
bool ring(const content::ItemRow& row);
bool pendant(const content::ItemRow& row);
inline bool jewellery(const content::ItemRow& row) { return ring(row) || pendant(row); }
Element elementOf(const content::ItemRow& row);
// What a piece at `refinement` resists in its element: 1 a plus.
inline int resistanceOf(const content::ItemRow& row, int refinement) {
    return jewellery(row) ? std::max(0, refinement) : 0;
}
constexpr int kJewelleryMostPlus = 4;
constexpr int64_t kJewelleryRegenTicks = 7 * 20;  // m_LifeFillCount > 6, once a second

// ---- powered rings and pendants (docs/jewellery.md, "Powers") -----------------------------
//
// Ours, every number (the user, 2026-10-03: "lets make new rings and pendants, which make sense
// from classical ARPG experience, like exp gain, zen gain"). Five pieces in group 13's free 21-25,
// each with a power of its own -- its signature -- and up to three more drawn at the drop from
// the other four. How many it has is its colour: one green, two blue, three purple, four
// legendary ("if there is only 1 options its green, if more blue, purple, legendary"). A power's
// value is the plus's alone, a square curve from +0 to +9 ("+9 still to OP", then "its better"),
// and every worn piece adds its own.
// The four resistances are further powers only, no piece's signature (the user, 2026-10-04: "we
// need more resistance options for rings and pendants, all of them as potential options"): points
// in their element on top of the largest of MU's own pieces worn. Ice, Poison and Lightning turn
// aside the chill, the poison and the push r times in r + 1, WebZen's ResistanceCheck
// (ObjBaseAttack.cpp:558); Fire, which turns nothing aside in MU, takes kResistanceCut a point
// off a monster's fire blow, to kResistanceCutMost (ours). Which monster casts what:
// docs/jewellery.md "Resistances".
enum class Affix : uint8_t {
    None = 0,
    Wisdom,
    Wealth,
    Fortune,
    Leech,
    Fury,
    IceResistance,
    PoisonResistance,
    LightningResistance,
    FireResistance
};
constexpr int kAffixes = 9;
// The element a resistance power is in, None for the other five.
Element affixElement(Affix affix);
constexpr double kResistanceCut = 0.05;
constexpr double kResistanceCutMost = 0.5;
// The row's own power, or None for anything not one of the five.
Affix signatureOf(const content::ItemRow& row);
inline bool powered(const content::ItemRow& row) { return signatureOf(row) != Affix::None; }
// How many powers it carries, its signature with them: 0 on anything unpowered.
int affixCount(const content::ItemRow& row, const Held& what);
// A power's value at a plus: lo + (hi - lo) x (plus / 9)^2, rounded. Percent for all but the
// Leech, whose is life a blow.
int affixValue(Affix affix, int refinement);
const char* affixName(Affix affix);   // the card's words: "Experience from kills", ...
// The share of drops each count takes (1 to 4 powers), and the monster level the three- and
// four-power pieces start at -- the runes' kRuneRarityLevel Epic and Legendary.
constexpr double kAffixCountShare[4] = {0.55, 0.28, 0.13, 0.04};
constexpr int kAffixCountLevel[4] = {0, 0, 40, 60};
// The share of a deep kill's item drops that may draw a powered piece below its fifteen-level
// band (Realm::leave), and the lowest drop level among them (the Ring of Wealth's).
constexpr double kDeepJewellery = 0.15;
constexpr int kDeepJewelleryFrom = 18;

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
// **Its drop** (the user, 2026-10-03: "i barely see any summon orbs drops", "just fix summon orb
// drops"). In the item pool it was one row among up to seventy, on one kill in ten, and only from
// monsters up to level 18: one in 250 to 700 kills, the Goblin alone. Now its own roll, beside
// whatever else the kill leaves, from any monster its drop level reaches: one in kSummonOrbOdds,
// at +1 for every kSummonOrbLevelsAPlus levels the monster has over the orb's, up to the +4 Lala
// sells. invention, all of it.
constexpr int kSummonOrbOdds = 150;
constexpr int kSummonOrbLevelsAPlus = 12;
constexpr int kSummonOrbMostPlus = 4;
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
// Firecrackers stack too, five a cell (the user, 2026-10-04: 'allow them to stack them, max 5').
// Ours, as the potions' twenty.
constexpr int kFirecrackerStackMost = 5;
// How many of a row's pieces one cell holds: kStackMost for the drinkable potions,
// kFirecrackerStackMost for a Firecracker, 0 for a row that does not stack.
int stackMost(const content::ItemRow& row);
// Whether a row's pieces stack.
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
    uint64_t learned = 0;
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
    // Whether he is his class's second (Body::second, Sevina's): what a 2nd level wing asks.
    bool second = false;
    // His points with the stat runes in (Body::totalPoints): what a card's damage reads.
    // `points` stays the spent ones, which is what an item's asks are met by.
    HeroPoints totals;
};
// Whether this character may wear it at all: his class, and every requirement met.
bool fits(const content::Tables& tables, const Wearer& who, const Held& what);
// Whether it asks its class's second (OpenMU's class level 2, tools/cook.py's bits 8-10): the
// 2nd wings and the second class's gear (docs/second-class-gear.md).
inline bool secondClassOnly(const content::ItemRow& row) { return ((row.classes >> 8) & 7) != 0; }
// **Where the second class's gear falls** (the user, 2026-10-04: 'there will be tarkan map where
// 2nd class drop, also BC6 and strongest monsters from atlans also can drop some'). Ours: out of
// every ordinary and excellent pool but two -- Tarkan (MU's map 8, not built yet) and Blood
// Castle 6 -- where it falls by the level window as anything does; and in Atlans a kill of one
// of its strongest, kAtlansGearFromLevel or over, leaves a random piece one in kAtlansGearOdds,
// on its own dice, at +0 with a drop's luck and option. Their levels (Valkyrie 46, Vepar 45,
// Bahamut 43) never reach the gear's 70-100. The level and the rate are a proposal.
constexpr uint32_t kTarkanMap = 8;
constexpr int kAtlansGearFromLevel = 43;
constexpr int kAtlansGearOdds = 400;
// Whether a thing in one hand and a thing going into the other are one hand too many: a
// two-handed weapon wants the other hand empty or a quiver, and a bow or crossbow wants it
// empty or holding its own ammunition. Beast.Handful.
bool handful(const content::Tables& tables, const Satchel& bag, const Held& what, int hand);
// Whether a move from one slot to another would be taken -- into a worn slot only from the
// bag and only where it goes and only if it fits, and only if what it takes down (the slot's
// own, and the other hand when the two cannot be held together) finds room in the bag; into
// the bag if the rectangle is clear of everything but the thing itself and at most one other
// it would swap with. Beast.Movable.
bool movable(const content::Tables& tables, const Wearer& who, const Satchel& bag, int from,
             int to);
// The move itself, after `movable` said yes: what is in the way comes back to where this one
// was, and onto a worn slot whatever else comes down goes wherever the bag has room. Beast.Move.
bool move(const content::Tables& tables, const Wearer& who, Satchel& bag, int from, int to);

// Pours as much of `what` as fits into the stack at `at`, and says how many went. Nothing
// where `tops` says no. The satchel or the vault: both keep a thing at its top-left.
template <class Grid>
int topUp(const content::Tables& tables, Grid& grid, int at, const Held& what) {
    if (!tops(tables, grid[at], what)) return 0;
    Held onto = grid[at];
    const int most = stackMost(tables.items[size_t(onto.item)]);
    const int went = std::min<int>(what.durability, most - onto.durability);
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
        stack.durability = int16_t(std::min<int>(what.durability, stackMost(row)));
        after.put(at, stack);
        what.durability = int16_t(what.durability - stack.durability);
        if (landed < 0) landed = at;
    }
    grid = after;
    return landed;
}

// ---- refining with jewels (docs/refining.md) ----------------------------------------------
//
// The highest plus a jewel or a drop gives: the Soul stops at +9 on its own, and a drop must not
// hand out what only the machine makes. MU2's Refine.Cap. The Chaos Machine's +10 and +11 go
// past it to kMachineCap, Version075's MaximumItemLevel (sim/machine.h).
constexpr int kRefineCap = 9;

// Which of the jewels let go over a thing a row is: the Bless (14, 13), the Soul (14, 14) or
// the Life (14, 16). The Chaos (12, 15) is a jewel too and goes on nothing -- it is the
// machine's.
enum class Jewel : uint8_t { None, Bless, Soul, Life };
Jewel jewelOf(const content::ItemRow& row);

// Whether a jewel would go on a thing, asked by the realm's refusal and by the bag's drop
// colour. OpenMU's `CanLevelBeUpgraded` and the two handlers' ranges, which MuMain's
// `CanUpgradeItem` paints by to the level: the weapon and armour groups up to the boots, less
// the arrows and the bolts, a Bless on +0 to +5 and a Soul on +0 to +8. A Life goes on what
// carries the additional option (takesOptions) and on a wing, below kMostOption -- WebZen's
// gObjItemRandomOption3Up refuses group 12 from 12/7 up and the arrows and bolts
// (user.cpp:28761-28790), so no ring or pendant.
bool refinable(const content::Tables& tables, const Held& jewel, const Held& target);

// The Jewel of Life (gObjItemRandomOption3Up, user.cpp:28754-28880): the option a level up at
// m_iLifeRate, 50, and a failure takes it back to none. A lucky thing always takes it -- ours,
// the user's (2026-10-04: "if luck is good for item there is 100% for sucedd"); WebZen's roll
// never asks luck. A wing at no option draws which of its two kinds the option is
// (PLUS_WING_OP1_TYPE, `rand()%2`, :28797-28808), the 2nd wings only.
constexpr int kLifeChance = 50;

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
// The share is ours: WebZen's 1/2000 of the item chance was one kill in 20,000, and the user
// asked for ten times that (2026-10-06) -- 1/200, one kill in 2,000.
constexpr double kExcellentShareOfItem = 1.0 / 200.0;
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
    Spirits = 12,
    Pyroblast = 13,
    Inferno = 14,
    Glacier = 15,
    Venom = 16,
    Thunder = 17,
    Tempest = 18,
    Fireburst = 19,
    FireRing = 20,
    Bulwark = 21,
    Kinship = 22,
    Wrath = 23,
    Ironskin = 24,
    Steadfast = 25,
    SecondWind = 26,
    Whirlwind = 27,
    Volley = 28,
    // The stat runes (sim::statShareOf), two tiers of each.
    LesserMight = 29,
    GreaterMight = 30,
    LesserGrace = 31,
    GreaterGrace = 32,
    LesserVigor = 33,
    GreaterVigor = 34,
    LesserInsight = 35,
    GreaterInsight = 36,
    LesserAscendance = 37,
    GreaterAscendance = 38,
    Hellfire = 39,
    Twister = 40,
    Burn = 41,
    Plague = 42,
    PlagueArrows = 43,
    Scorch = 44,
    // The Common runes (kCommon*), each a faint copy of a Legendary.
    Cinder = 45,
    Gust = 46,
    Chill = 47,
    FaintEcho = 48,
    Spite = 49,
    Wisp = 50
};
// **A rune's group** (the user, 2026-10-02: "we need to start group runes which is only for
// specific classes, for specific weapon slots"): which classes may set it, a bit a class, and
// which sockets take it, a bit a kind of thing. `settable` asks both, the drop draws only a
// rune the killer's class may set, and the card prints both.
constexpr uint8_t classBit(Kin kin) { return uint8_t(1u << unsigned(kin)); }
constexpr uint8_t kWizardOnly = classBit(Kin::DarkWizard);
constexpr uint8_t kElfOnly = classBit(Kin::FairyElf);
constexpr uint8_t kKnightOnly = classBit(Kin::DarkKnight);
constexpr uint8_t kEveryClass = kWizardOnly | kElfOnly | kKnightOnly;
constexpr uint8_t kInWeapon = 1;     // either hand's weapon, never a shield
constexpr uint8_t kInShield = 2;
constexpr uint8_t kInArmour = 4;     // helm, armour, pants, gloves, boots, and wings (2026-10-05)
constexpr uint8_t kInJewellery = 8;  // the rings and the pendant
// **A rune's rarity** (the user, 2026-10-02: "we need also make group of rarity of runes"), on
// WoW's ladder as the item names are (game/ui describe's qualityOf): Rare blue, Epic purple,
// Legendary orange. A Rune of Creation that drops draws its rarity first, at kRuneRarityShare,
// out of the rarities that hold a rune the killer's class may set, and then one of those runes
// evenly. Rare is a number on a narrow stat; Epic a power that answers a blow or holds him up
// in a fight (Stormcall and Meteor, a proc on one more monster; Renewal and Kinship, which the
// user rates above a stat); Legendary strikes a crowd or opens a build no other rune does
// (Bulwark's shieldless guard). A quest's rune is the quest's. invention.
// **Common**, white, below them all (the user, 2026-10-06: 'super early game runes also which is
// like super light version of legendary runes', 'green runes lets say', 'or white'): a Legendary's
// power at a fraction, the only rune a monster under kCreationLevel lets fall. White and not
// green, as green is a legendary item's. Last in the enum so the rest keep their numbers.
enum class Rarity : uint8_t { Rare = 0, Epic = 1, Legendary = 2, Common = 3 };
constexpr int kRarities = 4;
// What a Common rune sells for, of the Jewel of Creation's 12,000,000 every other rune fetches:
// 120,000, so a Lorencia kill does not pay a fortune (seen on the card, 2026-10-06). Ours.
constexpr double kCommonRuneSale = 0.01;
// The rarity a fuse of three makes (sim/machine.h): Common to Rare, Rare to Epic, Epic to
// Legendary; a Legendary is the top and gives itself back.
constexpr Rarity nextRarity(Rarity rarity) {
    return rarity == Rarity::Common ? Rarity::Rare
           : rarity == Rarity::Rare ? Rarity::Epic
                                    : Rarity::Legendary;
}
constexpr double kRuneRarityShare[kRarities] = {0.60, 0.30, 0.10, 0.25};
const char* rarityName(Rarity rarity);
struct PowerRow {
    Power power;
    const char* name;
    const char* tells;
    uint8_t classes;  // who may set it, classBit each
    uint8_t slots;    // what takes it, kIn* each
    Rarity rarity;
    // Set only by a class's second -- Blade Knight, Soul Master, Muse Elf (the user, 2026-10-04:
    // 'we need that some stronger runes is for 2nd classes', then the class legendaries locked).
    bool second = false;
    // Whether a hero of this class, his second or not, may set it and have it work.
    bool takenBy(Kin kin, bool isSecond) const {
        return (classes & classBit(kin)) != 0 && (!second || isSecond);
    }
    // A weapon's power: read off the hands, and rolled on a swing, an arrow or a spell.
    bool weapon() const { return (slots & kInWeapon) != 0; }
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
// **Frenzy** (the Pit's boots), Diablo 3's Barbarian's shape (the user, 2026-10-02: "in diablo 3
// frenzy was little bit different"): every wound he deals -- swing, arrow or spell -- adds a stack
// of kFrenzyStackSpeed attack speed and spell MagicSpeed, up to kFrenzyMostStacks, and sets the
// time left back to kFrenzyTicks; when it runs out the stacks go together. D3's is +15% a stack,
// five, four seconds; this speed is MU's points (the Ale's twenty rides beside it), so a stack is
// eight and the five are forty. One stack a wound however many runes he wears. invention.
constexpr int kFrenzyStackSpeed = 8;
constexpr int kFrenzyMostStacks = 5;
constexpr int64_t kFrenzyTicks = 80;  // four seconds of the realm's twenty ticks
// **Renewal**, Devin's since 2026-09-30, in the Undying's place (the user: "life regeneration
// rune which is much more important i think than just max hp"): this share of maximum health
// every three seconds (kRecoverEveryTicks), anywhere, in a fight too -- 0.75 gives health back
// only on a safe tile, at kHealthRecoveryInSafeZone, which it rides beside. Each one worn adds.
constexpr double kRenewalShare = 0.03;
// **Evil Spirit**, the Pit's shield (the user, 2026-10-01: "shield with socket and rune which on
// miss has chance to cast evil spirits"), every class's and in a shield alone: a monster's blow
// that misses him has this chance to let MU's Evil Spirit go round him for nothing -- the
// wizard's skill 9 (Version075 SkillsInitializer.cs:51-52: damage 45, no element), struck as
// WebZen's server strikes it (CObjUseSkill::SkillEvil, 1.00.93 ObjUseSkill.cpp:2606-2645): every
// monster within ten tiles of him (`gObjCalDistance < 10`, no wall asked) has two chances in three
// (`rand()%3 < 2`) of one wizardry blow, each held a random 0-2000 ms
// (gObjAddAttackProcMsgSendDelay) -- while MuMain's spirits wander round him for their 49 frames.
// MuMain's own strikes off each spirit every fifteen frames (ZzzEffectJoint.cpp:3755-3760) are
// built only without CSK_EVIL_SKILL and are the client's, not the server's rule. A knight's or an
// elf's blow takes his energy's band, as every magic rune takes it (kRuneEnergyLow). A miss while
// any is held rolls nothing. The wizard casts the same off the Scroll of Evil Spirit
// (sim/skills.cpp). Invention: the rune and its trigger; the spell is WebZen's.
constexpr double kSpiritChance = 0.15;
constexpr float kSpiritReach = 10.0f;      // `< 10`: a tile short of it
constexpr double kSpiritOdds = 2.0 / 3.0;  // rand()%3 < 2
constexpr int64_t kSpiritDelayTicks = 40;  // rand()%2000 ms, the realm's twenty ticks a second
// **The stat runes** (the user, 2026-10-05: "+10% all stats ... +10% agility, energy, vitality,
// str"), then 'lets keep only +10 and +30': Might is strength, Grace agility, Vigor vitality,
// Insight energy, Ascendance all four, at 10 percent (Lesser) and 30 (Greater). Every class's, in
// any socket -- Ascendance a weapon's alone (the user, 2026-10-05: 'runes which increase all
// stats only on weapons'). The share is of
// the points he has spent, summed over every rune worn, and is added on top of them for all that
// a point buys (Body::totalPoints) but never for what an item asks to be worn: a rune that let
// the piece it sits in be worn would hold itself up. The tiers are mine: a single stat at 10 is
// Rare, at 30 Epic; all four at 10 Epic, at 30 Legendary. invention.
// Percent of each stat this power adds, all zero for a power that is not a stat rune.
HeroPoints statShareOf(Power power);
// Nullptr for none and for a number no row has.
const PowerRow* powerOf(uint8_t power);
// The Rune of Creation's row: 14, 22.
bool creation(const content::ItemRow& row);
// Who may carry sockets: the option-bearing set, weapons, armour and shields, and the rings and
// pendants, which roll them as a drop as the rest do and take only an armour's rune (settable).
inline bool takesSockets(const content::ItemRow& row) {
    return takesOptions(row) || jewellery(row);
}
// What may hold sockets at all: all of those, and the wings, which roll none as a drop but take
// them at the Chaos Machine's Add Socket (the user, 2026-10-05: 'allow to add sockets to wings
// on chaos machine') and then an armour's rune (settable).
inline bool socketsFit(const content::ItemRow& row) { return takesSockets(row) || anyWing(row); }
constexpr int kMostSockets = 3;
// A ring holds one socket at most (the user, 2026-10-02: "maximum amount of sockets for rings
// is 1"); everything else kMostSockets. Every way in caps it here: a drop, a quest, a debug lay.
inline int mostSocketsOf(const content::ItemRow& row) { return ring(row) ? 1 : kMostSockets; }
// A drop's chance of a socket, drawn after luck and the option, and then of each further one
// (the user, 2026-09-28: "item drop with +socket is rare"; 2026-10-01: "incerase drop rate for
// armors,weapons with +socket" -- 0.5% and 25% were; 2026-10-06: "+socket is not so special
// thing", "its not super common, but its not a super big thing" -- 3% was). invention.
constexpr double kSocketChance = 0.06;
constexpr double kMoreSocketChance = 0.30;
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
// The monster level each rune rarity starts at (the user, 2026-10-02: an Epic out of Dungeon 1
// "does not make sense", then "i think legendary can start drop only in lostter 5", "not only
// but starting at"): a Rare from kCreationLevel, an Epic from 40 -- the Dungeon's Hell Spiders
// down, Devias's Ice Queen -- and a Legendary from 60, the Lost Tower's fifth floor (Devil 60,
// Death Knight 62) and anything harder after it. A rarity a kill cannot reach is out of the
// draw, and kRuneRarityShare is shared among the rest. invention.
// A Common from the first monster, and the only rune below kCreationLevel.
constexpr int kRuneRarityLevel[kRarities] = {kCreationLevel, 40, 60, 1};
// A rune's power as a drop draws it, for a monster of `level` and a hero of `kin`: one his class
// may set -- an element rune only where his class throws something of its element
// (elementServes) -- its rarity drawn first at kRuneRarityShare among the rarities `level`
// reaches that hold one, then one of that rarity evenly. 0 when none can be drawn.
class Random;
// `commons` false leaves the Common runes out: a reward's rune (Blood Castle) is never one.
uint8_t drawRunePower(Random& dice, Kin kin, bool second, int level, bool commons = true);
// The Bless (14, 13), the Soul (14, 14), the Life (14, 16) and the Chaos (12, 15): the
// kJewelChance draws. The Life falls from 72 (OpenMU Version095d Jewels.cs:43), past Devias.
bool refiningJewel(const content::ItemRow& row);
// The first socket with nothing set in it, or -1.
int freeSocket(const Held& thing);
// Whether this rune may be set into that thing by this class: a Creation with a power, a thing
// with a free socket, of a kind the power's group takes, and a class in that group.
bool settable(const content::Tables& tables, const Held& jewel, const Held& target, Kin kin,
              bool second);

// **Stormcall**, the Dark Knight's first power: a swing that lands has this chance to call
// lightning down on another monster within `kStormcallReach` tiles of him, which takes his
// swing's roll at `kStormcallForce` and is pushed as Lightning pushes. invention. 20%, as the
// first quests' three runes all are (the user, 2026-10-01). Every class's since 2026-10-03: the
// elf's arrows roll it as the swing does, and the wizard's spells once a cast (Realm::land).
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
// **Pyroblaster**, the Dark Wizard's second weapon rune (the user, 2026-10-02: "DW only rune,
// which makes Fireball 50% stronger and there is chance of chain reactiion with 4 monsters
// dooing fireballs"): his Fire Ball strikes at kPyroblastForce, and one that lands has
// kPyroblastChance, each one worn rolling, to burst into kPyroblastChain more, each flying from
// the monster it struck at another within kPyroblastReach tiles of it and in its sight, the
// nearest first. They are his Fire Ball at the cast's force, paying nothing, and burst no
// further. invention, all of it.
constexpr float kPyroblastForce = 1.5f;
constexpr double kPyroblastChance = 0.20;
constexpr int kPyroblastChain = 4;
// Eight tiles, Evil Spirit's ten less two: at five most bursts in Lorencia found nothing near.
constexpr float kPyroblastReach = 8.0f;
// **The element runes** (the user, 2026-10-02: "make new runes with +20% fire damage, ice damage,
// poison damage, lighting damage, wind damage"): Inferno, Glacier, Venom, Thunder and Tempest,
// every class's, in a weapon's, a ring's or a pendant's socket, each adding kElementRuneDamage to
// what his blows of its element deal, summed over every one worn. What is of an element is
// `skillElement` (sim/realm_fight.cpp): 0.75's own elementalModifier where it has one (Fire Ball
// and Flame fire, Ice, Poison, Lightning), and ours past it -- Meteorite fire where OpenMU says
// Earth, as nothing here is earth; Cyclone and Twisting Slash wind, as 0.75's one wind spell,
// Twister, is not built; the runes' own blows their spells' (Meteor fire, Stormcall lightning,
// Frost Arrow ice, the Poison rune's sickness poison). Drops only to a class with something of
// its element (`elementServes`). invention, all of it.
constexpr double kElementRuneDamage = 0.20;
// **Wrath** (the user, 2026-10-03: "+20% damage rune for all classes ... its legendary i guess"),
// the Balrog's rune: every blow he deals -- a swing, an arrow, a spell, a rune's own -- takes
// kWrathDamage more for each set in his hands, laid with the skill's multiplier in strikeAt.
// Every class's, a weapon's socket only. ours.
constexpr double kWrathDamage = 0.20;
// **Four more** (the user, 2026-10-04, picked from a list of ideas: 'defence', 'chance to block',
// 'a kill restores of health and mana', 'Twisting Slash has chance pulls monsters in and hits 50%
// harder'). Invention, all four.
// **Ironskin** (Rare, every class's armour and shield): kIronskinDefense more defence for each,
// on top of the set's.
constexpr double kIronskinDefense = 0.10;
// **Steadfast** (Rare, every class's armour and shield): a blow that would land has
// kSteadfastBlock a rune to be blocked and miss, swing or spell, to kSteadfastMost at the most.
constexpr double kSteadfastBlock = 0.05;
constexpr double kSteadfastMost = 0.25;
// **Second Wind** (Epic, every class's armour and jewellery): kSecondWindShare of maximum health
// and of mana back after each kill, beside an excellent weapon's eighth.
constexpr double kSecondWindShare = 0.05;
// **Whirlwind** (Legendary, a knight's weapon): Twisting Slash strikes kWhirlwindDamage harder
// for each in his hands, and a cast has kWhirlwindChance to pull every monster within
// kWhirlwindReach tiles in to him first -- slid along the ground, stopped by walls -- and strike
// those that land within its reach.
constexpr double kWhirlwindDamage = 0.50;
constexpr double kWhirlwindChance = 0.25;
constexpr float kWhirlwindReach = 6.0f;
// **Piercing Volley** (Legendary, the Muse Elf's weapon; the user, 2026-10-04: 'make legendary
// strong rune that penetration can be used as multishot'): her Penetration is loosed as
// sim::kVolleyLanes arrows in Skillshot's fan, kFanDegrees apart, each flying on through
// everything in its lane at Penetration's own force, each body struck once a cast as the fan
// strikes it. Every shot, no chance; a second one worn adds nothing. invention.
// **The knight's fire runes** (the user, 2026-10-02: "pyroblast chance for DK weapon", "inferno
// chance for DK weapon"), in a knight's weapon alone. **Fireburst**: a swing that lands has
// kFireburstChance to burst into the Pyroblaster's chain -- kPyroblastChain Fire Balls, each
// flying from the monster the last struck at the nearest it has not, within kPyroblastReach.
// **Ring of Fire**: a swing that lands has kFireRingChance to let the wizard's Inferno ring go
// round him, striking every monster within its four tiles. Each blow of either is his swing's
// roll with his energy's band on top, as Stormcall's is (kRuneEnergyLow/High), at its force,
// and fire, so Inferno runes raise it. Half Stormcall's chance, as each strikes several.
// invention, all of it.
constexpr double kFireburstChance = 0.10;
constexpr float kFireburstForce = 0.8f;
constexpr double kFireRingChance = 0.10;
constexpr float kFireRingForce = 1.0f;
// **The knight's spell runes** (the user, 2026-10-06: 'rune for DK that there is chance of
// hellfire coming from character on hits', and the same of twister), Legendary (they strike a
// crowd; Epic until the user's 'ok do it', 2026-10-06), any knight's weapon.
// **Hellfire**: a swing that lands has kHellfireRuneChance to set the wizard's Hellfire ring
// round him, striking every monster within its four tiles. **Twister**: a swing that lands has
// kTwisterRuneChance to send the wizard's storm walking out from his feet toward what he struck,
// its three strikes on everything within a tile and a half of it then. Each blow is his rune's
// (runeStrike), Hellfire fire and Twister wind, so Inferno and Tempest runes raise them; each a
// little under Ring of Fire's, as the wizard's ladder puts both spells under Inferno.
// invention, all of it.
constexpr double kHellfireRuneChance = 0.10;
constexpr float kHellfireRuneForce = 0.9f;
constexpr double kTwisterRuneChance = 0.10;
constexpr float kTwisterRuneForce = 0.8f;
// **The Common runes** (Rarity::Common), each a Legendary at a fraction, every one first-class:
// **Cinder** Ring of Fire's ring and **Gust** Twister's storm, a knight's; **Chill** Frost Arrow's
// freeze for half as long with half its wound, the elf's; **Faint Echo** Arcane Echo's second
// cast, the wizard's; **Spite** a quarter of Wrath, any weapon; **Wisp** Evil Spirit off a miss at
// half its blow, any shield or jewellery. invention, all of it.
constexpr double kCinderChance = 0.05;
constexpr float kCinderForce = 0.4f;
constexpr double kGustChance = 0.05;
constexpr float kGustForce = 0.35f;
constexpr double kChillChance = 0.08;
constexpr int64_t kChillTicks = 20;
constexpr float kChillWound = 0.5f;  // of Frost Arrow's second wound
constexpr double kFaintEchoChance = 0.07;
constexpr double kSpiteDamage = 0.05;
constexpr double kWispChance = 0.05;
constexpr float kWispForce = 0.5f;
// **Immolate** (the user, 2026-10-06: 'chance to burn monsters to do some % of damage of hp and
// their has to be some burn efekt to the monster'), Epic, any knight's weapon: a swing that lands
// has kBurnRuneChance to set the monster burning for kBurnRuneTicks, a pulse every
// kBurnRuneEvery taking kBurnRuneShare of its maximum health (kBurnRuneFloor of the swing at the
// least) -- never more a pulse than the top
// of his swing, so a boss is not melted by its own size -- fire, so Inferno runes raise it. A
// burn may kill, as a blow does; a second restarts it. Drawn as flames along its bones and an
// ember tint (game/play_show.cpp). **Scorch** is the wizard's own (the user, 2026-10-06: 'burn
// rune also can be used for DW, but in context that inferno, fireball, meteor, flame has a chance
// to burn', then 'its different rune for DW'), Epic, in his staff: the same burn, rolled by each
// blow of those four spells, his staff's swing never. invention.
constexpr double kBurnRuneChance = 0.15;
constexpr double kBurnRuneShare = 0.03;
// The least a pulse takes: this share of the swing that lit it, so the burn still reads on
// Lorencia's monsters, whose 3% is a point or two (sim_test, 2026-10-06).
constexpr double kBurnRuneFloor = 0.15;
constexpr int32_t kBurnRuneTicks = 80;
constexpr int32_t kBurnRuneEvery = 20;
// **Spirit Plague** (the user, 2026-10-06: 'Evil Spirits has chance to poison monster and do
// some percentage of monster hp'), Epic, every class's, in a weapon, a shield, a ring or the
// pendant: each blow an Evil Spirit lands -- the wizard's spell's or an Evil Spirit rune's --
// has kPlagueChance for each worn (to kPlagueMost) to poison what it struck, as the Poison spell
// does and for as long, each pulse kPlagueShare of its maximum health: no less than
// kBurnRuneFloor of the spirit's blow, no more than the blow itself, so a boss is not melted.
// The poison's own rule holds: a pulse never takes the last point. A second restarts it.
// **Plague Arrows** (the user, 2026-10-06: 'similiar rune for multi shot for elf, where multi
// shot has chance to poison enemy similiar like evil spirits'), Epic, the elf's weapon: each
// lane of a fan of arrows -- Multi-Shot's three, and Penetration's under a Piercing Volley ('this
// should also work on rune where penetration become multi-shot') -- rolls the chance as it is
// loosed, is drawn a little green ('so user at least sees which arrows was with poison'), and
// poisons every body it lands in at the same share. invention.
constexpr double kPlagueChance = 0.20;
constexpr double kPlagueMost = 0.50;
constexpr double kPlagueShare = 0.03;
// **Bulwark** (the user: "allow to use defense skill without shield"), a knight's weapon's: his
// Defense goes up with no shield on his arm -- a second weapon, a two-handed one or an empty
// hand -- off his strength and agility alone (`guardShare` with no shield's defence in it).
// invention.
// **Kinship** (the user: "remove guardian angel or imp debuffs"), every class's, in a ring or
// the pendant: the worn pet's price is lifted -- the Guardian Angel's x0.8 on his blows (ours,
// sim::petPower) and the life the Imp and the Horn of Dinorant take for each blow they raise.
// Their gifts stay. invention.
// The element a rune adds to, None for every other power.
Element elementOf(Power power);
// Whether `kin` throws anything of `element`, so a rune of it would do something in his hands.
bool elementServes(Element element, Kin kin);

// ---- the Firecracker (docs/drop-boxes.md §8) ------------------------------------------------
//
// MU's Box of Luck at level 2 (14, 11), its own row here (source/items/misc/MagicBox03.json).
// Thrown on the ground it is spent and opens: WebZen's FireCrackerOpenEven (Event.cpp:1201, the
// MODIFY_DROP_ITEM_OF_FIRE_CRACKER_EVENT_20050316 body 1.00.93 builds). Two in ten an item off
// eventitembag5 at the thrower's feet with a firework over it, else Zen (kFirecrackerZenLeast).
// Realm::crack.
bool firecracker(const content::ItemRow& row);
// `rand()%10 < g_ItemDropRateForgFireCracker`, 2 in WebZen's own 0.99.60T commonserver.cfg.
constexpr int kFirecrackerItemIn10 = 2;
// Every eventitembag5 row is level 5, and outside Korea `GetLevel + rand()%5` (gLanguage != 0):
// +5 to +9.
constexpr int kFirecrackerPlus = 5;
constexpr int kFirecrackerPluses = 5;
// Where it falls (the user, 2026-10-02: "only from dungeons like dungeon, lost-tower, bc, and
// devil square", then "lets do 1 of 300 to all dungeons"): a kill on a dungeon's map -- the
// Dungeon (1), the Lost Tower (4), Devil Square (9) and Blood Castle (11 to 17) -- of a monster
// of kFirecrackerFromLevel or over, one in kFirecrackerOdds, beside whatever else it leaves.
// MU's own was any map under Atlans at 10 in 10,000 while its event ran, from Giant-level (17)
// monsters (gObjMonster.cpp ~5040); the maps and the rate are ours.
constexpr int kFirecrackerOdds = 300;
constexpr int kFirecrackerFromLevel = 17;
inline bool firecrackerMap(uint32_t map) {
    return map == 1 || map == 4 || map == 9 || (map >= 11 && map <= 17);
}
// **Where a mount is ridden**: every map, the dungeons and Blood Castle with the open ones, off a
// safe tile, as MU rides (the user, 2026-10-05: 'allow to use mount in dungeons and BC', undoing
// 2026-10-04's open maps alone).
inline bool rideMap(uint32_t) { return true; }
// **Icarus**, MU's map 10, the sky land (docs/icarus-port.md): entered, and stood in, only by one
// who can fly. Any wing worn (WebZen asks its presence, not its life) or a Horn of Dinorant with
// life left, and never with the Horn of Uniria worn (gObjMoveGate, user.cpp:27201-27220; OpenMU
// 0.95d's CanFly, Icarus.cs:46-50; MuMain's inventory will not equip Uniria there,
// NewUIMyInventory.cpp:351-354).
constexpr uint32_t kIcarusMap = 10;
bool canFly(const content::Tables& tables, const Satchel& bag);
// **Blood Castle's ticket** (docs/blood-castle-port.md, Part A 2.2): the Scroll of Archangel
// (13, 16) and the Blood Bone (13, 17) of one level make the Invisibility Cloak (13, 18) of that
// level in the Chaos Machine. The level is the castle's, carried in the refinement.
bool scrollOfArchangel(const content::ItemRow& row);
bool bloodBone(const content::ItemRow& row);
bool invisibilityCloak(const content::ItemRow& row);
// The Divine Staff of Archangel (5,10) the Statue of Saint gives up: a quest item here, carried
// back to the Archangel and worn by nobody (the user, 2026-10-03: 'we need that archange staff
// is quest item and its not ussable for any char').
bool divineStaff(const content::ItemRow& row);
// Any of the three Archangel weapons a Statue of Saint gives up -- the Divine Staff, Sword and
// Crossbow (sim/event.h kArchangelWeapons): quest items, worn by nobody, never sold.
bool archangelWeapon(const content::ItemRow& row);
// **Sevina's treasures** (sim/quests.cpp, docs/class-change-quest.md): MU's Broken Sword (14, 24),
// Tear of Elf (14, 25) and Soul of Wizard (14, 26), each a class's, carried back to her and
// never sold. They fall on the Lost Tower's last floor and in Atlans (Realm::treasure), one kill
// in 50 while the class's quest stands -- the rate ours (the user, 2026-10-06: 1 in 150 was too
// low, "at 2%").
bool classTreasure(const content::ItemRow& row);
constexpr int kTreasureIn10000 = 200;
constexpr uint32_t kLostTowerMap = 4, kAtlansMap = 7;
// A treasure lies twice the time an ordinary drop does, so it is not lost to a fight. Ours.
constexpr int kTreasureLingerSeconds = 120;
// WebZen gObjMonster.cpp:5823-5985: a kill anywhere but a Blood Castle rolls the scroll, then the
// bone, `rand()%10000 < rate` with the code's defaults (Gamemain.cpp:1133-1134), and one that
// lands is the kill's whole drop (`return TRUE`). 128 its durability, as WebZen's ItemSerialCreate.
// The rates are OpenMU's 1% each (EventTicketItems.cs:29-30), not WebZen's 10 and 20: the user,
// 2026-10-04, 'we need to increase drop for bc ticket ingreadients'.
constexpr int kScrollOfArchangelIn10000 = 100;
constexpr int kBloodBoneIn10000 = 100;
inline bool bloodCastleMap(uint32_t map) { return map >= 11 && map <= 17; }
// Its level by the monster's: +1 under 32, +2 under 45, +3 under 57, +4 under 68, +5 under 76,
// +6 under 84, +7 from there (gObjMonster.cpp:5863-5900; a +7 cannot be combined at 0.97d).
constexpr int castleMaterialLevel(int monsterLevel) {
    return monsterLevel < 32 ? 1 : monsterLevel < 45 ? 2 : monsterLevel < 57 ? 3
         : monsterLevel < 68 ? 4 : monsterLevel < 76 ? 5 : monsterLevel < 84 ? 6 : 7;
}

// The Zen when no item comes, into the purse as every Zen here (Realm::leave). MU's is
// MoneyItemDrop(2004, ...), the year; ours rolls kFirecrackerZenLeast to kFirecrackerZenMost
// (the user, 2026-10-04: 'we need to give much more zen ... because item +9 can sold from a lot
// of zen'). The band is what the item sells for: over 3,900 cracked items a median of 41,800,
// most gear 30,000 to 130,000 a piece, the Bless and Soul at 2 and 3 million pulling the mean
// to 148,270.
constexpr int64_t kFirecrackerZenLeast = 30000;
constexpr int64_t kFirecrackerZenMost = 100000;
// WebZen's eventitembag5.txt (0.99.60T's Data, the Firecracker's and the Heart of Love's bag; its
// header still calls it the Christmas star's), row for row, by MU's group and number. A row this
// tree has no item for is left out of the draw, so the draw is even over what is here.
struct BagRow {
    int8_t group = 0;
    int16_t number = 0;
};
// ---- the Box of Luck and the Box of Kundun (docs/kundun-box.md, drop-boxes.md section 2) --------
//
// MU's 14, 11 at level 0 and at levels 8-12; here, as the Firecracker, a row each at a free number
// (source/items/misc/MagicBox01.json and MagicBox08.json), the Kundun box's tier its plus, +1 to
// +3 (invention). Thrown on the ground each is spent and opens (Realm::crack), with no show, as
// MU's open with none. The raid's: the Golden Budge Dragons' and the Golden Dragon's
// (docs/golden-dragon-raid.md section 1, Loot).
bool boxOfLuck(const content::ItemRow& row);
bool boxOfKundun(const content::ItemRow& row);
constexpr int kKundunTiers = 3;
// An item, of which an excellent one; else the Zen (EledoradoBoxOpenEven, Event.cpp:603,
// docs/kundun-box-sources.md 2.2-2.3). **Ours, rising with the tier** (the user, 2026-10-06: 'we
// need that there is decenet excelent drop for any drop of kundum', 'but not to crasy'): an
// excellent in 12, 17.5 and 24 boxes in a hundred ('we need better excelent drop rate from box
// of kundum'). WebZen's shipped rates (0.99.60T
// commonserver.cfg) were 30/25/20 and 5/4/3 -- 1.5, 1 and 0.6 in a hundred, rarer as the box
// got better.
constexpr int kKundunItemIn100[kKundunTiers] = {30, 35, 40};
constexpr int kKundunExcellentIn100[kKundunTiers] = {40, 50, 60};
constexpr int64_t kKundunZen[kKundunTiers] = {50000, 100000, 150000};
// A plain row's level is the bag file's plus `rand()%addlevel` (2 for +1 to +3): +5/+6 from
// the +1 box, +4/+5 from the +2 and +3.
constexpr int kKundunPlainLevel[kKundunTiers] = {5, 4, 4};
constexpr int kKundunAddLevel = 2;
// The Box of Luck as OpenMU's Version095d opens it (BoxOfLuck.cs): one in two an item at +6
// off its list, else 10,000 Zen. WebZen's own eventitembag.txt is not in hand.
constexpr int kLuckItemIn100 = 50;
constexpr int kLuckPlus = 6;
constexpr int64_t kLuckZen = 10000;
// MU's own item and box choices, the skill rolled and not given as the Firecracker's.
constexpr int kBoxLuckIn100 = 50;
inline constexpr BagRow kLuckBag[] = {
    {0, 3}, {0, 5}, {0, 9}, {0, 10}, {0, 13}, {4, 4}, {4, 5}, {4, 9}, {4, 11}, {4, 12}, {5, 0},
    {5, 2}, {5, 3}, {5, 4}, {12, 15}, {14, 13}, {14, 14},
    // Bronze, Pad, Bone, Leather, Scale, Sphinx, Brass, Vine, Silk and Wind, helm to boots
    {7, 0}, {8, 0}, {9, 0}, {10, 0}, {11, 0}, {7, 2}, {8, 2}, {9, 2}, {10, 2}, {11, 2},
    {7, 4}, {8, 4}, {9, 4}, {10, 4}, {11, 4}, {7, 5}, {8, 5}, {9, 5}, {10, 5}, {11, 5},
    {7, 6}, {8, 6}, {9, 6}, {10, 6}, {11, 6}, {7, 7}, {8, 7}, {9, 7}, {10, 7}, {11, 7},
    {7, 8}, {8, 8}, {9, 8}, {10, 8}, {11, 8}, {7, 10}, {8, 10}, {9, 10}, {10, 10}, {11, 10},
    {7, 11}, {8, 11}, {9, 11}, {10, 11}, {11, 11}, {7, 12}, {8, 12}, {9, 12}, {10, 12}, {11, 12},
};
// WebZen's eventitembag8..10.txt (0.99.60T; docs/kundun-box-sources.md 2.4), each in its two
// pools: the plain rows (`//일반아이템`) and the excellent ones (`//엑설런트`). By name off our own
// rows; Thunder Staff is 0.75's Lightning Staff and Katache its Katana.
inline constexpr BagRow kKundunPlain1[] = {
    {12, 15}, {14, 14}, {14, 13}, {13, 0}, {13, 1}, {13, 2}, {7, 5}, {8, 5}, {9, 5}, {10, 5},
    {11, 5}, {7, 2}, {8, 2}, {9, 2}, {10, 2}, {11, 2}, {7, 10}, {8, 10}, {9, 10}, {10, 10},
    {11, 10}, {6, 1}, {6, 2}, {6, 4}, {5, 0}, {5, 1}, {5, 2}, {0, 0}, {0, 2}, {0, 4}, {1, 1},
    {1, 2}, {2, 0}, {2, 1}, {3, 2}, {4, 1}, {4, 2}, {4, 8}, {4, 9}, {4, 10},
};
inline constexpr BagRow kKundunExcellent1[] = {
    {13, 8}, {13, 9}, {13, 12}, {13, 13}, {7, 5}, {8, 5}, {9, 5}, {10, 5}, {11, 5}, {7, 2},
    {8, 2}, {9, 2}, {10, 2}, {11, 2}, {7, 10}, {8, 10}, {9, 10}, {10, 10}, {11, 10}, {6, 1},
    {6, 2}, {6, 4}, {0, 0}, {0, 1}, {0, 2}, {0, 4}, {1, 2}, {2, 0}, {2, 1}, {3, 2}, {4, 1},
    {4, 8}, {4, 9}, {5, 0}, {5, 1},
};
inline constexpr BagRow kKundunPlain2[] = {
    {12, 15}, {14, 14}, {14, 13}, {14, 16}, {13, 0}, {13, 1}, {7, 12}, {8, 12}, {9, 12},
    {10, 12}, {11, 12}, {7, 8}, {8, 8}, {9, 8}, {10, 8}, {11, 8}, {7, 7}, {8, 7}, {9, 7},
    {10, 7}, {11, 7}, {0, 10}, {0, 11}, {0, 13}, {1, 6}, {1, 7}, {2, 3}, {3, 7}, {3, 0},
    {3, 4}, {5, 3}, {4, 4}, {4, 3}, {4, 11},
};
inline constexpr BagRow kKundunExcellent2[] = {
    {13, 8}, {13, 9}, {13, 12}, {13, 13}, {7, 6}, {8, 6}, {9, 6}, {10, 6}, {11, 6}, {7, 4},
    {8, 4}, {9, 4}, {10, 4}, {11, 4}, {7, 11}, {8, 11}, {9, 11}, {10, 11}, {11, 11}, {5, 2},
    {0, 10}, {0, 5}, {0, 13}, {1, 6}, {1, 7}, {2, 3}, {3, 7}, {3, 0}, {4, 11}, {4, 4}, {4, 12},
};
inline constexpr BagRow kKundunPlain3[] = {
    {12, 15}, {14, 14}, {14, 13}, {14, 16}, {7, 3}, {8, 3}, {9, 3}, {10, 3}, {11, 3}, {7, 13},
    {8, 13}, {9, 13}, {10, 13}, {11, 13}, {7, 7}, {8, 7}, {9, 7}, {10, 7}, {11, 7}, {0, 11},
    {0, 15}, {0, 14}, {3, 4}, {4, 5}, {4, 12}, {5, 4},
};
inline constexpr BagRow kKundunExcellent3[] = {
    {13, 8}, {13, 9}, {13, 12}, {13, 13}, {7, 12}, {8, 12}, {9, 12}, {10, 12}, {11, 12},
    {7, 8}, {8, 8}, {9, 8}, {10, 8}, {11, 8}, {7, 7}, {8, 7}, {9, 7}, {10, 7}, {11, 7},
    {5, 3}, {0, 11}, {0, 15}, {1, 8}, {4, 12},
};

inline constexpr BagRow kFirecrackerBag[] = {
    // weapons and shields
    {0, 2}, {0, 4}, {0, 3}, {1, 1}, {1, 2}, {2, 0}, {2, 1}, {4, 8}, {4, 9}, {5, 0}, {6, 4},
    {6, 1}, {6, 2}, {0, 10}, {0, 11}, {0, 13}, {1, 5}, {1, 6}, {1, 7}, {2, 3}, {3, 7}, {3, 4},
    {4, 11}, {4, 4}, {4, 12}, {5, 3}, {6, 5}, {6, 8},
    // Leather, Pad, Bronze, Wind, Spirit and Sphinx, helm to boots
    {7, 5}, {8, 5}, {9, 5}, {10, 5}, {11, 5}, {7, 2}, {8, 2}, {9, 2}, {10, 2}, {11, 2},
    {7, 0}, {8, 0}, {9, 0}, {10, 0}, {11, 0}, {7, 12}, {8, 12}, {9, 12}, {10, 12}, {11, 12},
    {7, 13}, {8, 13}, {9, 13}, {10, 13}, {11, 13}, {7, 7}, {8, 7}, {9, 7}, {10, 7}, {11, 7},
    // the Bless, the Soul and the Chaos, which come bare
    {14, 13}, {14, 14}, {12, 15},
};

}  // namespace mu::sim
