#include "game/ui/card.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/quests.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/swings.h"

namespace mu::game {
namespace {

using gfx::Box;

// **The window, 2026-10-10** (the user's proposal, https://claude.ai/artifact/E1SgVrhmhQNquWSv2EfcLR,
// settled over a morning: 'always show stats, even there is no stat to spend like on original',
// 'dont usee - and apply, keep existing logic to add points', 'remember that window has fixed
// height'). The same 190x429 and the same frame, his name, guild, level and resets small under its
// title; inside it, from the top: his class with his unspent points; the three numbers his class
// lives by; and three
// pages under underline tabs -- Attributes (MU's four wells, each with what it buys under it, the
// plus beside it while there is a point to spend), Combat and Bonuses. Every page is laid out to
// fit between the tabs and the foot; nothing in the window scrolls or grows it.

// The head's rows, in MU's panel units.
constexpr float kIdTop = 45.0f;
constexpr float kKeysTop = 61.0f, kKeysTall = 30.0f;
constexpr float kTabsTop = 99.0f, kTabsTall = 16.0f;
constexpr int kTabCount = 3;
const char* const kTabWords[kTabCount] = {"Attributes", "Combat", "Bonuses"};
// The page between the tabs' floor and the foot's rule.
constexpr float kBodyTop = 122.0f;
// And where it must end: four units over the foot's rule.
constexpr float kBodyBottom = panel::kFootRule - 4.0f;

// The wells run between the grid's two edges (`panel::kGridX` to `kWellRight`), as the bag's and
// the shelf's do, and type sits seven in from them.
constexpr float kWellX = panel::kGridX, kWellW = panel::kWellRight - panel::kGridX;
constexpr float kLeft = 18.0f;
constexpr float kRight = panel::kValueRight;

// An attribute: its well, the lines under it, and the air before the next one.
constexpr float kAttrTall = 20.0f, kLineStep = 12.0f, kLineTop = 3.0f, kAttrGap = 8.0f;
// The plus, inside the well's right end.
constexpr float kPlusSide = 13.0f;
// A Combat or Bonuses group: its heading, its rows, the air after it.
constexpr float kGroupHead = 10.0f, kGroupGap = 6.0f;

// The foot: the level's progress, at the panel's shared rule.
constexpr float kFootRule = panel::kFootRule;
// **Reset points is the foot** (the user, 2026-10-20: "reset poont has to be in footer actualy",
// then "reset points has to be full width button and complet footer, exp is going under"): the
// button across the wells at the foot's top, the level's progress under it.
constexpr Box kResetSquare{panel::kGridX, kFootRule, panel::kWellRight - panel::kGridX, 16.0f};
constexpr float kFootRight = kRight;
constexpr float kExpTop = kResetSquare.y + kResetSquare.h + 5.0f;
constexpr Box kBar{panel::kGridX, kExpTop + 12.0f, kFootRight - panel::kGridX, 4.0f};

// Type, in units. A figure is a size above its label, so the eye runs down the numbers. A size
// up from where they were (Crylos on Discord, 2026-10-25: "increase the font size for stats its so
// tiny"), and the lines' steps with them.
constexpr float kClassSize = 9.0f, kNameSize = 9.5f, kFigureSize = 10.5f, kLineSize = 8.5f,
                kSmallCaps = 7.0f, kKeySize = 9.5f, kTabSize = 7.5f,
                kRowSize = 8.5f;

constexpr uint32_t kHeading = style::kBoneHi;
constexpr uint32_t kPlain = style::kBone2;
constexpr uint32_t kQuiet = style::kAshInk;
constexpr uint32_t kDim = style::kAshInk2;
// Points to spend are the one thing on the card that asks for a click: the accent.
constexpr uint32_t kSpendable = style::kBloodHi;
constexpr uint32_t kSpendWash = gfx::rgba(0.165f, 0.051f, 0.035f);
// The open tab: golden, not blood (the user's, for a chosen row, 2026-09-30).
constexpr uint32_t kChosen = gfx::rgba(0.941f, 0.843f, 0.604f);
constexpr uint32_t kChosenBar = gfx::rgba(0.788f, 0.639f, 0.353f);
constexpr uint32_t kDivider = style::kAsh3;
// A rune row's pips (Card::pipsAt): their size and step, in units, and the
// hole an empty one's ring is drawn round.
constexpr float kPipRadius = 1.9f, kPipPitch = 5.4f;
// A Combat or Bonuses row, in units: the room above its words and under them (kRowPadLeast on
// a page too long), from a name's baseline to the capitals of the effect under it, and from
// one line of the effect to the next; the effect's type, and the share of a face's ascent its
// capitals stand. The rule between rows, a step up from the well.
constexpr float kRowPad = 4.5f, kRowPadLeast = 2.0f, kNoteGap = 3.5f, kNoteStep = 9.0f, kNoteSize = 7.5f,
                kCapShare = 0.72f;
constexpr uint32_t kRowStroke = gfx::rgba(0.200f, 0.157f, 0.125f);
// Where a row's words start and its figures end: 5 units in from the well's sides.
constexpr float kRowLeft = kWellX + 5.0f, kRowRight = kWellX + kWellW - 5.0f;
constexpr uint32_t kPipHole = gfx::rgba(0.075f, 0.067f, 0.055f);
// The Bonuses page's values, each in what it is: a stat's, an element's, a fortune's colour.
// Never red: red reads as a loss (the user, 2026-10-23).
constexpr uint32_t kStrengthInk = gfx::rgba(1.0f, 0.647f, 0.282f);
constexpr uint32_t kAgilityInk = gfx::rgba(0.553f, 0.851f, 0.459f);
constexpr uint32_t kVitalityInk = gfx::rgba(1.0f, 0.871f, 0.408f);
constexpr uint32_t kEnergyInk = gfx::rgba(0.502f, 0.706f, 1.0f);
constexpr uint32_t kIceInk = gfx::rgba(0.561f, 0.847f, 1.0f);
constexpr uint32_t kPoisonInk = gfx::rgba(0.588f, 0.878f, 0.333f);
constexpr uint32_t kLightningInk = gfx::rgba(1.0f, 0.910f, 0.443f);
constexpr uint32_t kFireInk = gfx::rgba(1.0f, 0.627f, 0.251f);
constexpr uint32_t kWindInk = gfx::rgba(0.529f, 0.937f, 0.808f);
constexpr uint32_t kExperienceInk = gfx::rgba(0.765f, 0.643f, 1.0f);
constexpr uint32_t kZenInk = gfx::rgba(1.0f, 0.820f, 0.353f);
constexpr uint32_t kFindInk = gfx::rgba(1.0f, 0.639f, 0.290f);
constexpr uint32_t kLifeOnHitInk = gfx::rgba(0.475f, 0.902f, 0.427f);
constexpr uint32_t kCriticalInk = gfx::rgba(1.0f, 0.765f, 0.431f);

// **"Each press adds" under the general info** (the user, 2026-10-20: "each press adds
// container has to be above stats under the general info"): its row stands under the three his
// class lives by while there are points in hand, on every page, and the tabs and the page come
// down by it. The canvas's measures at half its pixels: a control 16 tall.
constexpr float kCtlTall = 16.0f;
constexpr float kSwitchTop = kKeysTop + kKeysTall + 6.0f;
constexpr float kSwitchRow = kCtlTall + 6.0f;
// How far the tabs and the page come down: the switch's row while there are points to spend.
float dropFor(int points) { return points > 0 ? kSwitchRow : 0.0f; }

Box tabBox(int i, float drop) {
    const float w = kWellW / float(kTabCount);
    return {kWellX + w * float(i), kTabsTop + drop, w, kTabsTall};
}

constexpr float kStepLabelW = 64.0f;
constexpr Box kStepBox{kWellX + kStepLabelW, kSwitchTop, kWellW - kStepLabelW, kCtlTall};
// Where the attributes must end: the page's floor, nothing of the reset's on the page.
constexpr float kAttrBottom = kBodyBottom - 2.0f;
// **The reset's question is the foot while it is up**: one void well where the button stands --
// its words, then No and Reset at its right -- and under it, where the level's progress stands,
// what a reset does or, in red, why it cannot.
constexpr Box kAskBox{kWellX, kFootRule - 3.0f, kWellW, 22.0f};
constexpr float kAskButtonTall = 15.0f, kAskPad = 3.5f;
constexpr Box kYesBox{kWellX + kWellW - kAskPad - 36.0f, kAskBox.y + (kAskBox.h - kAskButtonTall) * 0.5f, 36.0f,
                      kAskButtonTall};
constexpr Box kNoBox{kYesBox.x - 3.0f - 28.0f, kYesBox.y, 28.0f, kAskButtonTall};
// The plus a little wider when it says "+10" or "All".
constexpr float kWidePlus = 22.0f;
// Sheet::over and pushed past the four pluses.
constexpr int kStepIds = 10, kResetId = 20, kNoId = 21, kYesId = 22;

// The switch's parts as shown: 1 | 10 | All, or 1 | All under ten points in hand (the user,
// 2026-10-20: "if total skill points is less that 10 dont show 10 as option").
int stepParts(int points, int* steps) {
    int n = 0;
    steps[n++] = 1;
    if (points >= 10) steps[n++] = 10;
    steps[n++] = 0;
    return n;
}
// What a press spends now: the step, a 10 chosen under ten points read as All.
int stepNow(int step, int points) { return step == 10 && points < 10 ? 0 : step; }
int gainOf(int step, int points) { return step == 0 ? points : std::min(step, points); }

// Where each attribute's well stands: one after another, each pushed down by the lines the one
// before it carries, from the page's top (`drop` under the switch's row). Returns the step
// between lines, which closes up (and the gaps with it) for a class whose lines would run into
// the floor.
float attrTops(const size_t counts[4], float tops[4], float drop) {
    size_t lines = 0;
    for (int i = 0; i < 4; ++i) lines += counts[i];
    const float top = kBodyTop + drop;
    float step = kLineStep, gap = kAttrGap;
    const float fixed = 4.0f * (kAttrTall + kLineTop);
    if (lines > 0 && top + fixed + 3.0f * gap + step * float(lines) > kAttrBottom) {
        gap = 5.0f;
        step = std::min(kLineStep, (kAttrBottom - top - fixed - 3.0f * gap) / float(lines));
    }
    float y = top;
    for (int i = 0; i < 4; ++i) {
        tops[i] = y;
        y += kAttrTall + kLineTop + step * float(counts[i]) + gap;
    }
    return step;
}
Box plusFor(float top, int step) {
    const float w = step == 1 ? kPlusSide : kWidePlus;
    return {panel::kWellRight - w - 3.0f, top + (kAttrTall - kPlusSide) * 0.5f, w, kPlusSide};
}

std::string number(long long v) { return panel::commas(v); }
std::string band(long long lo, long long hi) { return number(lo) + " ~ " + number(hi); }
std::string pair(long long now, long long most) { return number(now) + " / " + number(most); }
std::string percent(double share) { return std::to_string(int(std::lround(share * 100.0))) + "%"; }
std::string plus(int v) { return "+" + std::to_string(v); }
std::string plusPercent(int v) { return "+" + std::to_string(v) + "%"; }

// ---- what a stat buys: the card its name raises ------------------------------------------------
//
// The user, 2026-10-23: "we need some tooltips for general stats for str,agi,vit,ene", then "how
// it affect each class" -- a player asking on Discord which stat shortens a cooldown. Every figure
// is read off the rules the realm reckons with (sim::rowOf, maximumMana, attackSpeedStat,
// magicSpeedStat, kAgilityPerDoubling...), at ten points, which is what the 10 under the stats
// spends; nothing here is a copy that can drift from them.
constexpr float kStatTipWide = 250.0f;

// A tenth-rounded figure with its sign: "+1.7", "+15".
std::string tenths(double v) {
    char out[24];
    const double r = std::round(v * 10.0) / 10.0;
    if (std::fabs(r - std::round(r)) < 1e-6) std::snprintf(out, sizeof out, "+%d", int(std::lround(r)));
    else std::snprintf(out, sizeof out, "+%.1f", r);
    return out;
}

tip::Sheet statSheet(int stat, sim::Kin kin, const int totals[4]) {
    using sim::Kin;
    const bool knight = kin == Kin::DarkKnight, wizard = kin == Kin::DarkWizard;
    const bool elf = kin == Kin::FairyElf, gladiator = kin == Kin::MagicGladiator;
    const sim::ClassRow& row = sim::rowOf(kin);
    constexpr double kTen = 10.0;
    static const char* const kNames[4] = {"Strength", "Agility", "Vitality", "Energy"};

    tip::Sheet sheet;
    sheet.name = kNames[stat];
    sheet.nameTone = tip::Tone::White;
    // No class line under the name: it is always his own (the user, 2026-10-23: "dont show for
    // the dark knight, becaue user aleady plays that class").
    sheet.wide = kStatTipWide;

    tip::Section every;
    every.kicker = "Every 10 points";
    const auto row2 = [&](const std::string& label, const std::string& value, tip::Tone tone = tip::Tone::White) {
        every.rows.push_back(tip::Row{label, {tip::Value{value, tone}}, "", tip::Tone::White});
    };
    const auto band = [](double lo, double hi) { return tenths(lo) + " ~ " + tenths(hi).substr(1); };
    tip::Section notes;
    const auto note = [&](const std::string& text) {
        notes.rows.push_back(tip::Row{"", {}, text, tip::Tone::Gray});
    };
    sim::HeroPoints ten;
    const int shield = sim::maximumShield(0, sim::HeroPoints{10, 0, 0, 0}, 0) - sim::maximumShield(0, {}, 0);

    if (stat == 0) {
        if (elf) {
            row2("Bow damage", band(kTen / 14.0, kTen / 8.0));
            row2("Melee damage", band(kTen * row.minimumDamagePerStrengthAndAgility,
                                      kTen * row.maximumDamagePerStrengthAndAgility));
        } else {
            row2(wizard ? "Weapon damage" : "Damage",
                 band(kTen * row.minimumDamagePerStrength, kTen * row.maximumDamagePerStrength));
        }
        row2("Attack rate", tenths(kTen * row.ratePerStrength));
        row2("Shield", tenths(shield));
        if (knight || gladiator)
            note("Raises the multiplier of every knight skill; each skill's card shows its own sum.");
        if (knight) note("And Defense's guard: strength is its main stat.");
        if (wizard) note("A wizard's spells take nothing from it: it is for the gear that asks for it.");
        if (elf) note("Agility is her bow's main stat; strength adds half as much.");
    } else if (stat == 1) {
        const int agility = totals[1];
        const double shorter = 1.0 - 1.0 / (1.0 + double(std::max(0, agility)) / sim::kAgilityPerDoubling);
        if (elf) row2("Bow damage", band(kTen / 7.0, kTen / 4.0));
        if (elf) row2("Melee damage", band(kTen * row.minimumDamagePerStrengthAndAgility,
                                            kTen * row.maximumDamagePerStrengthAndAgility));
        row2("Attack speed", tenths(sim::attackSpeedStat(kin, 10, nullptr, nullptr)));
        if (wizard || gladiator) row2("Magic speed", tenths(sim::magicSpeedStat(kin, 10)));
        row2("Attack rate", tenths(kTen * row.ratePerAgility));
        row2("Defense", tenths(kTen * row.defensePerAgility * 0.5));
        row2("Defense rate", tenths(kTen * row.defenseRatePerAgility));
        row2("Shield", tenths(shield));
        row2("Cooldowns now", "-" + std::to_string(int(std::lround(shorter * 100.0))) + "%", tip::Tone::Yellow);
        note("Every cooldown is divided by 1 + agility / " + std::to_string(int(sim::kAgilityPerDoubling)) +
             ": " + std::to_string(int(sim::kAgilityPerDoubling)) +
             " agility halves them. Never shorter than the skill's own animation.");
        note(wizard ? "Magic speed casts spells faster; attack speed swings the staff faster."
             : gladiator ? "Attack speed speeds his swings and knight skills, magic speed his spells."
                         : "Attack speed speeds swings and every attack skill's animation.");
        if (elf) note("Her main stat, and Greater Defense's.");
    } else if (stat == 2) {
        row2("Life", tenths(kTen * row.healthPerVitality));
        row2("Shield", tenths(shield));
        if (elf) row2("Summon life", "+" + std::to_string(int(std::lround(kTen * sim::kSummonHealthPerVitality * 100.0))) + "%");
    } else {
        ten.energy = 10;
        const int mana = sim::maximumMana(kin, 0, ten) - sim::maximumMana(kin, 0, {});
        if (wizard || gladiator) row2("Wizardry", band(kTen / 9.0, kTen / 4.0));
        if (gladiator) row2("Damage", band(kTen * row.minimumDamagePerEnergy, kTen * row.maximumDamagePerEnergy));
        if (knight || gladiator) row2("Skill damage", "+" + std::to_string(sim::skillPercent(ten) - sim::skillPercent({})) + "%");
        if (elf) {
            ten.energy = 70;  // the two blessings step at every seventh and fifth point
            row2("Greater Damage", tenths(double(sim::mightOf(ten) - sim::mightOf({})) / 7.0));
            row2("Heal", tenths(double(sim::healOf(ten) - sim::healOf({})) / 7.0));
            row2("Summon life", "+" + std::to_string(int(std::lround(kTen * sim::kSummonHealthPerEnergy * 100.0))) + "%");
            row2("Summon damage", "+" + std::to_string(int(std::lround(kTen * sim::kSummonForcePerEnergy * 100.0))) + "%");
        }
        row2("Mana", tenths(mana));
        row2("Shield", tenths(shield));
        if (wizard) note("His main stat: every spell's band, and Soul Barrier's guard.");
        if (gladiator) note("His spells' band, and his swings and knight skills as well.");
        if (elf) note("Her summons and blessings. Greater Defense takes half of it.");
        note("And the rune strikes on your gear.");
    }
    if (stat != 2) note("Gear asks for it too.");
    sheet.sections.push_back(every);
    if (!notes.rows.empty()) sheet.sections.push_back(notes);
    return sheet;
}

}  // namespace

void Card::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

bool Card::covers(float x, float y) const {
    return now_.who != nullptr &&
           Box{x_, y_, panel::kWidth * panel::scale(), panel::kHeight * panel::scale()}.has(x, y);
}

void Card::update(float width, float height, const sim::Body* hero, const sim::Realm* realm,
                  const std::string& name, const Pointer& pointer, int* spend, int* count, bool* reset,
                  bool* close) {
    // The right-hand column, always.
    x_ = panel::columnX(width, 1);
    y_ = panel::panelY(height);

    now_ = Sheet{};
    if (hero) {
        const sim::Kin kin = hero->kin;
        const bool knight = kin == sim::Kin::DarkKnight, wizard = kin == sim::Kin::DarkWizard;
        const bool elf = kin == sim::Kin::FairyElf, gladiator = kin == sim::Kin::MagicGladiator;
        const sim::HeroPoints total = hero->totalPoints();
        const sim::Fighter& f = hero->stats;
        const auto& ex = hero->excel;

        // Both hands' bands together while a knight holds two weapons: what one blow rolls.
        const Line damage{"Damage", band(f.minimumDamage + f.offhandMinimumDamage,
                                         f.maximumDamage + f.offhandMaximumDamage)};
        const Line attackRate{"Attack rate", number((long long)f.attackRate)};
        const Line speed{wizard ? "Magic speed" : "Attack speed",
                         number(realm ? realm->attackSpeed() : 0)};
        const Line defense{"Defense", number(f.defense)};
        const Line defenseRate{"Defense rate", number((long long)f.defenseRate)};
        const Line life{"Life", pair(hero->health, hero->maxHealth)};
        const Line mana{"Mana", pair(hero->mana, hero->maxMana)};
        // Every class's shield, the HUD's word for SD: all four points, defence and level make
        // it (sim::maximumShield), so it stands with the pools rather than under one point.
        const Line shield{"Shield", pair(hero->sd, hero->maxSd)};
        const Line wizardry{"Wizardry", band((long long)f.wizardMinimum, (long long)f.wizardMaximum)};
        const Line skill{"Skill damage", std::to_string(sim::skillPercent(total)) + "%"};
        // The elf's three blessings, what her energy buys her and her party.
        const Line might{"Greater Damage", plus(sim::mightOf(total))};
        const Line ward{"Greater Defense", sim::absorbed(sim::wardShare(total))};
        const Line heal{"Heal", plus(sim::healOf(total))};

        // Under each attribute, what it buys -- MuMain's lines, in its order
        // (CNewUICharacterInfoWindow::RenderTableTexts): damage and attack rate; defence, speed
        // and defence rate; life; mana and the class's own. The elf's blessings are ours.
        now_.lines[0] = {damage, attackRate};
        now_.lines[1] = {defense, speed, defenseRate};
        now_.lines[2] = {life, shield};
        if (wizard) now_.lines[3] = {wizardry, mana};
        if (knight) now_.lines[3] = {skill, mana};
        if (gladiator) now_.lines[3] = {wizardry, skill, mana};
        if (elf) now_.lines[3] = {might, ward, heal, mana};

        // The three his class lives by, at the top whatever page is open. Life and mana show
        // their whole, the pool's top.
        const Line lifeTop{"Life", number(hero->maxHealth)};
        const Line manaTop{"Mana", number(hero->maxMana)};
        if (knight) { now_.keys[0] = damage; now_.keys[1] = skill; now_.keys[2] = lifeTop; }
        if (wizard) { now_.keys[0] = wizardry; now_.keys[1] = manaTop; now_.keys[2] = lifeTop; }
        if (elf) { now_.keys[0] = damage; now_.keys[1] = might; now_.keys[2] = defenseRate; }
        if (gladiator) { now_.keys[0] = damage; now_.keys[1] = wizardry; now_.keys[2] = lifeTop; }

        if (tab_ == 1) {
            // Combat: what he deals, what he turns aside, what he lasts by. A chance the gear
            // gives is shown only once something gives it.
            const auto chance = [](std::vector<Line>& to, const char* label, double share) {
                if (share > 0.0) to.push_back({label, percent(share)});
            };
            Group attack{"Attack", {damage, attackRate, speed}};
            if (wizard || gladiator) attack.lines.push_back(wizardry);
            if (knight || gladiator) attack.lines.push_back(skill);
            chance(attack.lines, "Critical chance", f.criticalChance);
            chance(attack.lines, "Excellent chance", f.excellentChance);
            chance(attack.lines, "Ignore defense", f.ignoreDefense);
            Group guard{"Guard", {defense, defenseRate}};
            chance(guard.lines, "Damage decrease", f.damageDecrease);
            chance(guard.lines, "Block chance", f.blockChance);
            Group endure{"Endurance", {life, shield, mana}};
            if (ex.lifeRegen > 0) endure.lines.push_back({"Life regeneration", plusPercent(ex.lifeRegen)});
            now_.groups.push_back(std::move(attack));
            if (elf) now_.groups.push_back({"Blessings", {might, ward, heal}});
            now_.groups.push_back(std::move(guard));
            now_.groups.push_back(std::move(endure));
        } else if (tab_ == 2) {
            // Bonuses: the jewellery's, every row shown, a nought dimmed, so a player learns
            // what there is to find.
            const auto row = [](const char* label, int v, bool percentSign, uint32_t ink) {
                Line line{label, v <= 0 ? (percentSign ? "0%" : "0") : (percentSign ? plusPercent(v) : plus(v)), v <= 0};
                line.ink = ink;
                return line;
            };
            // **Runes** (the user, 2026-10-24, the canvas's B): one row a kind of rune he wears --
            // a limited one's pips (game::runePool), its total where its line is a "+N%", else
            // how many; the stat runes a row a stat, their share and what it adds to his points.
            Group runes{"Runes", {}};
            {
                const sim::Satchel* bag = realm ? &realm->satchel() : nullptr;
                const auto pipsOf = [&](Line& line, sim::Power power) {
                    if (!bag) return;
                    const RunePool pool = runePool(*bag, power);
                    // One that only ever counts once has no room to show: no pips at all (the
                    // user, 2026-10-25: "if rune is usable as 1 socket only, dont show rings").
                    if (pool.most <= 1) return;
                    line.pips = pool.most;
                    for (const sim::Rarity rarity : pool.worn) {
                        line.stones.push_back(tip::colourOf(rarityTone(rarity)));
                    }
                };
                const sim::HeroPoints& share = hero->runeShare;
                const int shares[4] = {share.strength, share.agility, share.vitality, share.energy};
                // Named by the runes, not the stat (the user, 2026-10-24: "Strength is rune
                // name?"): Might, Grace, Vigor, Insight, in the strongest worn's ink.
                const char* const statWords[4] = {"Might", "Grace", "Vigor", "Insight"};
                const char* const statLong[4] = {"Strength", "Agility", "Vitality", "Energy"};
                const uint32_t statInks[4] = {kStrengthInk, kAgilityInk, kVitalityInk, kEnergyInk};
                const sim::Power statRunes[4] = {sim::Power::LesserMight, sim::Power::LesserGrace,
                                                 sim::Power::LesserVigor, sim::Power::LesserInsight};
                for (int k = 0; k < 4; ++k) {
                    if (shares[k] <= 0) continue;
                    Line line{statWords[k], plusPercent(shares[k]), false};
                    // What the share is of; the figure is the row's own, never twice (the user,
                    // 2026-10-25: "there is some duplicates with percentages").
                    line.note = statLong[k];
                    line.ink = statInks[k];
                    pipsOf(line, statRunes[k]);
                    if (!line.stones.empty()) line.labelInk = line.stones.front();
                    runes.lines.push_back(std::move(line));
                }
                // The rest, a row a power in the order worn; Bulwark's kin and the Volleys' each
                // show the kin's one shared pip.
                std::vector<uint8_t> seen;
                for (int slot = sim::kWeaponRight; bag && slot <= sim::kRingLeft; ++slot) {
                    const sim::Held& held = (*bag)[slot];
                    if (held.empty()) continue;
                    for (int at = 0; at < std::min<int>(held.sockets, sim::kMostSockets); ++at) {
                        const sim::PowerRow* power = sim::powerOf(held.powers[at]);
                        if (!power) continue;
                        const sim::HeroPoints its = sim::statShareOf(power->power);
                        if (its.strength || its.agility || its.vitality || its.energy) continue;
                        if (std::find(seen.begin(), seen.end(), held.powers[at]) != seen.end()) continue;
                        seen.push_back(held.powers[at]);
                        int count = 0;
                        for (int s2 = sim::kWeaponRight; s2 <= sim::kRingLeft; ++s2) {
                            const sim::Held& h2 = (*bag)[s2];
                            if (h2.empty()) continue;
                            for (int a2 = 0; a2 < std::min<int>(h2.sockets, sim::kMostSockets); ++a2) {
                                count += h2.powers[a2] == held.powers[at];
                            }
                        }
                        Line line{power->name, "", false};
                        line.labelInk = tip::colourOf(rarityTone(power->rarity));
                        pipsOf(line, power->power);
                        // "+10% critical chance" times those that count; otherwise how many.
                        const char* tells = power->tells ? power->tells : "";
                        const int counted = line.pips > 0 ? std::min(count, line.pips) : count;
                        int each = 0;
                        if (tells[0] == '+' && std::sscanf(tells + 1, "%d%%", &each) == 1 &&
                            std::strchr(tells, '%') != nullptr) {
                            line.value = plusPercent(each * counted);
                            // The total stands at the right: the effect under it without its
                            // "+10% ", "defense" and not "+10% defense each" beside a +30%.
                            const char* rest = std::strchr(tells, '%') + 1;
                            while (*rest == ' ') ++rest;
                            line.note = rest;
                            if (!line.note.empty()) line.note[0] = char(std::toupper((unsigned char)line.note[0]));
                        } else {
                            // One worn says nothing a count would: "x2" only once there are two.
                            if (count > 1) line.value = "x" + std::to_string(count);
                            line.note = tells;
                        }
                        line.ink = kHeading;
                        runes.lines.push_back(std::move(line));
                    }
                }
                if (runes.lines.empty()) runes.lines.push_back(Line{"No runes worn", "", true});
            }
            now_.groups.push_back(std::move(runes));
            now_.groups.push_back({"Resistances",
                                   {row("Ice", ex.iceResistance, false, kIceInk),
                                    row("Poison", ex.poisonResistance, false, kPoisonInk),
                                    row("Lightning", ex.lightningResistance, false, kLightningInk),
                                    row("Fire", ex.fireResistance, false, kFireInk),
                                    row("Wind", ex.windResistance, false, kWindInk)}});
            now_.groups.push_back({"Fortune",
                                   {row("Experience", ex.moreExperience, true, kExperienceInk),
                                    row("Zen", ex.moreZen, true, kZenInk),
                                    row("Item find", ex.itemFind, true, kFindInk)}});
            now_.groups.push_back({"On hit",
                                   {row("Life on hit", ex.lifeOnHit, false, kLifeOnHitInk),
                                    row("Critical damage", ex.criticalDamage, true, kCriticalInk)}});
        }

        now_.who = hero;
        now_.name = name;
        now_.second = hero->second;
        now_.width = width;
        now_.height = height;
        now_.top = y_;
        now_.level = hero->level;
        now_.points = hero->pointsInHand;
        // His rebirths (sim/rebirth.h) are the head's resets.
        now_.resets = hero->rebirths;
        now_.guild = guild_;
        now_.experience = hero->experience;
        // The points he spent, without the stat runes (the user, 2026-10-19: "dont show extra
        // agility on characters stats because of runes, its missleading. show that info on
        // bonuses"): what an item asks is met by these, and the runes' share is on Bonuses. What
        // each buys below is still reckoned with the runes in, as the realm reckons it.
        now_.stats[0] = hero->points.strength;
        now_.stats[1] = hero->points.agility;
        now_.stats[2] = hero->points.vitality;
        now_.stats[3] = hero->points.energy;

        // The pointer, against this frame's layout.
        const Box cross = panel::scaled(x_, y_, panel::frameClose());
        overClose_ = cross.has(pointer.x, pointer.y);
        const size_t counts[4] = {now_.lines[0].size(), now_.lines[1].size(), now_.lines[2].size(),
                                  now_.lines[3].size()};
        const int points = hero->pointsInHand;
        const float drop = dropFor(points);
        float tops[4];
        attrTops(counts, tops, drop);
        const bool spending = tab_ == 0 && points > 0;
        const int step = stepNow(step_, points);
        // What a reset would take back, and whether the bag has room for what it takes off.
        const sim::HeroPoints start = sim::startingPoints(kin);
        now_.returnable = (hero->points.strength - start.strength) + (hero->points.agility - start.agility) +
                          (hero->points.vitality - start.vitality) + (hero->points.energy - start.energy);
        now_.strips = realm && now_.returnable > 0 ? realm->resetStrips() : 0;
        if (now_.returnable <= 0) asking_ = false;
        int steps[3];
        const int parts = stepParts(points, steps);
        const auto under = [&](const Box& units) { return panel::scaled(x_, y_, units).has(pointer.x, pointer.y); };
        const auto controlUnder = [&]() {
            // The foot: the reset, or its question's two while it is up.
            if (asking_) {
                if (under(kNoBox)) return kNoId;
                if (under(kYesBox)) return kYesId;
            } else if (now_.returnable > 0 && under(kResetSquare)) {
                return kResetId;
            }
            // The switch, on every page while there are points.
            if (points > 0 && under(kStepBox)) {
                for (int i = 0; i < parts; ++i) {
                    const float w = kStepBox.w / float(parts);
                    if (under({kStepBox.x + w * float(i), kStepBox.y, w, kStepBox.h})) return kStepIds + i;
                }
            }
            if (spending) {
                for (int i = 0; i < 4; ++i)
                    if (under(plusFor(tops[i], step))) return i;
            }
            return -1;
        };
        const auto tabUnder = [&]() {
            for (int i = 0; i < kTabCount; ++i)
                if (panel::scaled(x_, y_, tabBox(i, drop)).has(pointer.x, pointer.y)) return i;
            return -1;
        };
        over_ = controlUnder();
        const int overTab = tabUnder();
        // A stat's well, off its plus: its card.
        if (tab_ == 0 && (over_ < 0 || over_ >= 4)) {
            for (int i = 0; i < 4; ++i)
                if (under({kWellX, tops[i], kWellW, kAttrTall})) now_.overStat = i;
        }
        now_.totals[0] = total.strength;
        now_.totals[1] = total.agility;
        now_.totals[2] = total.vitality;
        now_.totals[3] = total.energy;
        if (pointer.pressed) {
            if (overClose_) closing_ = true;
            pushed_ = over_;
            pressTab_ = overTab;
        }
        if (pointer.released) {
            // Only if the pointer is still on it, which is how a press is taken back.
            if (closing_ && overClose_ && close) *close = true;
            if (pushed_ >= 0 && pushed_ == over_) {
                if (pushed_ < 4) {
                    if (spend) *spend = pushed_;
                    if (count) *count = gainOf(step, points);
                } else if (pushed_ >= kStepIds && pushed_ < kStepIds + parts) {
                    step_ = steps[pushed_ - kStepIds];
                } else if (pushed_ == kResetId) {
                    asking_ = true;
                } else if (pushed_ == kNoId) {
                    asking_ = false;
                } else if (pushed_ == kYesId && now_.strips >= 0) {
                    // A bag without room says so on the page and lights no Yes; the realm's
                    // refusal, should the bag change first, is the desk's to say.
                    if (reset) *reset = true;
                    asking_ = false;
                }
            }
            if (pressTab_ >= 0 && pressTab_ == overTab && pressTab_ != tab_) tab_ = pressTab_;
            closing_ = false;
            pushed_ = -1;
            pressTab_ = -1;
        }
        now_.step = stepNow(step_, points);
        now_.asking = asking_;

        // Under a plus: what the press would make of each line under that stat, the gain before
        // it and the value in gold (the canvas's hover). The realm reckons both sides the same
        // way, so the difference lands on what the line prints whatever gear adds on top.
        if (spending && realm && over_ >= 0 && over_ < 4) {
            const sim::Realm::SpendPreview p = realm->previewSpend(over_, gainOf(now_.step, points));
            const sim::Fighter& a = p.before;
            const sim::Fighter& b = p.after;
            const auto one = [](long long d) { return (d >= 0 ? "+" : "") + number(d); };
            const auto lightBand = [&](Line& line, long long lo, long long hi, long long dLo, long long dHi) {
                if (dLo == 0 && dHi == 0) return;
                line.gain = one(dLo) + " ~ " + one(dHi);
                line.value = band(lo + dLo, hi + dHi);
                line.lit = true;
            };
            const auto lightOne = [&](Line& line, long long now, long long d, const char* unit) {
                if (d == 0) return;
                line.gain = one(d) + unit;
                line.value = number(now + d) + unit;
                line.lit = true;
            };
            const auto lightPool = [&](Line& line, long long now, long long most, long long d) {
                if (d == 0) return;
                line.gain = one(d);
                line.value = pair(now + d, most + d);
                line.lit = true;
            };
            for (Line& line : now_.lines[over_]) {
                const std::string& l = line.label;
                if (l == "Damage") {
                    lightBand(line, f.minimumDamage + f.offhandMinimumDamage, f.maximumDamage + f.offhandMaximumDamage,
                              (b.minimumDamage + b.offhandMinimumDamage) - (a.minimumDamage + a.offhandMinimumDamage),
                              (b.maximumDamage + b.offhandMaximumDamage) - (a.maximumDamage + a.offhandMaximumDamage));
                } else if (l == "Wizardry") {
                    lightBand(line, (long long)f.wizardMinimum, (long long)f.wizardMaximum,
                              (long long)b.wizardMinimum - (long long)a.wizardMinimum,
                              (long long)b.wizardMaximum - (long long)a.wizardMaximum);
                } else if (l == "Attack rate") {
                    lightOne(line, (long long)f.attackRate, (long long)b.attackRate - (long long)a.attackRate, "");
                } else if (l == "Defense") {
                    lightOne(line, f.defense, b.defense - a.defense, "");
                } else if (l == "Defense rate") {
                    lightOne(line, (long long)f.defenseRate, (long long)b.defenseRate - (long long)a.defenseRate, "");
                } else if (l == "Life") {
                    lightPool(line, hero->health, hero->maxHealth, p.lifeAfter - p.lifeBefore);
                } else if (l == "Mana") {
                    lightPool(line, hero->mana, hero->maxMana, p.manaAfter - p.manaBefore);
                } else if (l == "Shield") {
                    lightPool(line, hero->sd, hero->maxSd, p.sdAfter - p.sdBefore);
                } else if (l == "Skill damage") {
                    lightOne(line, sim::skillPercent(p.totalsBefore),
                             sim::skillPercent(p.totalsAfter) - sim::skillPercent(p.totalsBefore), "%");
                } else if (l == "Greater Damage") {
                    lightOne(line, sim::mightOf(p.totalsBefore),
                             sim::mightOf(p.totalsAfter) - sim::mightOf(p.totalsBefore), "");
                    if (line.lit) line.value = "+" + line.value;
                } else if (l == "Heal") {
                    lightOne(line, sim::healOf(p.totalsBefore),
                             sim::healOf(p.totalsAfter) - sim::healOf(p.totalsBefore), "");
                    if (line.lit) line.value = "+" + line.value;
                } else if (l == "Greater Defense") {
                    const std::string after = sim::absorbed(sim::wardShare(p.totalsAfter));
                    if (after != line.value) {
                        line.value = after;
                        line.lit = true;
                    }
                }
            }
        }
        now_.tab = tab_;
        now_.overTab = overTab;
        now_.pressTab = pressTab_;
        now_.pushed = pushed_;
        now_.over = over_;
        now_.closing = closing_;
        now_.overClose = overClose_;
    }
    // A tab changed this frame draws its page on the next, which has built it.
    if (now_ == drawn_ && rebuilds_ > 0) return;
    drawn_ = now_;
    rebuild();
}

// A rune row's pips on the Bonuses page (Line::pips), as the item card draws them: a solid
// stone for each worn in its rarity, an outlined ring for room left, past the limit dim and
// struck through.
void Card::pipsAt(const Line& line, float px, float cy, float k) {
    const float r = kPipRadius * k, line1 = std::max(1.0f, 0.5f * k);
    const auto disc = [&](float cx, float radius, uint32_t colour) {
        constexpr int kSides = 16;
        float xy[kSides * 2];
        for (int i = 0; i < kSides; ++i) {
            const float a = 6.2831853f * float(i) / float(kSides);
            xy[i * 2] = cx + radius * std::cos(a);
            xy[i * 2 + 1] = cy + radius * std::sin(a);
        }
        canvas_.polygon(nullptr, xy, nullptr, kSides, colour);
    };
    const int slots = std::max(line.pips, int(line.stones.size()));
    for (int i = 0; i < slots; ++i) {
        const float cx = px + r + kPipPitch * k * float(i);
        const bool worn = i < int(line.stones.size());
        if (i >= line.pips) {
            disc(cx, r, kDim);
            canvas_.rect({cx - r * 1.4f, cy - line1 * 0.5f, r * 2.8f, line1}, line.stones[size_t(i)]);
        } else if (worn) {
            disc(cx, r, line.stones[size_t(i)]);
        } else {
            disc(cx, r, kDim);
            disc(cx, r - line1 * 1.2f, kPipHole);
        }
    }
}

void Card::rebuild() {
    ++rebuilds_;
    canvas_.clear();
    tip_.clear();
    if (!now_.who || !arts_) return;
    panel::Arts& arts = *arts_;
    const float x = x_, y = y_, k = panel::scale();
    const gfx::Face& face = canvas_.face();
    const float u = tip::unit();

    panel::frame(canvas_, arts, x, y, "Character");
    // **His name, small under the title, his guild in brackets after it, then his level and
    // resets** -- the user, 2026-10-10: 'show chatacyer title how it was and small character name
    // [guild] under the title, same as Inventory and gear score', then 'move lvl,resets also where
    // is character name [guild]'. The bag's gear score line (bag.cpp): figures and the name in
    // bone at its figure's size, words in ash capitals at its words', all centred together on one
    // baseline under the title's capitals. No guild, no brackets; no name (--fresh), no name.
    {
        const float u = tip::unit();
        const float size = 11.0f * u, small = 9.0f * u, gap = 4.0f * u, wide = 10.0f * u;
        struct Bit {
            std::string text;
            bool caps;
            float before;  // the air in front of it
        };
        std::vector<Bit> bits;
        if (!now_.name.empty()) bits.push_back({now_.name, false, 0.0f});
        if (!now_.guild.empty()) bits.push_back({"[" + now_.guild + "]", true, gap});
        bits.push_back({"LEVEL", true, bits.empty() ? 0.0f : wide});
        bits.push_back({std::to_string(now_.level), false, gap});
        bits.push_back({"RESETS", true, wide});
        bits.push_back({std::to_string(now_.resets), false, gap});
        float total = 0.0f;
        for (const Bit& bit : bits)
            total += bit.before + (bit.caps ? controls::capsWidth(small, bit.text)
                                             : controls::labelWidth(size, bit.text));
        float at = std::round(x + (panel::kWidth * k - total) * 0.5f);
        const float baseline = std::round(y + panel::kHeadBand * k * 0.86f);
        for (const Bit& bit : bits) {
            at += bit.before;
            if (bit.caps) {
                controls::caps(canvas_, at, baseline, small, style::kAshInk, bit.text);
                at += controls::capsWidth(small, bit.text);
            } else {
                at += controls::label(canvas_, at, baseline, size, style::kBoneHi, bit.text);
            }
        }
    }

    // Type set from a top-left in units, its ascent below it; and the same ranged right.
    const auto at = [&](float uy, float size) { return y + uy * k + face.ascent(size * k); };
    const auto left = [&](float ux, float uy, float size, uint32_t ink, const std::string& text) {
        controls::label(canvas_, x + ux * k, at(uy, size), size * k, ink, text);
    };
    const auto right = [&](float ux, float uy, float size, uint32_t ink, const std::string& text) {
        controls::ranged(canvas_, x + ux * k, at(uy, size), size * k, ink, text);
    };
    const auto smallCaps = [&](float ux, float baseline, uint32_t ink, const std::string& text) {
        controls::caps(canvas_, x + ux * k, baseline, kSmallCaps * k, ink, text, 0.04f);
    };
    // A hairline down a strip, between two of its cells.
    const auto divide = [&](float ux, float top, float tall) {
        canvas_.rect(panel::scaled(x, y, {ux, top + 2.0f, 0.5f, tall - 4.0f}), kDivider);
    };

    // ---- who he is: the class, his unspent points beside it, the guild under -------------------
    // A step down from the name over it: the class is what he is, the name who.
    controls::caps(canvas_, x + kLeft * k, at(kIdTop, kClassSize), kClassSize * k, kPlain,
                   sim::className(int(now_.who->kin), now_.second), 0.04f);
    if (now_.points > 0) {
        // A red tag, the one thing in the head that asks for something, gone at none.
        const std::string tag = std::to_string(now_.points) + " points";
        const float size = kLineSize * k;
        // Its words centred on it both ways (the user, 2026-10-20: "not perfectly align on red
        // background"): the tracking after the last letter is not part of the word.
        const float w = controls::capsWidth(size, tag, 0.04f) - size * 0.04f;
        const float pad = 3.0f * k;
        const float tagRight = std::round(x + kRight * k);
        const Box box{std::round(tagRight - w - pad * 2.0f), std::round(y + (kIdTop - 1.0f) * k),
                      std::round(w + pad * 2.0f), std::round(11.0f * k)};
        canvas_.rect(box, kSpendWash);
        controls::caps(canvas_, box.x + std::round((box.w - w) * 0.5f), controls::capsMiddle(box.y, box.h, size),
                       size, kSpendable, tag, 0.04f);
    }

    // ---- the three his class lives by -----------------------------------------------------------
    const Box keys{kWellX, kKeysTop, kWellW, kKeysTall};
    panel::cell(canvas_, x, y, keys, sheet::Cell::Rest);
    const float keyW = kWellW / 3.0f;
    for (int i = 0; i < 3; ++i) {
        const float cellX = kWellX + keyW * float(i);
        if (i > 0) divide(cellX, kKeysTop, kKeysTall);
        const float cx = cellX + 7.0f;
        // A long name ("Greater Damage") and a wide band (a wizard's late wizardry) each step
        // down until they fit their cell.
        float caps = kSmallCaps;
        while (caps > 5.0f &&
               controls::capsWidth(caps * k, now_.keys[i].label, 0.04f) > (keyW - 9.0f) * k)
            caps -= 0.25f;
        controls::caps(canvas_, x + cx * k, at(kKeysTop + 5.0f, kSmallCaps), caps * k, kQuiet,
                       now_.keys[i].label, 0.04f);
        float size = kKeySize;
        while (size > 6.5f && controls::labelWidth(size * k, now_.keys[i].value) > (keyW - 10.0f) * k)
            size -= 0.5f;
        left(cx, kKeysTop + 15.0f, size, kHeading, now_.keys[i].value);
    }

    const float drop = dropFor(now_.points);
    const auto stateOf = [&](int id, bool off) {
        controls::State state;
        state.lift = now_.over == id ? 1.0f : 0.0f;
        state.held = now_.pushed == id;
        state.off = off;
        return state;
    };

    // ---- what a press adds, under the three, while there are points ------------------------------
    if (now_.points > 0) {
        const Box label{kWellX, kStepBox.y, kStepLabelW, kCtlTall};
        smallCaps(kWellX + 2.0f, panel::centredBaseline(face, panel::scaled(x, y, label), kSmallCaps * k), kQuiet,
                  "Each press adds");
        int steps[3];
        const int parts = stepParts(now_.points, steps);
        const char* words[3] = {};
        int on = -1;
        for (int i = 0; i < parts; ++i) {
            words[i] = steps[i] == 1 ? "1" : steps[i] == 10 ? "10" : "All";
            if (steps[i] == now_.step) on = i;
        }
        const int over = now_.over >= kStepIds && now_.over < kStepIds + parts ? now_.over - kStepIds : -1;
        controls::choice(canvas_, panel::scaled(x, y, kStepBox), words, parts, on, over, u, kTabSize * k);
    }

    // ---- the tabs -------------------------------------------------------------------------------
    // Underline tabs: the words on the sheet over one dark floor, the open one golden with a
    // golden bar on the floor (the user's pick of three, 2026-10-10).
    canvas_.rect(panel::scaled(x, y, {kWellX, kTabsTop + drop + kTabsTall - 0.5f, kWellW, 0.5f}),
                 style::kIronDk);
    for (int i = 0; i < kTabCount; ++i) {
        const Box box = tabBox(i, drop);
        const bool chosen = i == now_.tab;
        const uint32_t ink = chosen ? kChosen : (i == now_.overTab ? style::kBone : kQuiet);
        const float base = panel::centredBaseline(face, panel::scaled(x, y, box), kTabSize * k);
        const float w = controls::capsWidth(kTabSize * k, kTabWords[i], 0.0f);
        controls::caps(canvas_, x + box.midX() * k - w * 0.5f, base, kTabSize * k, ink,
                       kTabWords[i], 0.0f);
        if (chosen) {
            canvas_.rect(panel::scaled(x, y, {box.x + 2.0f, box.y + box.h - 1.0f, box.w - 4.0f, 1.0f}),
                         kChosenBar);
        }
    }

    // ---- the page -------------------------------------------------------------------------------
    if (now_.tab == 0) {
        const char* names[4] = {"Strength", "Agility", "Vitality", "Energy"};
        const size_t counts[4] = {now_.lines[0].size(), now_.lines[1].size(), now_.lines[2].size(),
                                  now_.lines[3].size()};
        float tops[4];
        const float lineStep = attrTops(counts, tops, drop);
        const int gain = gainOf(now_.step, now_.points);
        for (int i = 0; i < 4; ++i) {
            const Box well{kWellX, tops[i], kWellW, kAttrTall};
            // Under the pointer the plus says what it would make of the stat: its well's rim lit,
            // the figure in gold with the gain before it, and the same on each line under it.
            const bool hot = now_.points > 0 && now_.over == i;
            panel::cell(canvas_, x, y, well, hot ? sheet::Cell::Over : sheet::Cell::Rest);
            const float figure = kFigureSize * k;
            const float base = panel::centredBaseline(face, panel::scaled(x, y, well), figure);
            controls::label(canvas_, x + kLeft * k, base, kNameSize * k, kPlain, names[i]);
            const Box plusUnits = plusFor(tops[i], now_.step);
            const float figureRight = now_.points > 0 ? plusUnits.x - 4.0f : kRight;
            const std::string value = panel::commas(now_.stats[i] + (hot ? gain : 0));
            controls::ranged(canvas_, x + figureRight * k, base, figure, hot ? kChosen : kHeading, value);
            if (hot) {
                const float valueW = controls::labelWidth(figure, value);
                controls::ranged(canvas_, x + figureRight * k - valueW - 4.0f * k, base, kLineSize * k,
                                 kChosenBar, "+" + panel::commas(gain));
            }
            for (size_t l = 0; l < now_.lines[i].size(); ++l) {
                const Line& line = now_.lines[i][l];
                const float uy = tops[i] + kAttrTall + kLineTop + lineStep * float(l);
                left(kLeft, uy, kLineSize, kQuiet, line.label);
                right(kRight, uy, kLineSize, line.lit ? kChosen : kPlain, line.value);
                if (!line.gain.empty()) {
                    const float valueW = controls::labelWidth(kLineSize * k, line.value) / k;
                    right(kRight - valueW - 6.0f, uy, kLineSize, kChosenBar, line.gain);
                }
            }
            // The plus only where there is something to spend: MU hides it rather than greying
            // it. A red square, the one thing on the card that asks to be pressed; at 10 and All
            // a red button that says what a press adds ("+10", "All").
            if (now_.points <= 0) continue;
            const Box plus = panel::scaled(x, y, plusUnits);
            if (now_.step == 1) {
                const float side = std::round(std::min(plus.w, plus.h));
                controls::square(canvas_,
                                 {std::round(plus.midX() - side * 0.5f), std::round(plus.midY() - side * 0.5f),
                                  side, side},
                                 controls::Glyph::Plus, stateOf(i, false), u, true);
            } else {
                controls::button(canvas_, plus, now_.step == 0 ? "All" : "+" + std::to_string(gain),
                                 controls::Kind::Primary, stateOf(i, false), u, kTabSize * k);
            }
        }
    } else {
        // A group with nothing in it yet is its heading and a quiet "none" on one line, no well,
        // so the room goes to what he has. A rune is a block: its name, pips and total on a
        // line, what it does under it, wrapped at its words.
        const float noteRoom = (kRowRight - kRowLeft) * k;
        const auto wrapped = [&](const std::string& note) {
            std::vector<std::string> out;
            std::string at;
            size_t from = 0;
            while (from < note.size()) {
                size_t to = note.find(' ', from);
                if (to == std::string::npos) to = note.size();
                const std::string word = note.substr(from, to - from);
                const std::string tried = at.empty() ? word : at + " " + word;
                if (!at.empty() && controls::labelWidth(kNoteSize * k, tried) > noteRoom) {
                    out.push_back(at);
                    at = word;
                } else {
                    at = tried;
                }
                from = to + 1;
            }
            if (!at.empty()) out.push_back(at);
            return out;
        };
        const auto empty = [](const Group& group) {
            return std::all_of(group.lines.begin(), group.lines.end(), [](const Line& line) { return line.dim; });
        };
        // Even room round every row's words, by the face's own measure: as much from the rule
        // above to the top of the name's capitals as from the last line's baseline to the rule
        // below. Capitals stand kCapShare of the ascent.
        const float nameCap = face.ascent(kRowSize * k) * kCapShare / k;
        const float noteCap = face.ascent(kNoteSize * k) * kCapShare / k;
        const auto words = [&](const Line& line) {
            if (line.note.empty()) return nameCap;
            return nameCap + kNoteGap + noteCap + kNoteStep * float(wrapped(line.note).size() - 1);
        };
        // Only a page still too long closes up, and only the room round the words.
        float text = 0.0f, rows = 0.0f;
        float heads = 0.0f;
        for (const Group& group : now_.groups) {
            heads += kGroupHead + kGroupGap;
            if (empty(group)) continue;
            for (const Line& line : group.lines) {
                text += words(line);
                rows += 1.0f;
            }
        }
        const float room = kBodyBottom - kBodyTop - drop - heads - text;
        const float pad = rows > 0.0f ? std::clamp(room / (2.0f * rows), kRowPadLeast, kRowPad) : kRowPad;
        const float stroke = std::max(1.0f, std::round(0.5f * k));
        float uy = kBodyTop + drop;
        for (const Group& group : now_.groups) {
            smallCaps(kWellX + 2.0f, at(uy + 1.0f, kSmallCaps), kDim, group.name);
            if (empty(group)) {
                const float base = at(uy + 1.0f, kSmallCaps);
                controls::ranged(canvas_, x + kRowRight * k, base, kSmallCaps * k, kDim, "none");
                uy += kGroupHead + kGroupGap;
                continue;
            }
            uy += kGroupHead;
            float tall = 0.0f;
            for (const Line& line : group.lines) tall += words(line) + 2.0f * pad;
            panel::cell(canvas_, x, y, {kWellX, uy, kWellW, tall}, sheet::Cell::Rest);
            // The pips stand in one column, right against the group's widest figure, so every
            // row's pips end where the next one's do, whatever its name.
            float widest = 0.0f;
            for (const Line& line : group.lines) widest = std::max(widest, controls::labelWidth(kRowSize * k, line.value));
            const float pipsEnd = x + kRowRight * k - widest - (widest > 0.0f ? kPipPitch * k : 0.0f);
            float ly = uy;
            for (size_t l = 0; l < group.lines.size(); ++l) {
                const Line& line = group.lines[l];
                const std::vector<std::string> said = line.note.empty() ? std::vector<std::string>{} : wrapped(line.note);
                if (l > 0) {
                    canvas_.rect({std::round(x + (kWellX + 1.0f) * k), std::round(y + ly * k), (kWellW - 2.0f) * k, stroke},
                                 kRowStroke);
                }
                const float base = std::round(y + (ly + pad + nameCap) * k);
                controls::label(canvas_, x + kRowLeft * k, base, kRowSize * k,
                                line.dim ? kDim : line.labelInk ? line.labelInk : kPlain, line.label);
                const int slots = std::max(line.pips, int(line.stones.size()));
                if (slots > 0) {
                    const float r = kPipRadius * k;
                    pipsAt(line, pipsEnd - 2.0f * r - kPipPitch * k * float(slots - 1), base - nameCap * k * 0.5f, k);
                }
                controls::ranged(canvas_, x + kRowRight * k, base, kRowSize * k, line.dim ? kDim : line.ink ? line.ink : kHeading,
                                 line.value);
                for (size_t n = 0; n < said.size(); ++n) {
                    // What the rune does, quiet under its name.
                    const float under = ly + pad + nameCap + kNoteGap + noteCap + kNoteStep * float(n);
                    controls::label(canvas_, x + kRowLeft * k, std::round(y + under * k), kNoteSize * k, kQuiet, said[n]);
                }
                ly += words(line) + 2.0f * pad;
            }
            uy += tall + kGroupGap;
        }
    }

    // ---- the foot: how far through the level ----------------------------------------------------
    controls::foot(canvas_, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, panel::kHeight}),
                   y + kFootRule * k, u, false);
    const uint64_t from = sim::neededExperience(now_.level);
    const uint64_t to = sim::neededExperience(now_.level + 1);
    const float share =
        to > from ? float(double(now_.experience > from ? now_.experience - from : 0) /
                          double(to - from))
                  : 1.0f;
    if (now_.asking) {
        // The question is the foot while it is up: one void well with a blood edge at its left,
        // "Take back N points?" with the count in the spend's red, No and Reset at its right; under
        // it, where the level's progress stands, what a reset does -- or, in red, why it cannot.
        const Box ask = panel::scaled(x, y, kAskBox);
        panel::cell(canvas_, x, y, kAskBox, sheet::Cell::Rest);
        canvas_.rect({ask.x, ask.y + std::round(2.0f * k), std::max(1.0f, std::round(k)), ask.h - std::round(4.0f * k)},
                     style::kBlood);
        const float size = kNameSize * k;
        const float base = panel::centredBaseline(face, ask, size);
        // A space's width, which a word's own trailing space does not carry.
        const float space = controls::labelWidth(size, "a a") - controls::labelWidth(size, "aa");
        float at = x + (kWellX + 7.0f) * k;
        at += controls::label(canvas_, at, base, size, kPlain, "Take back") + space;
        at += controls::label(canvas_, at, base, size, kSpendable, panel::commas(now_.returnable)) + space;
        controls::label(canvas_, at, base, size, kPlain, "points?");
        controls::button(canvas_, panel::scaled(x, y, kNoBox), "No", controls::Kind::Secondary, stateOf(kNoId, false),
                         u, kTabSize * k);
        controls::button(canvas_, panel::scaled(x, y, kYesBox), "Reset", controls::Kind::Primary,
                         stateOf(kYesId, now_.strips < 0), u, kTabSize * k);
        std::string note = "Every stat returns to the class's start";
        uint32_t ink = kQuiet;
        if (now_.strips < 0) {
            note = "No room in your bag for the gear it takes off";
            ink = kSpendable;
        } else if (now_.strips > 0) {
            note = std::to_string(now_.strips) +
                   (now_.strips == 1 ? " worn item goes to your bag" : " worn items go to your bag");
        }
        const Box noteRow = panel::scaled(x, y, {kWellX, kAskBox.bottom(), kWellW, kBar.bottom() - kAskBox.bottom()});
        const float noteSize = kLineSize * k;
        controls::label(canvas_, std::round(noteRow.midX() - controls::labelWidth(noteSize, note) * 0.5f),
                        panel::centredBaseline(face, noteRow, noteSize), noteSize, ink, note);
    } else {
        const float kicker = kLineSize * k;
        controls::caps(canvas_, x + kLeft * k, y + kExpTop * k + face.ascent(kicker), kicker, kQuiet,
                       "Experience", 0.04f);
        controls::ranged(canvas_, x + kFootRight * k, y + kExpTop * k + face.ascent(kicker), kicker, kPlain,
                         number((long long)now_.experience) + " / " + number((long long)to) + " · " +
                             std::to_string(int(share * 100.0f)) + "%");
        controls::meter(canvas_, panel::scaled(x, y, kBar), std::clamp(share, 0.0f, 1.0f),
                        gfx::rgba(0.910f, 0.863f, 0.773f), style::kIron, u);
        // The reset, off with nothing placed.
        controls::State state = stateOf(kResetId, now_.returnable <= 0);
        controls::button(canvas_, panel::scaled(x, y, kResetSquare), "Reset points", controls::Glyph::Undo,
                         controls::Kind::Danger, state, u, kTabSize * k);
    }

    panel::close(canvas_, x, y, now_.overClose, now_.closing);

    if (now_.tab == 0 && now_.overStat >= 0) {
        const size_t counts[4] = {now_.lines[0].size(), now_.lines[1].size(), now_.lines[2].size(),
                                  now_.lines[3].size()};
        float tops[4];
        attrTops(counts, tops, drop);
        const tip::Sheet sheet = statSheet(now_.overStat, now_.who->kin, now_.totals);
        tip::draw(tip_, sheet, panel::scaled(x, y, {kWellX, tops[now_.overStat], kWellW, kAttrTall}),
                  now_.width, now_.height);
    }
}

}  // namespace mu::game
