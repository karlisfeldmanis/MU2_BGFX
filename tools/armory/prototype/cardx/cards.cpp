// Calls the game's own item card builder (src/game/ui/describe.cpp) for every item in a few
// states and prints the tip::Sheet it returns as JSON. Built by gen_cards.py against the repo's
// sources, read-only.
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "content/tables.h"
#include "game/ui/describe.h"
#include "game/ui/tip.h"
#include "sim/items.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/wear.h"
#include "sim/broker.h"

using namespace mu;
using namespace mu::game;

namespace mu::core {
void logError(const char* f, ...) { va_list a; va_start(a, f); std::vfprintf(stderr, f, a); va_end(a); std::fputc('\n', stderr); }
void logf(const char* f, ...) { (void)f; }
}

static std::string esc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) o += ' ';
        else if (c >= 0x80) {
            // A whole UTF-8 sequence passes as it is; a lone Latin-1 byte (the card's " \xB7 "
            // joiner, which the face bakes as Latin-1) becomes \u00XX.
            int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
            bool ok = len > 0 && i + len <= s.size();
            for (int k = 1; ok && k < len; ++k) ok = ((unsigned char)s[i + k] & 0xC0) == 0x80;
            if (ok) {
                o += s.substr(i, size_t(len));
                i += size_t(len - 1);
            } else {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            }
        } else o += char(c);
    }
    return o;
}
static std::string q(const std::string& s) { return "\"" + esc(s) + "\""; }

static const char* toneName(tip::Tone t) {
    static const char* n[] = {"White", "Blue", "Red", "Yellow", "Green", "Gray", "Violet", "RedPurple", "Orange",
                              "Uncommon", "Rare", "Epic", "Legendary", "Artifact", "Quest", "RuneLegendary",
                              "RuneAncient"};
    return n[int(t)];
}
static const char* markName(tip::Mark m) {
    static const char* n[] = {"None", "Blade", "Shield", "Star", "Triangle", "Diamond", "Socket", "Note", "Ring",
                              "RingSet"};
    return n[int(m)];
}

static std::string row(const tip::Row& r) {
    std::string o = "{";
    auto add = [&](const std::string& k, const std::string& v) { o += (o.size() > 1 ? "," : "") + q(k) + ":" + v; };
    if (!r.label.empty()) add("label", q(r.label));
    if (!r.values.empty()) {
        std::string vs = "[";
        for (size_t i = 0; i < r.values.size(); ++i) {
            const tip::Value& v = r.values[i];
            vs += std::string(i ? "," : "") + "{\"text\":" + q(v.text) + ",\"tone\":\"" + toneName(v.tone) + "\"" +
                  (v.chip ? ",\"chip\":true" : "") + (v.delta.empty() ? "" : ",\"delta\":" + q(v.delta)) + "}";
        }
        add("values", vs + "]");
    }
    if (!r.free.empty()) add("free", q(r.free));
    if (!r.free.empty() || !r.tail.empty()) add("freeTone", q(toneName(r.freeTone)));
    if (!r.keyword.empty()) {
        add("keyword", q(r.keyword));
        if (r.keywordTone != tip::Tone::White) add("keywordTone", q(toneName(r.keywordTone)));
    }
    if (!r.tail.empty()) add("tail", q(r.tail));
    if (r.mark != tip::Mark::None) {
        add("mark", q(markName(r.mark)));
        add("markTone", q(toneName(r.markTone)));
    }
    if (r.labelToned) add("labelTone", q(toneName(r.labelTone)));
    if (r.gap) add("gap", "true");
    return o + "}";
}

