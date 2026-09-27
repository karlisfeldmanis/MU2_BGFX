// What wear does to a worn thing, what it costs to put right, and how fast it comes.
//
// Three sources, which agree about the shape and not about every number:
//   * MuMain's client -- CalcMaxDurability (ZzzInventory.cpp:1572), CalcDurabilityPercent
//     (ZzzInfomation.cpp:2439), CalcRepairCost (ZzzInventory.cpp:1224) and the four colour bands
//     NewUIItemEnduranceInfo and NewUIMyInventory draw. What a player SEES.
//   * OpenMU's server -- ItemExtensions.GetMaximumDurabilityOfOnePiece, Player.cs:1968-2045 (the
//     wear), ItemPriceCalculator.CalculateRepairPrice and CalculateSellingPrice. What the server
//     CHARGES and DECIDES; MuMain has no wear at all, because the client is only ever told.
//   * The rates are OpenMU's configuration (GameConfigurationInitializerBase.cs:61-63), and NOT
//     Webzen's: the original server's numbers are not in LEGACY/reference. Chosen by the user on
//     2026-09-24 as "OpenMU's, as one knob" -- kept as the named constants below so the day the
//     original is found, or the day wear is wanted faster, is a one-line change.
//
// Where the two disagree, and which is taken:
//   * the repair price's factor: MuMain draws 3.5, OpenMU charges 3.0. Taken 3.0, because the
//     server is what takes the Zen and one number here serves both the tooltip and the till, so
//     the price shown is the price paid. MuMain's 3.5 would show 17% over what is charged.
//   * the penalty's maximum: CalcDurabilityPercent adds +1/+2 a level where CalcMaxDurability
//     has the finer table. Identical to +9, which is Refine.Cap for now; above +9 this uses the
//     finer one everywhere, so the band a stat is cut by and the band the icon is tinted by are
//     the same band.
#pragma once

#include <cstdint>

#include "content/tables.h"
#include "sim/items.h"

namespace mu::sim {

// Accumulated health damage that takes one point off a defending piece (DamagePerOneItemDurability).
constexpr double kDamagePerDurability = 2000.0;
// Landed hits that take one point off the weapon or pendant (HitsPerOneItemDurability).
constexpr double kHitsPerDurability = 10000.0;

// What a self-repair from the bag costs over a merchant's (CalcRepairCost's SelfRepair, OpenMU's
// `!npcDiscount`), and what a thing worn to nothing costs over a worn one (DestroyedItemPenalty).
constexpr double kSelfRepairFactor = 2.5;
constexpr double kBrokenRepairFactor = 1.4;
// The level a character mends his own gear from, away from any counter: MuMain enables the
// inventory's repair button at `wLevel >= 50` (NewUIMyInventory.cpp:813) and nowhere else; below
// it the blacksmith is the only way (the user, 2026-09-24). OpenMU checks no level at all -- the
// client is the gate there -- so the realm asks it here, where the window cannot be the gate.
constexpr int kSelfRepairLevel = 50;

// Whether a row wears at all: it is worn somewhere, it is not ammunition (whose durability is
// its shots), and its row gives it some. Potions, jewels, scrolls and orbs do not.
bool wears(const content::ItemRow& row);

// The most it can hold at a plus: the row's own and MU's per-level table on top, capped at 255.
// `CalcMaxDurability` less its excellent (+15) and ancient (+20), which nothing here rolls yet.
// Nought for a row that does not wear.
int maximumDurability(const content::ItemRow& row, int refinement);
// And a held thing's, which is the same and fifteen more when it is excellent
// (CalcMaxDurability, OpenMU's GetMaximumDurabilityOfOnePiece), still capped at 255.
int maximumDurability(const content::ItemRow& row, const Held& held);

// What a new one starts with -- off a shelf, off a body, into a hand: whole for gear, and the
// row's own number for anything else (a quiver's shots; the caller counts a stack).
int fullDurability(const content::ItemRow& row, int refinement);

// Whether this NPC's counter mends as well as sells: MuMain's `isRepairNpc`
// (ZzzInterface.cpp:1565) -- Hanzo the Blacksmith (251) and Eo the Craftsman (243) of the ones
// this world has. Zienna, Rhea and Bolo are the other three and live on maps not built here.
bool repairsAt(int npc);

// The share of its own contribution a worn piece loses, as CalcDurabilityPercent cuts damage,
// defence and block: nothing to half worn, then a fifth, three tenths and a half past 50, 70 and
// 80 percent gone -- and the whole of it at nought, where every one of MuMain's readers skips
// the item (`Durability != 0`). Nought for a maximum of nought.
float wearCut(int durability, int maximum);

// The four bands the icons and the slots are tinted by, off `durability <= maximum x share`
// (NewUIItemEnduranceInfo.cpp:653-668): yellow at a half, orange at three tenths, red-orange at
// a fifth, red at nought. Not the same thresholds as `wearCut`, which counts what is gone.
enum class Worn : uint8_t { Fine, Half, Third, Fifth, Broken };
Worn wornBand(int durability, int maximum);

// What it costs to put back to full: `3 x base^0.75 x missing + 1`, where the base is a third of
// the buying price, capped at 400 million; x1.4 when it is broken, x2.5 when the character does
// it himself, then MU's rounding (to 10 over 100, to 100 over 1000). Nought when nothing is
// missing or the row does not wear.
int64_t repairPrice(const content::ItemRow& row, int refinement, bool skill, int durability,
                    bool atMerchant);

// And what a worn one fetches over the counter: its selling price less 0.6 of the share gone
// (CalculateSellingPrice, ItemValue's goldType 1).
int64_t wornSellingPrice(int64_t sellingPrice, int durability, int maximum);

}  // namespace mu::sim
