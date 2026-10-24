// skills.json, talents.json, gear_powers.json: the sim's own tables, read through its own
// functions. Moved in from tools/armory/prototype/sysb/dump.cpp.

#include "dump.h"
#include "sim/items.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/talents.h"

namespace armory {
namespace {

// A spread a later release added (HEAD's Frustum) is named by its number.
std::string spreadName(sim::Spread s) {
    switch (s) {
        case sim::Spread::One: return "one";
        case sim::Spread::Ring: return "ring";
        case sim::Spread::Arc: return "arc";
        case sim::Spread::Line: return "line";
        case sim::Spread::Fan: return "fan";
        case sim::Spread::Beam: return "beam";
        default: break;
    }
    return "spread-" + std::to_string(int(s));
}

// A skill's fields a later release added, where this one has them (a compile-time question, so
// the dump builds against the release's src/ and HEAD's alike).
template <class R>
void laterFields(J& o, const R& r) {
    if constexpr (requires { r.arcHalfAngle(); }) o.set("arcHalfDeg", J(double(r.arcHalfAngle() * 57.29578f)));
    if constexpr (requires { r.cutTicks; }) o.set("cutTicks", r.cutTicks);
    if constexpr (requires { r.cutChance; }) o.set("cutChance", J(double(r.cutChance)));
    if constexpr (requires { r.cutShare; }) o.set("cutShare", r.cutShare);
}

J kinMap(int (*f)(sim::Kin)) {
    J o = J::obj();
    for (sim::Kin k : kKins) o.set(kinName(k), f(k));
    return o;
}

}  // namespace

const char* kinName(sim::Kin k) {
    switch (k) {
        case sim::Kin::DarkWizard: return "DW";
        case sim::Kin::FairyElf: return "Elf";
        case sim::Kin::DarkKnight: return "DK";
        case sim::Kin::MagicGladiator: return "MG";
    }
    return "?";
}

J dumpSkills(const Dump& d) {
    using namespace sim;
    const content::Tables& tables = d.items();
    J c = J::obj();
    c.set("kAgilityPerDoubling", J(double(kAgilityPerDoubling))).set("kKnightSkillBoost", J(double(kKnightSkillBoost)))
        .set("kTwoHandBoost", J(double(kTwoHandBoost))).set("kGuardCap", J(double(kGuardCap)))
        .set("kSpellQuicken", J(double(kSpellQuicken))).set("kIceQuicken", J(double(kIceQuicken)))
        .set("kCooldownSkillHaste", J(double(kCooldownSkillHaste))).set("kLineTiles", J(double(kLineTiles)))
        .set("kBeamTiles", J(double(kBeamTiles))).set("kPoisonEvery", kPoisonEvery).set("kBurnEvery", kBurnEvery)
        .set("kChillFactor", J(double(kChillFactor))).set("kSummonCool", kSummonCool)
        .set("kLightningBodies", kLightningBodies).set("kShowerRocks", kShowerRocks)
        .set("kPierceTiles", J(double(kPierceTiles))).set("kVolleyLanes", kVolleyLanes)
        .set("kNovaFullEnergy", kNovaFullEnergy);
    J skills = J::arr();
    for (int i = 0; i < skillCount(); ++i) {
        const SkillRow& r = skillAt(i);
        J kins = J::arr();
        for (Kin k : kKins) {
            if (skillFor(r, k)) kins.push(kinName(k));
        }
        const content::PlayerAction* clip = tables.action(r.clip);
        const int cast = authoredCastTicks(tables, r);
        J o = J::obj();
        o.set("index", i).set("number", r.number).set("name", r.name).set("kin", kinName(r.kin)).set("classes", kins)
            .set("second", secondSkill(r)).set("mana", r.mana).set("reach", J(double(r.reach)))
            .set("force", J(double(r.force))).set("forcePerStrength", J(double(r.forcePerStrength)))
            .set("coolTicks", r.coolTicks).set("knock", r.knock).set("spread", spreadName(r.spread))
            .set("boonTicks", r.boonTicks).set("damageTaken", J(double(r.damageTaken))).set("tells", r.tells)
            .set("clip", r.clip).set("clipKeys", clip ? clip->keys : 0).set("clipSpeed", J(double(clip ? clip->speed : 0.0f)))
            .set("authoredCastTicks", cast).set("built", r.built).set("families", unsigned(r.families))
            .set("familiesListed", familiesListed(r.families)).set("needLevel", r.needLevel)
            .set("wizardry", r.wizardry).set("damage", r.damage).set("flies", J(double(r.flies))).set("pushes", r.pushes)
            .set("channelTicks", r.channelTicks).set("pulseTicks", r.pulseTicks).set("strikeFrom", r.strikeFrom)
            .set("strikeUntil", r.strikeUntil).set("strikesEach", r.strikesEach).set("fallTicks", r.fallTicks)
            .set("splash", J(double(r.splash))).set("blinks", r.blinks).set("chillTicks", r.chillTicks)
            .set("poisonTicks", r.poisonTicks).set("anyHand", r.anyHand).set("mends", r.mends)
            .set("mightTicks", r.mightTicks).set("arrows", r.arrows).set("summons", r.summons).set("burns", r.burns)
            .set("burnTiles", J(double(r.burnTiles))).set("walks", J(double(r.walks))).set("mounted", r.mounted)
            .set("chargeTicks", r.chargeTicks).set("chargeStages", r.chargeStages).set("summonsAs", r.summonsAs)
            .set("airborne", r.airborne)
            .set("primary", r.primary()).set("onSelf", r.onSelf()).set("aids", r.aids()).set("castsBare", r.castsBare())
            .set("aimsAtPointer", r.aimsAtPointer()).set("channelled", r.channelled())
            .set("floorTicks", floorTicksFor(r, cast));
        laterFields(o, r);
        skills.push(o);
    }
    J out = d.stamped();
    out.set("constants", c).set("skills", skills);
    return out;
}

J dumpTalents(const Dump& d) {
    using namespace sim;
    J trees = J::obj();
    for (Kin k : kKins) {
        const TalentTree tree = talentTree(k);
        J cols = J::arr();
        for (int c = 0; c < tree.count; ++c) {
            const TalentColumn& col = tree.columns[c];
            J feeds = J::arr();
            for (int f = 0; f < 6; ++f) feeds.push(col.feeds[f]);
            J nodes = J::arr();
            for (int n = 0; n < kTalentNodes; ++n) {
                const TalentNode& node = col.nodes[n];
                const TalentEffect& e = node.effect;
                J skills = J::arr();
                for (int s = 0; s < 5; ++s) skills.push(e.skills[s]);
                J o = J::obj();
                o.set("name", node.name).set("text", node.text).set("icon", node.icon).set("stat", int(e.stat))
                    .set("amount", J(double(e.amount))).set("hold", int(e.hold)).set("elements", int(e.elements))
                    .set("skills", skills).set("special", int(e.special)).set("chance", e.chance)
                    .set("seconds", e.seconds).set("wizardry", e.wizardry);
                nodes.push(o);
            }
            cols.push(J::obj().set("name", col.name).set("sub", col.sub).set("feeds", feeds).set("nodes", nodes));
        }
        trees[kinName(k)] = J::obj().set("columns", cols);
    }
    for (Kin k : kKins) {
        J byLevel = J::obj();
        for (int level : {1, 50, 100, 150, 200, 250, 300, 350, 400}) {
            J a = J::arr();
            for (int resets = 0; resets <= 2; ++resets) a.push(talentPoints(k, level, resets));
            byLevel[std::to_string(level)] = a;
        }
        trees[kinName(k)].set("points", byLevel);
    }
    J c = J::obj();
    c.set("kMaximumLevel", kMaximumLevel).set("kStatPointsPerTalent", kStatPointsPerTalent)
        .set("kTalentMajorOpensAt", kTalentMajorOpensAt).set("kTalentSmallMost", kTalentSmallMost)
        .set("pointsPerLevel", kinMap([](Kin k) { return pointsPerLevel(k); }));
    J out = d.stamped();
    out.set("constants", c).set("classes", trees)
        .set("points_note", "talent points by level, at 0, 1 and 2 rebirths (sim::talentPoints)");
    return out;
}

J dumpGearPowers(const Dump& d) {
    using namespace sim;
    const content::Tables& tables = d.items();
    J items = J::obj();
    for (const content::ItemRow& row : tables.items) {
        J o = J::obj();
        const Needs n = asks(row, 0);
        o.set("label", row.label).set("group", row.group).set("number", row.number).set("dropLevel", row.dropLevel)
            .set("classes", row.classes)
            .set("needs", J::obj().set("level", n.level).set("strength", n.strength).set("agility", n.agility)
                              .set("energy", n.energy).set("vitality", n.vitality))
            .set("defense", row.defense).set("durability", row.durability).set("tells", row.tells);
        if (row.group == 13 || row.group == 12 || row.teaches) {
            const PetPower p = petPower(row);
            o.set("pet", J::obj().set("taken", J(double(p.taken))).set("dealt", J(double(p.dealt))).set("health", p.health)
                             .set("lifeCost", p.lifeCost).set("wear", J(double(p.wear))).set("mount", p.mount)
                             .set("timed", timedPet(row)).set("seconds", J((long long)petSeconds(row))));
            if (anyWing(row)) {
                J byPlus = J::arr();
                for (int plus = 0; plus <= 15; ++plus) {
                    const PetPower w = wingPower(row, plus);
                    byPlus.push(J::obj().set("plus", plus).set("dealt", J(double(w.dealt))).set("taken", J(double(w.taken)))
                                    .set("defense", wingDefense(row, plus)).set("lifeCost", w.lifeCost)
                                    .set("needLevel", asks(row, plus).level));
                }
                const char* names[] = {"regeneration", "damage", "wizardry"};
                J options = J::arr();
                options.push(names[int(wingOption(row, 0))]);
                options.push(names[int(wingOption(row, kWingOptionKind))]);
                o.set("wing", J::obj().set("level", secondWing(row) ? 2 : 1).set("by_plus", byPlus).set("options", options));
            }
            if (jewellery(row)) {
                o.set("jewellery", J::obj().set("ring", ring(row)).set("element", int(elementOf(row)))
                                       .set("signature", int(signatureOf(row))));
            }
        }
        if (row.teaches) {
            const content::ItemRow r = asRead(row, 0);
            o.set("teaches", r.teaches).set("teachesLevel", r.teachesLevel).set("teachesEnergy", r.teachesEnergy);
            if (summoningOrb(row)) {
                J byPlus = J::arr();
                for (int plus = 0; plus <= 6; ++plus) {
                    const content::ItemRow s = asRead(row, plus);
                    byPlus.push(J::obj().set("plus", plus).set("teaches", s.teaches).set("label", s.label)
                                    .set("energy", s.teachesEnergy).set("level", s.teachesLevel));
                }
                o.set("by_plus", byPlus);
            }
        }
        items[row.name] = o;
    }
    J affixes = J::arr();
    for (int a = 1; a <= kAffixes; ++a) {
        J byPlus = J::arr();
        for (int p = 0; p <= 11; ++p) byPlus.push(affixValue(Affix(a), p));
        affixes.push(J::obj().set("id", a).set("name", affixName(Affix(a))).set("element", int(affixElement(Affix(a))))
                         .set("by_plus", byPlus));
    }
    J fury = J::arr(), sting = J::arr();
    for (int p = 0; p <= 11; ++p) {
        fury.push(furyChance(p));
        sting.push(J(ringStingChance(p)));
    }
    J share = J::arr(), shareLevel = J::arr();
    for (int i = 0; i < 4; ++i) {
        share.push(J(kAffixCountShare[i]));
        shareLevel.push(kAffixCountLevel[i]);
    }
    J c = J::obj();
    c.set("kRefineCap", kRefineCap).set("kJewelleryPlusShare", J(kJewelleryPlusShare)).set("kAffixCountShare", share)
        .set("kAffixCountLevel", shareLevel).set("kDeepJewellery", J(kDeepJewellery))
        .set("kDeepJewelleryFrom", kDeepJewelleryFrom).set("kResistanceCut", J(kResistanceCut))
        .set("kResistanceCutMost", J(kResistanceCutMost)).set("kRingStingChance", J(kRingStingChance))
        .set("kRingStingChancePerPlus", J(kRingStingChancePerPlus)).set("kRingStingChanceMost", J(kRingStingChanceMost))
        .set("kRingPulseShare", J(kRingPulseShare)).set("kJewelleryMostPlus", kJewelleryMostPlus)
        .set("kWingExtraBase", kWingExtraBase).set("kWingExtraPerPlus", kWingExtraPerPlus)
        .set("kWingIgnoreChance", J(kWingIgnoreChance)).set("kSpiritSeconds", J((long long)kSpiritSeconds))
        .set("kHornSeconds", J((long long)kHornSeconds)).set("kWingWearTicks", J((long long)kWingWearTicks))
        .set("kWingWearSteps", J(kWingWearSteps)).set("kIcarusWingOdds", kIcarusWingOdds);
    J out = d.stamped();
    out.set("constants", c).set("items", items).set("affixes", affixes).set("furyChance", fury).set("ringSting", sting);
    return out;
}

}  // namespace armory