static std::string sheetJson(const tip::Sheet& s) {
    std::string o = "{\"name\":" + q(s.name) + ",\"nameTone\":\"" + toneName(s.nameTone) + "\"";
    if (!s.base.empty()) o += ",\"base\":" + q(s.base);
    if (!s.bound.empty()) o += ",\"bound\":" + q(s.bound) + ",\"boundTone\":\"" + toneName(s.boundTone) + "\"";
    o += ",\"item\":" + std::string(s.item ? "true" : "false");
    if (s.wide > 0.0f) o += ",\"wide\":" + std::to_string(s.wide);
    char b[64];
    std::snprintf(b, sizeof b, ",\"artScale\":%.3f", s.artScale);
    o += b;
    if (!s.hero.empty()) {
        o += ",\"hero\":{\"value\":" + q(s.hero.value) + ",\"tone\":\"" + toneName(s.hero.tone) +
             "\",\"word\":" + q(s.hero.word) + ",\"rail\":[";
        for (size_t i = 0; i < s.hero.rail.size(); ++i) o += (i ? "," : "") + row(s.hero.rail[i]);
        o += "]}";
    }
    o += ",\"sections\":[";
    for (size_t i = 0; i < s.sections.size(); ++i) {
        const tip::Section& sec = s.sections[i];
        o += std::string(i ? "," : "") + "{";
        std::string inner;
        if (!sec.kicker.empty()) inner += "\"kicker\":" + q(sec.kicker) + ",";
        if (sec.mark != tip::Mark::None) inner += "\"mark\":\"" + std::string(markName(sec.mark)) + "\",";
        if (sec.framed) inner += "\"framed\":true,";
        inner += "\"rows\":[";
        for (size_t r = 0; r < sec.rows.size(); ++r) inner += (r ? "," : "") + row(sec.rows[r]);
        o += inner + "]}";
    }
    o += "]";
    if (!s.wear.empty()) {
        std::snprintf(b, sizeof b, ",\"worn\":%.3f", s.worn);
        o += ",\"wear\":" + q(s.wear) + b + ",\"wearTone\":\"" + toneName(s.wearTone) + "\"";
    }
    if (!s.price.empty()) o += ",\"price\":" + q(s.price) + ",\"priceTone\":\"" + toneName(s.priceTone) + "\"";
    if (!s.sell.empty()) o += ",\"sell\":" + q(s.sell);
    if (!s.note.empty()) o += ",\"note\":" + q(s.note) + ",\"noteTone\":\"" + toneName(s.noteTone) + "\"";
    auto foot = [&](const char* key, const std::vector<tip::FootLine>& lines) {
        if (lines.empty()) return;
        o += std::string(",\"") + key + "\":[";
        for (size_t i = 0; i < lines.size(); ++i) {
            o += std::string(i ? "," : "") + "{\"text\":" + q(lines[i].text) + ",\"tone\":\"" + toneName(lines[i].tone) +
                 "\"" + (lines[i].quiet ? ",\"quiet\":true" : "") + "}";
        }
        o += "]";
    };
    foot("who", s.who);
    if (s.classed) o += ",\"classed\":true";
    foot("keep", s.keep);
    if (s.rune >= 0) o += ",\"rune\":true";
    return o + "}";
}

