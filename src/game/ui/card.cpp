#include "game/ui/card.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/ui/controls.h"
#include "game/ui/sheet.h"
#include "game/ui/style.h"
#include "sim/quests.h"
#include "sim/rules.h"
#include "sim/skills.h"

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
constexpr float kAttrTall = 19.0f, kLineStep = 10.5f, kLineTop = 3.0f, kAttrGap = 8.0f;
// The plus, inside the well's right end. Where a figure ends while it stands there.
constexpr float kPlusSide = 13.0f;
constexpr float kFigureRight = panel::kWellRight - kPlusSide - 7.0f;
// A Combat or Bonuses group: its heading, its rows, the air after it.
constexpr float kGroupHead = 10.0f, kRowTall = 13.0f, kGroupGap = 6.0f;

// The foot: the level's progress, at the panel's shared rule.
constexpr float kFootRule = panel::kFootRule;
constexpr float kFootTop = panel::kFootTop;
// **Reset points in the foot** (the user, 2026-10-20: "reset poont has to be in footer actualy"):
// a square at the foot's right, the bag's hammer's size and place, the turning arrow on it; the
// level's progress beside it, its bar and its figures ending short of it.
constexpr Box kResetSquare{panel::kWellRight - 24.0f, kFootTop + 1.0f, 24.0f, 24.0f};
constexpr float kFootRight = kResetSquare.x - 6.0f;
constexpr Box kBar{panel::kGridX, kFootTop + 12.0f, kFootRight - panel::kGridX, 4.0f};

// Type, in units. A figure is a size above its label, so the eye runs down the numbers.
constexpr float kClassSize = 9.0f, kNameSize = 8.5f, kFigureSize = 10.0f, kLineSize = 7.5f,
                kSmallCaps = 6.5f, kKeySize = 9.5f, kTabSize = 7.5f,
                kRowSize = 7.5f;

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

Box tabBox(int i) {
    const float w = kWellW / float(kTabCount);
    return {kWellX + w * float(i), kTabsTop, w, kTabsTall};
}

// **The Attributes page's foot** (the user's canvas C, 2026-10-20: "lets implement C · Amount
// toggle"): "Each press adds" and its 1 | 10 | All over Reset points, both at the page's floor.
// The canvas's measures at half its pixels: two controls 16 tall, 5 apart. The question a reset
// asks is one well over both rows -- its words, then No and Reset at its right -- with a line
// under it on what a reset does.
constexpr float kCtlTall = 16.0f, kCtlGap = 5.0f;
constexpr float kResetTop = kBodyBottom - kCtlTall;
constexpr float kStepTop = kResetTop - kCtlGap - kCtlTall;
// Where the attributes must end, with air over the switch.
constexpr float kAttrBottom = kStepTop - 6.0f;
constexpr float kStepLabelW = 64.0f;
constexpr float kAskButtonTall = 15.0f, kAskPad = 4.5f;
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
// before it carries. Returns the step between lines, which closes up (and the gaps with it) for
// a class whose lines would run into the foot -- the elf's four under Energy.
float attrTops(const size_t counts[4], float tops[4]) {
    size_t lines = 0;
    for (int i = 0; i < 4; ++i) lines += counts[i];
    float step = kLineStep, gap = kAttrGap;
    const float fixed = 4.0f * (kAttrTall + kLineTop);
    if (lines > 0 && kBodyTop + fixed + 3.0f * gap + step * float(lines) > kAttrBottom) {
        gap = 5.0f;
        step = std::min(kLineStep, (kAttrBottom - kBodyTop - fixed - 3.0f * gap) / float(lines));
    }
    float y = kBodyTop;
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
// **The foot's boxes**, from where the attributes end (the user, 2026-10-20: "each press add
// container has to be closer to stats points container"): the switch 7 under the last line under
// Energy, never lower than the page's floor allows.
struct Foot {
    Box step, reset, ask, yes, no;
};
Foot footFor(const size_t counts[4]) {
    float tops[4];
    const float lineStep = attrTops(counts, tops);
    // The last line's capitals end about its size below its top.
    const float lastLine = tops[3] + kAttrTall + kLineTop + lineStep * float(std::max<size_t>(1, counts[3]) - 1) + kLineSize;
    const float top = std::min(kStepTop, std::round(lastLine + 7.0f));
    Foot f;
    f.step = {kWellX + kStepLabelW, top, kWellW - kStepLabelW, kCtlTall};
    // The reset at the page's floor (the user, 2026-10-20: "reset point has to be closer to
    // bottom"), and its question there too, its line under it on the floor.
    f.reset = {kWellX, kResetTop, kWellW, kCtlTall};
    f.ask = {kWellX, kBodyBottom - 12.0f - 24.0f, kWellW, 24.0f};
    f.yes = {kWellX + kWellW - kAskPad - 36.0f, f.ask.y + (24.0f - kAskButtonTall) * 0.5f, 36.0f, kAskButtonTall};
    f.no = {f.yes.x - 3.0f - 28.0f, f.yes.y, 28.0f, kAskButtonTall};
    return f;
}

std::string number(long long v) { return panel::commas(v); }
std::string band(long long lo, long long hi) { return number(lo) + " ~ " + number(hi); }
std::string pair(long long now, long long most) { return number(now) + " / " + number(most); }
std::string percent(double share) { return std::to_string(int(std::lround(share * 100.0))) + "%"; }
std::string plus(int v) { return "+" + std::to_string(v); }
std::string plusPercent(int v) { return "+" + std::to_string(v) + "%"; }

}  // namespace

