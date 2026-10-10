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
constexpr Box kBar{panel::kGridX, kFootTop + 12.0f, panel::kWellRight - panel::kGridX, 4.0f};

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

// Where each attribute's well stands: one after another, each pushed down by the lines the one
// before it carries.
void attrTops(const size_t counts[4], float tops[4]) {
    float y = kBodyTop;
    for (int i = 0; i < 4; ++i) {
        tops[i] = y;
        y += kAttrTall + kLineTop + kLineStep * float(counts[i]) + kAttrGap;
    }
}
Box plusFor(float top) {
    return {panel::kWellRight - kPlusSide - 3.0f, top + (kAttrTall - kPlusSide) * 0.5f, kPlusSide,
            kPlusSide};
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
                  const std::string& name, const Pointer& pointer, int* spend, bool* close) {
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
        now_.experience = hero->experience;
        now_.stats[0] = total.strength;
        now_.stats[1] = total.agility;
        now_.stats[2] = total.vitality;
        now_.stats[3] = total.energy;
        // With the stat runes in, as everything below them reads them; a raised one in blue.
        now_.raised = (total.strength != hero->points.strength ? 1 : 0) |
                      (total.agility != hero->points.agility ? 2 : 0) |
                      (total.vitality != hero->points.vitality ? 4 : 0) |
                      (total.energy != hero->points.energy ? 8 : 0);

        // The pointer, against this frame's layout.
        const Box cross = panel::scaled(x_, y_, panel::frameClose());
        overClose_ = cross.has(pointer.x, pointer.y);
        const size_t counts[4] = {now_.lines[0].size(), now_.lines[1].size(), now_.lines[2].size(),
                                  now_.lines[3].size()};
        float tops[4];
        attrTops(counts, tops);
        const bool spending = tab_ == 0 && hero->pointsInHand > 0;
        const auto plusUnder = [&]() {
            if (!spending) return -1;
            for (int i = 0; i < 4; ++i)
                if (panel::scaled(x_, y_, plusFor(tops[i])).has(pointer.x, pointer.y)) return i;
            return -1;
        };
        const auto tabUnder = [&]() {
            for (int i = 0; i < kTabCount; ++i)
                if (panel::scaled(x_, y_, tabBox(i)).has(pointer.x, pointer.y)) return i;
            return -1;
        };
        over_ = plusUnder();
        const int overTab = tabUnder();
        if (pointer.pressed) {
            if (overClose_) closing_ = true;
            pushed_ = over_;
            pressTab_ = overTab;
        }
        if (pointer.released) {
            // Only if the pointer is still on it, which is how a press is taken back.
            if (closing_ && overClose_ && close) *close = true;
            if (pushed_ >= 0 && pushed_ == over_ && spend) *spend = pushed_;
            if (pressTab_ >= 0 && pressTab_ == overTab && pressTab_ != tab_) tab_ = pressTab_;
            closing_ = false;
            pushed_ = -1;
            pressTab_ = -1;
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
        const float w = controls::capsWidth(size, tag, 0.04f);
        const float pad = 3.0f * k;
        const float tagRight = x + kRight * k;
        canvas_.rect({tagRight - w - pad, y + (kIdTop - 1.0f) * k, w + pad * 2.0f, 11.0f * k},
                     kSpendWash);
        controls::caps(canvas_, tagRight - w + pad * 0.0f, at(kIdTop + 1.5f, kLineSize), size,
                       kSpendable, tag, 0.04f);
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
        attrTops(counts, tops);
        for (int i = 0; i < 4; ++i) {
            const Box well{kWellX, tops[i], kWellW, kAttrTall};
            panel::cell(canvas_, x, y, well, sheet::Cell::Rest);
            const float figure = kFigureSize * k;
            const float base = panel::centredBaseline(face, panel::scaled(x, y, well), figure);
            controls::label(canvas_, x + kLeft * k, base, kNameSize * k, kPlain, names[i]);
            const float figureRight = now_.points > 0 ? kFigureRight : kRight;
            controls::ranged(canvas_, x + figureRight * k, base, figure,
                             (now_.raised >> i) & 1 ? panel::kOptioned : kHeading,
                             std::to_string(now_.stats[i]));
            for (size_t l = 0; l < now_.lines[i].size(); ++l) {
                const Line& line = now_.lines[i][l];
                const float uy = tops[i] + kAttrTall + kLineTop + kLineStep * float(l);
                left(kLeft, uy, kLineSize, kQuiet, line.label);
                right(kRight, uy, kLineSize, kPlain, line.value);
            }
            // The plus only where there is something to spend: MU hides it rather than greying
            // it. A red square, the one thing on the card that asks to be pressed.
            if (now_.points <= 0) continue;
            const Box plus = panel::scaled(x, y, plusFor(tops[i]));
            const float side = std::round(std::min(plus.w, plus.h));
            controls::State state;
            state.lift = now_.over == i ? 1.0f : 0.0f;
            state.held = now_.pushed == i;
            controls::square(canvas_,
                             {std::round(plus.midX() - side * 0.5f), std::round(plus.midY() - side * 0.5f),
                              side, side},
                             controls::Glyph::Plus, state, u, true);
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
                   y + kFootRule * k, u);
    const uint64_t from = sim::neededExperience(now_.level);
    const uint64_t to = sim::neededExperience(now_.level + 1);
    const float share =
        to > from ? float(double(now_.experience > from ? now_.experience - from : 0) /
                          double(to - from))
                  : 1.0f;
    const float kicker = kLineSize * k;
    controls::caps(canvas_, x + kLeft * k, y + kFootTop * k + face.ascent(kicker), kicker, kQuiet,
                   "Experience", 0.04f);
    controls::ranged(canvas_, x + kRight * k, y + kFootTop * k + face.ascent(kicker), kicker, kPlain,
                     number((long long)now_.experience) + " / " + number((long long)to) + " · " +
                         std::to_string(int(share * 100.0f)) + "%");
    controls::meter(canvas_, panel::scaled(x, y, kBar), std::clamp(share, 0.0f, 1.0f),
                    gfx::rgba(0.910f, 0.863f, 0.773f), style::kIron, u);

    panel::close(canvas_, x, y, now_.overClose, now_.closing);
}

}  // namespace mu::game
