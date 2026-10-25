// cards.json: every item's card in a few states, from the game's own card builder
// (src/game/ui/describe.cpp) and the bag's "Sells for" strip (Realm::sellValue's sum).
// Moved in from tools/armory/prototype/cardx/cards.cpp.

#include <algorithm>

#include "compat.h"
#include "dump.h"
#include "game/ui/describe.h"
#include "game/ui/tip.h"
#include "sim/broker.h"
#include "sim/machine.h"
#include "sim/items.h"
#include "sim/wear.h"

namespace armory {
namespace {

using namespace mu::game;

const char* toneName(tip::Tone t) {
    static const char* n[] = {"White", "Blue", "Red", "Yellow", "Green", "Gray", "Violet", "RedPurple", "Orange",
                              "Uncommon", "Rare", "Epic", "Legendary", "Artifact", "Quest", "RuneLegendary",
                              "RuneAncient"};
    static_assert(int(tip::Tone::RuneAncient) == 16, "a tone was added: name it here");
    return n[int(t)];
}
const char* markName(tip::Mark m) {
    static const char* n[] = {"None", "Blade", "Shield", "Star", "Triangle", "Diamond", "Socket", "Note", "Ring",
                              "RingSet"};
    static_assert(int(tip::Mark::RingSet) == 9, "a mark was added: name it here");
    return n[int(m)];
}

J row(const tip::Row& r) {
    J o = J::obj();
    if (!r.label.empty()) o.set("label", r.label);
    if (!r.values.empty()) {
        J vs = J::arr();
        for (const tip::Value& v : r.values) {
            J one = J::obj();
            one.set("text", v.text).set("tone", toneName(v.tone));
            if (v.chip) one.set("chip", true);
            if (!v.delta.empty()) one.set("delta", v.delta);
            vs.push(one);
        }
        o.set("values", vs);
    }
    if (!r.free.empty()) o.set("free", r.free);
    if (!r.free.empty() || !r.tail.empty()) o.set("freeTone", toneName(r.freeTone));
    if (!r.keyword.empty()) {
        o.set("keyword", r.keyword);
        if (r.keywordTone != tip::Tone::White) o.set("keywordTone", toneName(r.keywordTone));
    }
    if (!r.tail.empty()) o.set("tail", r.tail);
    if (r.mark != tip::Mark::None) o.set("mark", markName(r.mark)).set("markTone", toneName(r.markTone));
    if (r.labelToned) o.set("labelTone", toneName(r.labelTone));
    if (r.gap) o.set("gap", true);
    return o;
}

J sheetJson(const tip::Sheet& s) {
    J o = J::obj();
    o.set("name", s.name).set("nameTone", toneName(s.nameTone));
    if (!s.base.empty()) o.set("base", s.base);
    if (!s.bound.empty()) o.set("bound", s.bound).set("boundTone", toneName(s.boundTone));
    o.set("item", s.item);
    if (s.wide > 0.0f) o.set("wide", J(s.wide));
    o.set("artScale", J(s.artScale));
    if (!s.hero.empty()) {
        J rail = J::arr();
        for (const tip::Row& r : s.hero.rail) rail.push(row(r));
        o.set("hero", J::obj().set("value", s.hero.value).set("tone", toneName(s.hero.tone)).set("word", s.hero.word)
                          .set("rail", rail));
    }
    J sections = J::arr();
    for (const tip::Section& sec : s.sections) {
        J one = J::obj();
        if (!sec.kicker.empty()) one.set("kicker", sec.kicker);
        if (sec.mark != tip::Mark::None) one.set("mark", markName(sec.mark));
        if (sec.framed) one.set("framed", true);
        J rows = J::arr();
        for (const tip::Row& r : sec.rows) rows.push(row(r));
        one.set("rows", rows);
        sections.push(one);
    }
    o.set("sections", sections);
    if (!s.wear.empty()) o.set("wear", s.wear).set("worn", J(s.worn)).set("wearTone", toneName(s.wearTone));
    if (!s.price.empty()) o.set("price", s.price).set("priceTone", toneName(s.priceTone));
    if (!s.sell.empty()) o.set("sell", s.sell);
    if (!s.note.empty()) o.set("note", s.note).set("noteTone", toneName(s.noteTone));
    const auto foot = [&](const char* key, const std::vector<tip::FootLine>& lines) {
        if (lines.empty()) return;
        J a = J::arr();
        for (const tip::FootLine& l : lines) {
            J one = J::obj();
            one.set("text", l.text).set("tone", toneName(l.tone));
            if (l.quiet) one.set("quiet", true);
            a.push(one);
        }
        o.set(key, a);
    };
    foot("who", s.who);
    if (s.classed) o.set("classed", true);
    foot("keep", s.keep);
    if (s.rune >= 0) o.set("rune", true);
    return o;
}

// Who reads the card: his class's first fit, meeting exactly what the thing asks.
sim::Wearer wearerFor(const content::Tables& tables, const sim::Held& held, bool met) {
    const content::ItemRow& r = tables.items[size_t(held.item)];
    const sim::Needs n = sim::asks(r, held.refinement, held.excellent != 0);
    sim::Wearer best;
    for (sim::Kin k : kKins) {
        sim::Wearer w;
        w.kin = k;
        const sim::HeroPoints start = sim::startingPoints(k);
        w.level = met ? std::max(1, n.level) : 1;
        w.points.strength = met ? std::max(start.strength, n.strength) : start.strength;
        w.points.agility = met ? std::max(start.agility, n.agility) : start.agility;
        w.points.vitality = met ? std::max(start.vitality, n.vitality) : start.vitality;
        w.points.energy = met ? std::max(start.energy, n.energy) : start.energy;
        w.second = met;
        w.rebirths = met ? 1 : 0;
        w.totals = w.points;
        w.maxHealth = 1000;
        w.maxMana = 500;
        if (k == sim::Kin::DarkWizard || k == sim::Kin::MagicGladiator) {
            w.wizardMinimum = w.totals.energy / 9.0;
            w.wizardMaximum = w.totals.energy / 4.0;
        }
        if (sim::fits(tables, w, held) || !met) return w;
        if (best.level == 1 && k == sim::Kin::DarkKnight) best = w;
        sim::Wearer more = w;  // a class that may wear it but whose level falls short of a rune
        more.level = 400;
        if (sim::fits(tables, more, held)) return more;
    }
    return best;
}

std::string grouped(int64_t v) {
    const std::string digits = std::to_string(v);
    std::string out;
    for (size_t k = 0; k < digits.size(); ++k) {
        if (k > 0 && (digits.size() - k) % 3 == 0) out += ',';
        out += digits[k];
    }
    return out;
}

}  // namespace

J dumpCards(const Dump& d) {
    const content::Tables& tables = d.items();
    const sim::Satchel bag;
    const int32_t creationRow = runeRow(tables);
    J items = J::obj();
    for (size_t i = 0; i < tables.items.size(); ++i) {
        const content::ItemRow& r = tables.items[i];
        struct State {
            std::string name;
            sim::Held held;
            bool met = true;
        };
        std::vector<State> states;
        sim::Held base;
        base.item = int32_t(i);
        base.durability = int16_t(sim::stacks(r) ? 1 : sim::maximumDurability(r, 0));
        if (r.durability > 0 && !sim::wears(r)) base.durability = int16_t(std::min(r.durability, 255));
        states.push_back({"plain", base});
        const bool wing = sim::anyWing(r);
        const bool jewel = sim::jewellery(r);
        if (wing) {
            // +9, and the most the Chaos Machine refines anything to (sim::kMachineCap).
            for (int plus : {9, sim::kMachineCap}) {
                sim::Held h = base;
                h.refinement = int16_t(plus);
                h.luck = true;
                h.option = int8_t(plus == 9 ? 3 : 4);
                if (plus == sim::kMachineCap && sim::secondWing(r)) h.wing = sim::kWingMaxLife;
                h.durability = int16_t(sim::maximumDurability(r, h));
                states.push_back({"+" + std::to_string(plus) + " luck +" + std::to_string(h.option * 4) +
                                      (h.wing ? " life" : ""),
                                  h});
            }
        } else if (jewel) {
            sim::Held h = base;
            h.refinement = 4;
            h.luck = true;
            h.option = 1;
            if (sim::powered(r)) {
                int put = 0;
                for (uint8_t a : {uint8_t(2), uint8_t(6), uint8_t(1)}) {
                    if (put < 2 && a != uint8_t(sim::signatureOf(r))) h.affixes[put++] = a;
                }
            }
            states.push_back({sim::powered(r) ? "+4 luck option three powers" : "+4 luck option", h});
        } else if (sim::takesOptions(r)) {
            sim::Held h = base;
            h.luck = true;
            h.option = 1;
            states.push_back({"+0 luck +4", h});
            sim::Held h9 = base;
            h9.refinement = 9;
            h9.luck = true;
            h9.option = 3;
            h9.durability = int16_t(sim::maximumDurability(r, h9));
            states.push_back({"+9 luck +12", h9});
            if (!sim::excellentLine(r, 0).empty()) {
                sim::Held hx = base;
                hx.refinement = 11;
                hx.luck = true;
                hx.option = 4;
                hx.excellent = 0x09;
                hx.durability = int16_t(sim::maximumDurability(r, hx));
                std::string label = "+11 excellent (bits 0 and 3)";
                if (sim::takesSockets(r) && creationRow >= 0) {
                    // Typical runes for its class: a legendary, an epic and a rare that the game
                    // would let this class set in this thing.
                    const sim::Wearer who = wearerFor(tables, hx, true);
                    const int room = sim::mostSocketsOf(r);
                    hx.sockets = uint8_t(room);
                    int set = 0;
                    for (sim::Rarity want : {sim::Rarity::Legendary, sim::Rarity::Epic, sim::Rarity::Rare}) {
                        for (int p = 1; p < 256 && set < room; ++p) {
                            const sim::PowerRow* power = sim::powerOf(uint8_t(p));
                            if (!power || power->rarity != want) continue;
                            bool used = false;
                            for (int k = 0; k < set; ++k) used |= hx.powers[k] == uint8_t(p);
                            if (used) continue;
                            sim::Held rune;
                            rune.item = creationRow;
                            rune.powers[0] = uint8_t(p);
                            sim::Held probe = hx;
                            for (int k = set; k < 3; ++k) probe.powers[k] = 0;
                            if (!sim::settable(tables, rune, probe, who.kin, true)) continue;
                            hx.powers[set++] = uint8_t(p);
                            break;
                        }
                    }
                    label += " " + std::to_string(room) + " sockets";
                }
                states.push_back({label, hx});
            }
        }
        if (sim::takesOptions(r) || wing || jewel) states.push_back({"+0 unmet (level 1, starting stats)", base, false});
        J card = J::obj();
        for (const State& s : states) {
            const sim::Wearer who = wearerFor(tables, s.held, s.met);
            sim::Wearer reader = who;
            reader.anyClass = true;  // the Armory's card is every class's
            tip::Sheet sheet = describe(tables, s.held, reader, bag);
            splitHint(tables, s.held, sheet);
            // The bag's "Sells for" strip (game/ui/bag.cpp): Realm::sellValue's sum.
            sim::Held sold = s.held;
            if (const int pieces = compat::piecesSold(tables, sold); pieces > 0) sold.durability = int16_t(pieces);
            const int64_t fetches = sim::npcValue(tables, sold);
            if (fetches > 0) sheet.sell = grouped(fetches);
            sim::Needs needs = sim::asks(r, s.held.refinement, s.held.excellent != 0);
            // The level as the card asks it (describe): its own, a scroll's skill's, a rune's.
            needs.level = std::max({needs.level, r.teaches > 0 ? r.teachesLevel : 0, compat::runeLevel(tables, s.held)});
            J state = J::obj();
            state.set("reader", J::obj().set("class", kinName(who.kin)).set("level", who.level))
                .set("sheet", sheetJson(sheet))
                .set("sells", J(fetches))
                .set("needs", J::obj().set("level", needs.level).set("strength", needs.strength)
                                  .set("agility", needs.agility).set("energy", needs.energy)
                                  .set("vitality", needs.vitality));
            card[s.name] = state;
        }
        items[r.name] = card;
    }
    J out = d.stamped();
    out.set("items", items);
    return out;
}

}  // namespace armory