void Card::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
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
            const auto row = [](const char* label, int v, bool percentSign) {
                return Line{label, v <= 0 ? (percentSign ? "0%" : "0") : (percentSign ? plusPercent(v) : plus(v)), v <= 0};
            };
            // The stat runes worn (Body::runeShare): the share, and what it adds to his points.
            const auto rune = [](const char* label, int share, int added) {
                if (share <= 0) return Line{label, "0%", true};
                return Line{label, plusPercent(share) + " (" + plus(added) + ")", false};
            };
            const sim::HeroPoints& share = hero->runeShare;
            now_.groups.push_back({"Stat runes",
                                   {rune("Strength", share.strength, total.strength - hero->points.strength),
                                    rune("Agility", share.agility, total.agility - hero->points.agility),
                                    rune("Vitality", share.vitality, total.vitality - hero->points.vitality),
                                    rune("Energy", share.energy, total.energy - hero->points.energy)}});
            now_.groups.push_back({"Resistances",
                                   {row("Ice", ex.iceResistance, false),
                                    row("Poison", ex.poisonResistance, false),
                                    row("Lightning", ex.lightningResistance, false),
                                    row("Fire", ex.fireResistance, false),
                                    row("Wind", ex.windResistance, false)}});
            now_.groups.push_back({"Fortune",
                                   {row("Experience", ex.moreExperience, true),
                                    row("Zen", ex.moreZen, true),
                                    row("Item find", ex.itemFind, true)}});
            now_.groups.push_back({"On hit",
                                   {row("Life on hit", ex.lifeOnHit, false),
                                    row("Critical damage", ex.criticalDamage, true)}});
        }

        now_.who = hero;
        now_.name = name;
        now_.second = hero->second;
        now_.width = width;
        now_.height = height;
        now_.top = y_;
        now_.level = hero->level;
        now_.points = hero->pointsInHand;
        // No resets and no guilds in the realm yet: the head says 0 and shows no
        // brackets until there are, and then these two read them.
        now_.resets = 0;
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
        float tops[4];
        attrTops(counts, tops);
        const Foot foot = footFor(counts);
        const int points = hero->pointsInHand;
        const bool spending = tab_ == 0 && points > 0;
        const int step = stepNow(step_, points);
        // What a reset would take back, and whether the bag has room for what it takes off.
        const sim::HeroPoints start = sim::startingPoints(kin);
        now_.returnable = (hero->points.strength - start.strength) + (hero->points.agility - start.agility) +
                          (hero->points.vitality - start.vitality) + (hero->points.energy - start.energy);
        now_.strips = realm && now_.returnable > 0 ? realm->resetStrips() : 0;
        if (tab_ != 0 || now_.returnable <= 0) asking_ = false;
        int steps[3];
        const int parts = stepParts(points, steps);
        const auto under = [&](const Box& units) { return panel::scaled(x_, y_, units).has(pointer.x, pointer.y); };
        const auto controlUnder = [&]() {
            if (now_.returnable > 0 && under(kResetSquare)) return kResetId;
            if (tab_ != 0) return -1;
            if (spending) {
                for (int i = 0; i < 4; ++i)
                    if (under(plusFor(tops[i], step))) return i;
            }
            if (asking_) {
                if (under(foot.no)) return kNoId;
                if (under(foot.yes)) return kYesId;
                return -1;
            }
            if (spending && under(foot.step)) {
                for (int i = 0; i < parts; ++i) {
                    const float w = foot.step.w / float(parts);
                    if (under({foot.step.x + w * float(i), foot.step.y, w, foot.step.h})) return kStepIds + i;
                }
            }
            return -1;
        };
        const auto tabUnder = [&]() {
            for (int i = 0; i < kTabCount; ++i)
                if (panel::scaled(x_, y_, tabBox(i)).has(pointer.x, pointer.y)) return i;
            return -1;
        };
        over_ = controlUnder();
        const int overTab = tabUnder();
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
                    // Asked on the Attributes page, whichever page it was pressed from.
                    tab_ = 0;
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
        if (tab_ != 0) asking_ = false;
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

