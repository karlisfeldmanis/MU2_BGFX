// rules.json: what else the site needs from code: the runes and their odds, the quests' rows and
// pay, who does what in town (Realm::talk) and their shelves, the Elites, the Chaos Machine and
// the Jeweler, the maps' rules and which of them make things live.

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>

#include "compat.h"
#include "dump.h"
#include "sim/broker.h"
#include "sim/config.h"
#include "sim/elite.h"
#include "sim/event.h"
#include "sim/items.h"
#include "sim/jeweler.h"
#include "sim/machine.h"
#include "sim/maps.h"
#include "sim/market.h"
#include "sim/quests.h"
#include "sim/realm.h"
#include "sim/realm_tuning.h"
#include "sim/rebirth.h"
#include "sim/square.h"
#include "sim/vault.h"
#include "sim/wear.h"

namespace armory {
namespace {

using namespace mu::sim;

// Whether a monster of kAncientLevel nests in a world of this release (dumpRules sets it).
bool ancientsDrop = false;

const char* rarityKey(Rarity r) {
    switch (r) {
        case Rarity::Rare: return "rare";
        case Rarity::Epic: return "epic";
        case Rarity::Legendary: return "legendary";
        case Rarity::Common: return "common";
        case Rarity::Ancient: return "ancient";
    }
    return "?";
}

J classesOf(uint8_t bits) {
    J a = J::arr();
    for (Kin k : kKins) {
        if (bits & classBit(k)) a.push(kinName(k));
    }
    return a;
}

// drawRunePower's odds (realm_items.cpp), mirrored: the rarities a level reaches that hold a
// power the killer's class may draw, each at its kRuneRarityShare of their sum, then one of that
// rarity's alike.
J runeOdds(Kin kin, bool second, int level, bool commons) {
    const auto drawable = [&](const PowerRow& row) {
        if (!commons && row.rarity == Rarity::Common) return false;
        const Element element = elementOf(row.power);
        if (element != Element::None) return elementServes(element, kin);
        return row.takenBy(kin, second);
    };
    int count[kRarities] = {};
    double held = 0.0;
    for (int p = 1; powerOf(uint8_t(p)); ++p) {
        const int r = int(powerOf(uint8_t(p))->rarity);
        if (drawable(*powerOf(uint8_t(p))) && level >= kRuneRarityLevel[r]) ++count[r];
    }
    for (int r = 0; r < kRarities; ++r) held += count[r] > 0 ? kRuneRarityShare[r] : 0.0;
    J rarities = J::obj(), powers = J::obj();
    for (int r = 0; r < kRarities; ++r) {
        if (count[r] == 0 || held <= 0.0) continue;
        rarities.set(rarityKey(Rarity(r)), J::p(kRuneRarityShare[r] / held));
    }
    for (int p = 1; powerOf(uint8_t(p)); ++p) {
        const PowerRow& row = *powerOf(uint8_t(p));
        const int r = int(row.rarity);
        if (!drawable(row) || level < kRuneRarityLevel[r] || held <= 0.0) continue;
        powers.set(std::to_string(p), J::p(kRuneRarityShare[r] / held / count[r]));
    }
    return J::obj().set("rarity", rarities).set("power", powers);
}

// What a later release added, read where this one has it: a compile-time question, so the dump
// builds against the release's own src/ and against HEAD alike.
template <class P>
J stackingJson(P power) {
    if constexpr (requires { stackingOf(power); }) {  // sim::stackingOf, found by its argument
        const auto s = stackingOf(power);
        std::string says;
        if constexpr (std::is_pointer_v<decltype(s.says)>) {
            says = s.says ? s.says : "";
        } else {
            says = s.says;  // a std::string at HEAD
        }
        return J::obj().set("stacks", s.stacks).set("says", says);
    } else {
        return J();
    }
}
template <class N>
bool removes(const N& need) {
    if constexpr (requires { need.remove; }) {
        return need.remove;
    } else {
        return false;
    }
}

// One page a power: who sets it where, and the odds a kill's rune is it, by killer and level.
J runes() {
    std::set<int> levels = {1};
    for (int r = 0; r < kRarities; ++r) levels.insert(std::max(1, kRuneRarityLevel[r]));
    // drawRunePower's odds for every killer and level a rarity opens at.
    std::map<std::string, std::map<int, J>> odds;
    for (Kin kin : kKins) {
        for (bool second : {false, true}) {
            if (kin == Kin::MagicGladiator && second) continue;
            for (int level : levels) odds[std::string(kinName(kin)) + (second ? "2" : "")][level] = runeOdds(kin, second, level, true);
        }
    }
    J powers = J::arr();
    for (int p = 1; powerOf(uint8_t(p)); ++p) {
        const PowerRow& row = *powerOf(uint8_t(p));
        J slots = J::arr();
        if (row.slots & kInWeapon) slots.push("weapon");
        if (row.slots & kInShield) slots.push("shield");
        if (row.slots & kInArmour) slots.push("armour");
        if (row.slots & kInJewellery) slots.push("jewellery");
        J chance = J::obj();
        for (const auto& [who, byLevel] : odds) {
            J one = J::obj();
            for (const auto& [level, o] : byLevel) {
                if (const J* v = o.find("power")->find(std::to_string(p))) one.set(std::to_string(level), *v);
            }
            if (one.size()) chance.set(who, one);
        }
        powers.push(J::obj()
                        .set("key", "rune-" + std::to_string(p)).set("power", p).set("name", row.name).set("tells", row.tells)
                        .set("classes", classesOf(row.classes)).set("slots", slots)
                        .set("rarity", rarityKey(row.rarity)).set("rarity_name", rarityName(row.rarity))
                        .set("second", row.second).set("once", row.once)
                        .set("element", int(elementOf(row.power)))
                        .set("stacking", stackingJson(row.power))
#ifdef ARMORY_RUNE_HERO_LEVEL
                        .set("hero_level", kRuneHeroLevel[int(row.rarity)])
#endif
                        .set("from_level", kRuneRarityLevel[int(row.rarity)])
                        .set("sale", J(kRuneSale[int(row.rarity)]))
                        .set("live", row.rarity != Rarity::Ancient || ancientsDrop)
                        .set("chance", chance)
                        .set("chance_note", "of a kill's Rune of Creation, by killer (2: second class) and the kill's level"));
    }
    return powers;
}

// The runes' constants, flat (rules.json's `constants`: no page of its own).
void runeConstants(J& c) {
    J rarities = J::arr(), share = J::arr(), from = J::arr(), hero = J::arr(), sale = J::arr();
    for (int r = 0; r < kRarities; ++r) {
        rarities.push(rarityKey(Rarity(r)));
        share.push(J(kRuneRarityShare[r]));
        from.push(kRuneRarityLevel[r]);
#ifdef ARMORY_RUNE_HERO_LEVEL
        hero.push(kRuneHeroLevel[r]);
#endif
        sale.push(J(kRuneSale[r]));
    }
    c.set("rarities", rarities).set("kRuneRarityShare", share).set("kRuneRarityLevel", from).set("kRuneHeroLevel", hero)
        .set("kRuneSale", sale).set("kAncientLevel", kAncientLevel).set("kCreationLevel", kCreationLevel)
        .set("kCreationChance", J(kCreationChance)).set("kMostSockets", kMostSockets)
        .set("kSocketChance", J(kSocketChance)).set("kMoreSocketChance", J(kMoreSocketChance));
}

J questItem(const QuestItem& q) {
    J o = J::obj();
    o.set("item", q.item ? q.item : "").set("count", q.count).set("plus", q.plus)
        .set("class", q.kin >= 0 ? J(kinName(Kin(q.kin))) : J())
        .set("sockets", int(q.sockets)).set("power", int(q.power)).set("first_only", q.firstOnly)
        .set("rank", compat::rankOf(q));
    // A Rune of Creation's power, and a powered piece's powers, by the game's own names.
    if (q.power) {
        const PowerRow* p = powerOf(q.power);
        o.set("power_name", p ? J(p->name) : J()).set("power_key", "rune-" + std::to_string(int(q.power)));
    }
    J aff = J::arr();
    for (uint8_t a : q.affixes) {
        if (a) aff.push(J::obj().set("affix", int(a)).set("name", affixName(Affix(a))));
    }
    if (aff.size()) o.set("affixes", aff);
    return o;
}

J quests() {
    J list = J::arr();
    for (int i = 0; i < kQuests; ++i) {
        const QuestRow& r = questAt(i);
        J offer = J::arr(), handIn = J::arr(), steps = J::arr(), paid = J::arr(), choices = J::arr(), natives = J::arr();
        for (const char* s : r.offer) {
            if (s && *s) offer.push(s);
        }
        for (const char* s : r.handIn) {
            if (s && *s) handIn.push(s);
        }
        for (int s = 0; s < r.stepCount; ++s) {
            const QuestStepRow& st = r.steps[s];
            steps.push(J::obj()
                           .set("kind", st.kind == QuestStepKind::Clear ? "clear" : st.kind == QuestStepKind::Find ? "find" : "return")
                           .set("target", st.target).set("count", st.count).set("line", st.line)
                           .set("item", st.item ? J(st.item) : J()));
        }
        for (int p = 0; p < r.paidCount; ++p) paid.push(questItem(r.paid[p]));
        for (int p = 0; p < r.choiceCount; ++p) choices.push(questItem(r.choices[p]));
        for (Kin k : kKins) {
            if (questNative(r, int(k))) natives.push(kinName(k));
        }
        J after = J::arr();
        for (int b = 0; b < kQuests; ++b) {
            if (r.afterAny & (1u << b)) after.push(questAt(b).key);
        }
        bool demo = false;
        for (int d : kDemoQuests) demo |= d == i;
        J o = J::obj();
        // The demo quest is given only under --quest-demo (RealmConfig::questDemo, off in the game).
        o.set("index", i).set("key", r.key).set("demo", demo).set("live", !demo || RealmConfig{}.questDemo).set("giver", r.giver).set("giverName", r.giverName)
            .set("place", r.place).set("receiver", questReceiver(r)).set("receiverName", questReceiverName(r))
            .set("receiverPlace", r.receiverPlace).set("title", r.title).set("offer", offer).set("underway", r.underway)
            .set("handIn", handIn).set("resting", r.resting).set("next", r.next).set("voice", r.voice)
            .set("steps", steps).set("repeatSeconds", J((long long)r.repeatSeconds)).set("afterAny", after)
            .set("minLevel", r.minLevel).set("gateLevel", r.gateLevel).set("promotes", r.promotes).set("boon", r.boon)
            .set("second", r.second).set("natives", natives).set("strangers", r.strangers).set("stranger", r.stranger)
            .set("experience", J((long long)r.experience)).set("firstExperience", J((long long)r.firstExperience))
            .set("zen", J((long long)r.zen)).set("paid", paid).set("choices", choices)
            .set("treasureFolkMap", questFolkMap(r.giver));
        list.push(o);
    }
    return list;
}

// Realm::talk's chain, mirrored by name: what talking to an NPC numbered `n` opens, first match.
std::string serviceOf(int32_t n) {
    if (sells(n)) return "shop";
    if (n == kVaultKeeper) return "vault";
    if (n == kBroker) return "broker";
    if (n == kChaosGoblin) return "chaos_machine";
    if (n == kJeweler) return "jeweler";
    if (n == kLugard) return "rebirth";
    if (questOf(n) >= 0) return "quests";
    if (n == kArchangel) return "archangel";
    if (n == kMessenger) return "blood_castle_gate";
    if (n == kCharon) return "devil_square_gate";
    if (n == kGuildMaster || n == kThompson || questReceives(n)) return "greets";
    return "none";
}

// One page a townsperson as the game shows him: by NPC number, figure and name, so the Crossbow
// Guards, the Elf Archers and the Elf Crossbow Guards (all MU's 247) are each their own, with their
// own spots. What he does is the number's (Realm::talk; a guard's post is Realm::raiseWardens').
J npcs(const Dump& d) {
    const content::Tables& items = d.items();
    std::map<std::tuple<int32_t, std::string, std::string>, J> placed;
    for (const World& w : d.worlds) {
        for (const content::Townsperson& one : w.tables->folk) {
            J& o = placed[{one.number, one.figure, one.name}];
            if (o.null()) {
                const std::string service = serviceOf(one.number);
                const WardenRow* warden = wardenRow(one.number);
                J services = J::arr();
                if (service != "none" && service != "greets") services.push(service);
                if (sells(one.number)) services.push("buys");
                if (repairsAt(one.number)) services.push("repair");
                if (questOf(one.number) >= 0 && service != "quests") services.push("quests");
                if (questReceives(one.number)) services.push("quest_hand_in");
                if (warden) services.push("guards_the_town");
                const std::string role = service != "none" ? service : warden ? "guard" : "";
                o = J::obj();
                o.set("npc", one.number).set("name", one.name).set("figure", one.figure)
                    .set("role", role.empty() ? J() : J(role)).set("services", services)
                    .set("repairs", repairsAt(one.number)).set("where", J::arr());
                if (warden) {
                    J damage = J::arr();
                    damage.push(warden->minimumDamage);
                    damage.push(warden->maximumDamage);
                    o.set("guard", J::obj().set("level", warden->level).set("life", warden->health).set("damage", damage)
                                       .set("defense", warden->defense).set("attack_range", warden->attackRange)
                                       .set("view_range", warden->viewRange).set("leash", warden->leash));
                }
                int count = 0;
                const Offer* stock = stockOf(one.number, &count);
                if (count > 0) {
                    J shelf = J::arr();
                    for (int i = 0; i < count; ++i) {
                        const Offer& of = stock[i];
                        const int32_t item = items.itemAt(of.group, of.number);
                        if (item < 0) continue;
                        const content::ItemRow& row = items.items[size_t(item)];
                        shelf.push(J::obj()
                                       .set("slot", of.slot).set("item", row.name).set("plus", of.refinement)
                                       .set("pieces", of.pieces).set("skill", of.skill)
                                       .set("price", J((long long)buyingPrice(row, of.refinement, std::max(1, of.pieces), of.skill))));
                    }
                    o.set("shelf", shelf);
                }
            }
            o["where"].push(J::obj().set("world", w.name).set("map", w.tables->map).set("x", one.x).set("y", one.y)
                                .set("live", live(d, w)));
        }
    }
    J list = J::arr();
    for (auto& [k, o] : placed) {
        bool any = false;
        for (const J& at : o["where"].items()) any = any || at.find("live")->boolean();
        o.set("live", any);
        list.push(std::move(o));
    }
    return list;
}

J elites(const Dump& d) {
    J never = J::arr();
    for (int32_t n : kNeverElite) never.push(n);
    J list = J::arr();
    for (const EliteMap& row : kEliteMaps) {
        std::string world;
        for (const World& w : d.worlds) {
            if (w.tables->map == row.map) world = w.name;
        }
        const MapRow* r = nullptr;
        for (const World& w : d.worlds) {
            if (w.tables->map == row.map) r = mapOf(w.name);
        }
        list.push(J::obj()
                      .set("key", world.empty() ? "map-" + std::to_string(row.map) : world)
                      .set("name", "Elites of " + (world.empty() ? "map " + std::to_string(row.map) : world))
                      .set("world", world).set("map", row.map).set("count", row.count).set("live", !world.empty() && r && reachable(row.map))
                      .set("life", J(double(row.life))).set("damage", J(double(kEliteDamage)))
                      .set("defense", J(double(kEliteDefense))).set("swing", J(double(kEliteSwing)))
                      .set("stride", J(double(kEliteStride))).set("experience", kEliteExperience)
                      .set("strongest", kEliteStrongest).set("never", never));
        J& e = list.items().back();
        e.set("pieces", J::arr());
        e["pieces"].push(kEliteLeastPieces);
        e["pieces"].push(kEliteMostPieces);
        e.set("levels_up", kEliteLevelsUp).set("luck_in_100", kEliteLuckIn100)
            .set("second_excellent_in_100", kEliteSecondExcellentIn100).set("jewel_in_100", kEliteJewelIn100)
            .set("second_jewel_in_100", kEliteSecondJewelIn100).set("zen_times", kEliteZen)
            .set("rise_seconds", J::arr());
        e["rise_seconds"].push(J((long long)(kEliteRiseLeastTicks / 20)));
        e["rise_seconds"].push(J((long long)(kEliteRiseMostTicks / 20)));
    }
    return list;
}

// The Chaos Machine's services and the Jeweler's, a page each; their constants into `c`.
J mixes(const Dump& d, J& c) {
    const content::Tables& t = d.items();
    const auto itemName = [&](int group, int number) -> J {
        const int32_t item = t.itemAt(group, number);
        return item >= 0 ? J(t.items[size_t(item)].name) : J();
    };
    J list = J::arr();
    for (int s = 0; s < kServices; ++s) {
        const Service service = Service(s);
        bool listed = false;
        for (Service one : kRowServices) listed |= one == service;
        // What the empty box asks, in the machine's own words (sim::judge).
        const Machine box;
        const Judged j = judge(t, box, service);
        J needs = J::arr();
        for (int n = 0; n < j.needCount; ++n) {
            needs.push(J::obj().set("name", j.needs[n].name).set("need", j.needs[n].need).set("at_least", j.needs[n].atLeast)
                           .set("remove", removes(j.needs[n])));
        }
        J e = J::obj();
        e.set("key", std::string("machine-") + serviceName(service)).set("name", serviceName(service))
            .set("at", "chaos_machine").set("npc", kChaosGoblin).set("service", s).set("verb", serviceVerb(service))
            .set("listed", listed).set("by_recipe", byRecipe(service)).set("title", j.title).set("asks", needs)
            .set("success", j.success).set("failure", j.failure).set("rate", j.rate).set("zen", J((long long)j.zen));
        // The wings a wings mix makes, the mixer's class's (firstWingOf, secondWingOf).
        if (service == Service::FirstWings || service == Service::SecondWings) {
            J result = J::arr();
            for (Kin k : kKins) {
                const bool first = service == Service::FirstWings;
                result.push(J::obj().set("item", itemName(12, first ? firstWingOf(k) : secondWingOf(k)))
                                .set("class", kinName(k)).set("name", first ? firstWingName(k) : secondWingName(k)));
            }
            e.set("result", result);
        }
        list.push(e);
    }
    // The Jeweler (sim/jeweler.h): recut, salvage, combine.
    J salvage = J::arr();
    for (int prize = 0; prize < kSalvagePrizes; ++prize) {
        const ItemKey key = salvagePrize(prize);
        salvage.push(J::obj().set("item", itemName(key.group, key.number)).set("count", salvageCount(prize)));
    }
    J odds = J::arr();
    for (const auto& row : kSalvageOdds) {
        J a = J::arr();
        for (int v : row) a.push(v);
        odds.push(a);
    }
    J recut = J::arr();
    for (int plus = 0; plus <= kJewelleryMostPlus; ++plus) recut.push(J((long long)recutZen(plus)));
    J combine = J::obj();
    for (int powers = 1; powers <= 4; ++powers) combine.set(std::to_string(powers), J((long long)combineZen(powers)));
    J jewel = J::arr();
    jewel.push(J::obj().set("item", itemName(kJewelerJewel.group, kJewelerJewel.number)));
    list.push(J::obj().set("key", "jeweler-recut").set("name", "Recut").set("at", "jeweler").set("npc", kJeweler)
                  .set("zen_by_plus", recut).set("asks", jewel));
    list.push(J::obj().set("key", "jeweler-salvage").set("name", "Salvage").set("at", "jeweler").set("npc", kJeweler)
                  .set("result", salvage).set("odds_by_powers", odds).set("rolls_twice_from_plus", kSalvageTwiceFrom)
                  .set("odds_note", "percent of each prize, a row per count of powers"));
    list.push(J::obj().set("key", "jeweler-combine").set("name", "Combine").set("at", "jeweler").set("npc", kJeweler)
                  .set("zen_by_powers", combine).set("asks", jewel));
    J cloakZen = J::arr(), invZen = J::arr(), invRate = J::arr(), removeZen = J::arr(), socketRate = J::arr();
    for (long long z : kCloakZen) cloakZen.push(J(z));
    for (long long z : kInvitationZen) invZen.push(J(z));
    for (int r : kInvitationRate) invRate.push(r);
    for (long long z : kRemoveRuneZen) removeZen.push(J(z));
    for (int r : kAddSocketRate) socketRate.push(r);
    c.set("kMachineCap", kMachineCap).set("kSecondWingsZen", J((long long)kSecondWingsZen))
        .set("kDinorantHorns", kDinorantHorns).set("kDinorantRate", kDinorantRate).set("kDinorantZen", J((long long)kDinorantZen))
        .set("kCloakRate", kCloakRate).set("kCloakZen", cloakZen).set("kCloakFromLevel", kCloakFromLevel)
        .set("kInvitationZen", invZen).set("kInvitationRate", invRate).set("kInvitationFromLevel", kInvitationFromLevel)
        .set("kPlusLuck", kPlusLuck).set("kPlusCap", kPlusCap).set("kPlusZen", J((long long)kPlusZen))
        .set("kMixZenPerPoint", J((long long)kMixZenPerPoint)).set("kRemoveRuneZen", removeZen)
        .set("kAddSocketRate", socketRate).set("kAddSocketZen", J((long long)kAddSocketZen))
        .set("kFuseZen", J((long long)kFuseZen)).set("kFuseCount", kFuseCount);
    return list;
}

J maps(const Dump& d) {
    J out = J::obj();
    for (const World& w : d.worlds) {
        const MapRow& r = rulesOf(w.tables->map);
        const MapRow* row = mapOf(w.name);
        out[w.name] = J::obj()
                          .set("live", live(d, w)).set("in_packs", d.packs).set("reachable", reachable(w.tables->map))
                          .set("map", w.tables->map).set("in_table", row != nullptr)
                          .set("event", r.event).set("floors", r.floors).set("summons", r.summons).set("feather", r.feather)
                          .set("nova_scroll", r.novaScroll).set("invasion", r.invasion).set("firecracker", r.firecracker)
                          .set("treasure", int(r.treasure)).set("flight", r.flight).set("first_wings", r.firstWings)
                          .set("second_gear", r.secondGear).set("second_class", r.secondClass).set("home", r.home)
                          .set("elites", elitesOn(w.tables->map)).set("monsters", int(w.tables->nests.size()))
                          .set("castles_built", eventOf(w.tables->map) == EventKind::Castle ? J(kCastlesBuilt) : J())
                          .set("squares_built", eventOf(w.tables->map) == EventKind::Square ? J(kSquaresBuilt) : J());
    }
    return out;
}

}  // namespace

J dumpRules(const Dump& d) {
    // What is live by the game's own flags: an Ancient rune falls only from a kill of kAncientLevel.
    int hardest = 0;
    std::string where;
    for (const World& w : d.worlds) {
        if (!live(d, w)) continue;  // a map nobody can get to drops nothing anybody gets
        for (const content::MonsterNest& nest : w.tables->nests) {
            if (nest.kind < w.tables->kinds.size() && w.tables->kinds[nest.kind].level > hardest) {
                hardest = w.tables->kinds[nest.kind].level;
                where = w.name;
            }
        }
    }
    ancientsDrop = hardest >= kAncientLevel;
    J c = J::obj();
    runeConstants(c);
    c.set("kQuestOption", kQuestOption).set("kRebirthLevel", kRebirthLevel).set("kRebirthZen", J((long long)kRebirthZen))
        .set("kCounter", J(double(kCounter)));
    J out = d.stamped();
    out.set("runes", runes()).set("quests", quests()).set("npcs", npcs(d)).set("elites", elites(d))
        .set("mixes", mixes(d, c)).set("boxes", dumpBoxes(d)).set("maps", maps(d)).set("constants", c)
        .set("look", lookJson());
    J worlds = J::arr();
    for (const World& w : d.worlds) worlds.push(w.name);
    out.set("live", J::obj()
                        .set("kAncientLevel", kAncientLevel).set("hardest_nesting_monster", hardest)
                        .set("hardest_in", where).set("ancient_runes_drop", ancientsDrop)
                        .set("in_packs", d.packs).set("worlds", worlds));
    out.set("notes", J::arr());
    out["notes"].push("npcs: role is what talking opens first, Realm::talk's order; services all it does");
    out["notes"].push("quests: paid every completion unless first_only (a native's first clear); gear comes lucky "
                      "with the kQuestOption option (Realm::completeQuest)");
    out["notes"].push("not here: minimap.cpp's chart constants and talent_window.cpp's cell arithmetic live in the "
                      "game's .cpp files, in no header the dump can include without the renderer");
    return out;
}

}  // namespace armory