// Who reads the card: his class's first fit, meeting exactly what the thing asks.
static sim::Wearer wearerFor(const content::Tables& tables, const sim::Held& held, bool met) {
    const content::ItemRow& row = tables.items[size_t(held.item)];
    const sim::Needs n = sim::asks(row, held.refinement, held.excellent != 0);
    sim::Wearer best;
    for (sim::Kin k : {sim::Kin::DarkKnight, sim::Kin::DarkWizard, sim::Kin::FairyElf, sim::Kin::MagicGladiator}) {
        sim::Wearer w;
        w.kin = k;
        const sim::HeroPoints start = sim::startingPoints(k);
        w.level = met ? std::max({1, n.level, 1}) : 1;
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

int main(int argc, char** argv) {
    content::Tables tables;
    std::string error;
    if (argc < 2 || !content::loadTables(argv[1], tables, error)) {
        std::fprintf(stderr, "tables: %s\n", error.c_str());
        return 1;
    }
    const sim::Satchel bag;
    const int32_t creationRow = runeRow(tables);
    std::printf("{\n");
    bool firstItem = true;
    for (size_t i = 0; i < tables.items.size(); ++i) {
        const content::ItemRow& row = tables.items[i];
        struct State { std::string name; sim::Held held; bool met = true; };
        std::vector<State> states;
        sim::Held base;
        base.item = int32_t(i);
        base.durability = int16_t(sim::stacks(row) ? 1 : sim::maximumDurability(row, 0));
        if (row.durability > 0 && !sim::wears(row)) base.durability = int16_t(std::min(row.durability, 255));
        states.push_back({"plain", base});
        const bool wing = sim::anyWing(row);
        const bool jewel = sim::jewellery(row);
        if (wing) {
            for (int plus : {9, 13}) {
                sim::Held h = base;
                h.refinement = int16_t(plus);
                h.luck = true;
                h.option = int8_t(plus == 9 ? 3 : 4);
                if (plus == 13 && sim::secondWing(row)) h.wing = sim::kWingMaxLife;
                h.durability = int16_t(sim::maximumDurability(row, h));
                states.push_back({"+" + std::to_string(plus) + " luck +" + std::to_string(h.option * 4) +
                                      (h.wing ? " life" : ""), h});
            }
        } else if (jewel) {
            sim::Held h = base;
            h.refinement = 4;
            h.luck = true;
            h.option = 1;
            if (sim::powered(row)) {
                int put = 0;
                for (uint8_t a : {uint8_t(2), uint8_t(6), uint8_t(1)}) {
                    if (put < 2 && a != uint8_t(sim::signatureOf(row))) h.affixes[put++] = a;
                }
            }
            states.push_back({sim::powered(row) ? "+4 luck option three powers" : "+4 luck option", h});
        } else if (sim::takesOptions(row)) {
            sim::Held h = base;
            h.luck = true;
            h.option = 1;
            states.push_back({"+0 luck +4", h});
            sim::Held h9 = base;
            h9.refinement = 9;
            h9.luck = true;
            h9.option = 3;
            h9.durability = int16_t(sim::maximumDurability(row, h9));
            states.push_back({"+9 luck +12", h9});
            if (!sim::excellentLine(row, 0).empty()) {
                sim::Held hx = base;
                hx.refinement = 11;
                hx.luck = true;
                hx.option = 4;
                hx.excellent = 0x09;
                hx.durability = int16_t(sim::maximumDurability(row, hx));
                std::string label = "+11 excellent (bits 0 and 3)";
                if (sim::takesSockets(row) && creationRow >= 0) {
                    // Typical runes for its class: a legendary, an epic and a rare that the game
                    // would let this class set in this thing.
                    const sim::Wearer who = wearerFor(tables, hx, true);
                    const int room = sim::mostSocketsOf(row);
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
        if (sim::takesOptions(row) || wing || jewel) {
            State lacking{"+0 unmet (level 1, starting stats)", base, false};
            states.push_back(lacking);
        }
        std::printf("%s%s: {", firstItem ? "" : ",\n", q(row.name).c_str());
        firstItem = false;
        for (size_t s = 0; s < states.size(); ++s) {
            const sim::Wearer who = wearerFor(tables, states[s].held, states[s].met);
            tip::Sheet sheet = describe(tables, states[s].held, who, bag);
            splitHint(tables, states[s].held, sheet);
            // The bag's "Sells for" strip (game/ui/bag.cpp): the realm's own sum, Realm::sellValue.
            sim::Held sold = states[s].held;
            if (const int pieces = sim::piecesSold(tables, sold); pieces > 0) sold.durability = int16_t(pieces);
            if (const int64_t fetches = sim::npcValue(tables, sold); fetches > 0) {
                std::string digits = std::to_string(fetches), grouped;
                for (size_t k = 0; k < digits.size(); ++k) {
                    if (k > 0 && (digits.size() - k) % 3 == 0) grouped += ',';
                    grouped += digits[k];
                }
                sheet.sell = grouped;
            }
            static const char* kinNames[] = {"DW", "Elf", "DK", "MG"};
            std::printf("%s%s: {\"reader\": {\"class\": \"%s\", \"level\": %d}, \"sheet\": %s}", s ? ",\n  " : "\n  ",
                        q(states[s].name).c_str(), kinNames[int(who.kin)], who.level, sheetJson(sheet).c_str());
        }
        std::printf("}");
    }
    std::printf("\n}\n");
    return 0;
}