void Card::rebuild() {
    ++rebuilds_;
    canvas_.clear();
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

    // ---- the tabs -------------------------------------------------------------------------------
    // Underline tabs: the words on the sheet over one dark floor, the open one golden with a
    // golden bar on the floor (the user's pick of three, 2026-10-10).
    canvas_.rect(panel::scaled(x, y, {kWellX, kTabsTop + kTabsTall - 0.5f, kWellW, 0.5f}),
                 style::kIronDk);
    for (int i = 0; i < kTabCount; ++i) {
        const Box box = tabBox(i);
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
        const float lineStep = attrTops(counts, tops);
        const Foot foot = footFor(counts);
        const int gain = gainOf(now_.step, now_.points);
        const auto stateOf = [&](int id, bool off) {
            controls::State state;
            state.lift = now_.over == id ? 1.0f : 0.0f;
            state.held = now_.pushed == id;
            state.off = off;
            return state;
        };
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

        // ---- the foot: what a press adds, and the reset ------------------------------------------
        if (now_.asking) {
            // The canvas's question: one void well with a blood edge at its left, "Take back N
            // points?" with the count in the spend's red, No and Reset at its right; under it,
            // quietly, what a reset does -- or, in red, why it cannot.
            const Box ask = panel::scaled(x, y, foot.ask);
            panel::cell(canvas_, x, y, foot.ask, sheet::Cell::Rest);
            canvas_.rect({ask.x, ask.y + std::round(2.0f * k), std::max(1.0f, std::round(k)),
                          ask.h - std::round(4.0f * k)},
                         style::kBlood);
            const float size = kNameSize * k;
            const float base = panel::centredBaseline(face, ask, size);
            // A space's width, which a word's own trailing space does not carry.
            const float space = controls::labelWidth(size, "a a") - controls::labelWidth(size, "aa");
            float at = x + (kWellX + 7.0f) * k;
            at += controls::label(canvas_, at, base, size, kPlain, "Take back") + space;
            at += controls::label(canvas_, at, base, size, kSpendable, panel::commas(now_.returnable)) + space;
            controls::label(canvas_, at, base, size, kPlain, "points?");
            controls::button(canvas_, panel::scaled(x, y, foot.no), "No", controls::Kind::Secondary,
                             stateOf(kNoId, false), u, kTabSize * k);
            controls::button(canvas_, panel::scaled(x, y, foot.yes), "Reset", controls::Kind::Primary,
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
            const Box noteRow = panel::scaled(x, y, {kWellX, foot.ask.bottom(), kWellW, kBodyBottom - foot.ask.bottom()});
            const float noteSize = kLineSize * k;
            controls::label(canvas_, std::round(noteRow.midX() - controls::labelWidth(noteSize, note) * 0.5f),
                            panel::centredBaseline(face, noteRow, noteSize), noteSize, ink, note);
        } else {
            if (now_.points > 0) {
                const Box label{kWellX, foot.step.y, kStepLabelW, kCtlTall};
                smallCaps(kWellX + 2.0f, panel::centredBaseline(face, panel::scaled(x, y, label), kSmallCaps * k),
                          kQuiet, "Each press adds");
                int steps[3];
                const int parts = stepParts(now_.points, steps);
                const char* words[3] = {};
                int on = -1;
                for (int i = 0; i < parts; ++i) {
                    words[i] = steps[i] == 1 ? "1" : steps[i] == 10 ? "10" : "All";
                    if (steps[i] == now_.step) on = i;
                }
                const int over = now_.over >= kStepIds && now_.over < kStepIds + parts ? now_.over - kStepIds : -1;
                controls::choice(canvas_, panel::scaled(x, y, foot.step), words, parts, on, over, u, kTabSize * k);
            }
        }
    } else {
        // A page heavy with gear's chances closes its rows up before it would cross the foot.
        size_t rows = 0;
        for (const Group& group : now_.groups) rows += group.lines.size();
        const float heads = (kGroupHead + kGroupGap) * float(now_.groups.size());
        const float rowTall =
            rows ? std::min(kRowTall, (kBodyBottom - kBodyTop - heads) / float(rows)) : kRowTall;
        float uy = kBodyTop;
        for (const Group& group : now_.groups) {
            smallCaps(kWellX + 2.0f, at(uy + 1.0f, kSmallCaps), kDim, group.name);
            uy += kGroupHead;
            const float tall = rowTall * float(group.lines.size());
            panel::cell(canvas_, x, y, {kWellX, uy, kWellW, tall}, sheet::Cell::Rest);
            for (size_t l = 0; l < group.lines.size(); ++l) {
                const Line& line = group.lines[l];
                const Box row{kWellX, uy + rowTall * float(l), kWellW, rowTall};
                if (l > 0) canvas_.rect(panel::scaled(x, y, {row.x + 1.0f, row.y, row.w - 2.0f, 0.5f}), kDivider);
                const float base = panel::centredBaseline(face, panel::scaled(x, y, row), kRowSize * k);
                controls::label(canvas_, x + kLeft * k, base, kRowSize * k, line.dim ? kDim : kPlain,
                                line.label);
                controls::ranged(canvas_, x + kRight * k, base, kRowSize * k, line.dim ? kDim : kHeading,
                                 line.value);
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
    const float kicker = kLineSize * k;
    controls::caps(canvas_, x + kLeft * k, y + kFootTop * k + face.ascent(kicker), kicker, kQuiet,
                   "Experience", 0.04f);
    controls::ranged(canvas_, x + kFootRight * k, y + kFootTop * k + face.ascent(kicker), kicker, kPlain,
                     number((long long)now_.experience) + " / " + number((long long)to) + " · " +
                         std::to_string(int(share * 100.0f)) + "%");
    controls::meter(canvas_, panel::scaled(x, y, kBar), std::clamp(share, 0.0f, 1.0f),
                    gfx::rgba(0.910f, 0.863f, 0.773f), style::kIron, u);
    {
        // The reset's square, lit under the pointer (the question is up: held down); off with
        // nothing placed. Its name over it while the pointer is on it, as a flat tag.
        controls::State state;
        state.lift = now_.over == kResetId ? 1.0f : 0.0f;
        state.held = now_.pushed == kResetId;
        state.off = now_.returnable <= 0;
        controls::square(canvas_, panel::scaled(x, y, kResetSquare), controls::Glyph::Undo, state, u, false,
                         now_.asking);
        if (now_.over == kResetId) {
            const std::string name = "Reset points";
            const float size = kLineSize * k, padX = 4.0f * k;
            const float w = controls::labelWidth(size, name);
            const Box tag{std::round(x + kResetSquare.right() * k - w - padX * 2.0f),
                          std::round(y + (kResetSquare.y - 15.0f) * k), std::round(w + padX * 2.0f),
                          std::round(12.0f * k)};
            canvas_.rect(tag, style::kVoid);
            canvas_.rect({tag.x, tag.bottom() - std::max(1.0f, std::round(k * 0.5f)), tag.w,
                          std::max(1.0f, std::round(k * 0.5f))},
                         style::kIron);
            controls::label(canvas_, tag.x + padX, panel::centredBaseline(face, tag, size), size, kHeading, name);
        }
    }

    panel::close(canvas_, x, y, now_.overClose, now_.closing);
}

}  // namespace mu::game
