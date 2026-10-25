// Dumps the game's own tables (sim skills, talents, item powers) as JSON, compiled against the
// repo's sources read-only. Built and run by gen_systems_b.py.
#include <cmath>
#include <cstdio>
#include <string>

#include "content/tables.h"
#include "sim/items.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/talents.h"

using namespace mu;
using namespace mu::sim;

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if ((unsigned char)c < 0x20) o += ' ';
        else o += c;
    }
    return o;
}
static std::string q(const char* s) { return "\"" + esc(s ? s : "") + "\""; }
static std::string q(const std::string& s) { return "\"" + esc(s) + "\""; }

static const char* kinName(Kin k) {
    switch (k) {
        case Kin::DarkWizard: return "DW";
        case Kin::FairyElf: return "Elf";
        case Kin::DarkKnight: return "DK";
        case Kin::MagicGladiator: return "MG";
    }
    return "?";
}
static const char* spreadName(Spread s) {
    switch (s) {
        case Spread::One: return "one";
        case Spread::Ring: return "ring";
        case Spread::Arc: return "arc";
        case Spread::Line: return "line";
        case Spread::Fan: return "fan";
        case Spread::Beam: return "beam";
    }
    return "?";
}

int main(int argc, char** argv) {
    content::Tables tables;
    std::string error;
    if (argc < 2 || !content::loadTables(argv[1], tables, error)) {
        std::fprintf(stderr, "tables: %s\n", error.c_str());
        return 1;
    }
    std::printf("{\n\"constants\": {\"kAgilityPerDoubling\": %g, \"kKnightSkillBoost\": %g, \"kTwoHandBoost\": %g,"
                " \"kGuardCap\": %g, \"kSpellQuicken\": %g, \"kIceQuicken\": %g, \"kCooldownSkillHaste\": %g,"
                " \"kLineTiles\": %g, \"kBeamTiles\": %g, \"kPoisonEvery\": %d, \"kBurnEvery\": %d,"
                " \"kChillFactor\": %g, \"kSummonCool\": %d, \"kLightningBodies\": %d, \"kShowerRocks\": %d,"
                " \"kPierceTiles\": %g, \"kVolleyLanes\": %d, \"kNovaFullEnergy\": %d,"
                " \"kMaximumLevel\": %d, \"kStatPointsPerTalent\": %d, \"kTalentMajorOpensAt\": %d,"
                " \"kTalentSmallMost\": %d, \"pointsPerLevel\": {\"DK\": %d, \"DW\": %d, \"Elf\": %d, \"MG\": %d},"
                " \"kRefineCap\": %d, \"kJewelleryPlusShare\": %g, \"kAffixCountShare\": [%g,%g,%g,%g],"
                " \"kAffixCountLevel\": [%d,%d,%d,%d], \"kDeepJewellery\": %g, \"kDeepJewelleryFrom\": %d,"
                " \"kResistanceCut\": %g, \"kResistanceCutMost\": %g, \"kRingStingChance\": %g,"
                " \"kRingStingChancePerPlus\": %g, \"kRingStingChanceMost\": %g, \"kRingPulseShare\": %g,"
                " \"kJewelleryMostPlus\": %d, \"kWingExtraBase\": %d, \"kWingExtraPerPlus\": %d,"
                " \"kWingIgnoreChance\": %g, \"kSpiritSeconds\": %lld, \"kHornSeconds\": %lld,"
                " \"kWingWearTicks\": %lld, \"kWingWearSteps\": %g, \"kIcarusWingOdds\": %d},\n",
                kAgilityPerDoubling, kKnightSkillBoost, kTwoHandBoost, kGuardCap, kSpellQuicken, kIceQuicken,
                kCooldownSkillHaste, kLineTiles, kBeamTiles, kPoisonEvery, kBurnEvery, kChillFactor, kSummonCool,
                kLightningBodies, kShowerRocks, kPierceTiles, kVolleyLanes, kNovaFullEnergy, kMaximumLevel,
                kStatPointsPerTalent, kTalentMajorOpensAt, kTalentSmallMost, pointsPerLevel(Kin::DarkKnight),
                pointsPerLevel(Kin::DarkWizard), pointsPerLevel(Kin::FairyElf),
                pointsPerLevel(Kin::MagicGladiator), kRefineCap, kJewelleryPlusShare, kAffixCountShare[0],
                kAffixCountShare[1], kAffixCountShare[2], kAffixCountShare[3], kAffixCountLevel[0],
                kAffixCountLevel[1], kAffixCountLevel[2], kAffixCountLevel[3], kDeepJewellery,
                kDeepJewelleryFrom, kResistanceCut, kResistanceCutMost, kRingStingChance,
                kRingStingChancePerPlus, kRingStingChanceMost, kRingPulseShare, kJewelleryMostPlus,
                kWingExtraBase, kWingExtraPerPlus, kWingIgnoreChance, (long long)kSpiritSeconds,
                (long long)kHornSeconds, (long long)kWingWearTicks, kWingWearSteps, kIcarusWingOdds);

    // ---- skills
    std::printf("\"skills\": [\n");
    for (int i = 0; i < skillCount(); ++i) {
        const SkillRow& r = skillAt(i);
        std::string kins;
        for (Kin k : {Kin::DarkKnight, Kin::DarkWizard, Kin::FairyElf, Kin::MagicGladiator}) {
            if (skillFor(r, k)) kins += std::string(kins.empty() ? "" : ",") + q(kinName(k));
        }
        const content::PlayerAction* clip = tables.action(r.clip);
        std::printf(
            "{\"index\": %d, \"number\": %d, \"name\": %s, \"kin\": %s, \"classes\": [%s], \"second\": %s,"
            " \"mana\": %d, \"reach\": %g, \"force\": %g, \"forcePerStrength\": %g, \"coolTicks\": %d,"
            " \"knock\": %s, \"spread\": %s, \"boonTicks\": %d, \"damageTaken\": %g, \"tells\": %s,"
            " \"clip\": %d, \"clipKeys\": %d, \"clipSpeed\": %g, \"authoredCastTicks\": %d, \"built\": %s,"
            " \"families\": %u, \"familiesListed\": %s, \"needLevel\": %d,"
            " \"wizardry\": %s, \"damage\": %d, \"flies\": %g, \"pushes\": %s, \"channelTicks\": %d,"
            " \"pulseTicks\": %d, \"strikeFrom\": %d, \"strikeUntil\": %d, \"strikesEach\": %d,"
            " \"fallTicks\": %d, \"splash\": %g, \"blinks\": %s, \"chillTicks\": %d, \"poisonTicks\": %d,"
            " \"anyHand\": %s, \"mends\": %s, \"mightTicks\": %d, \"arrows\": %d, \"summons\": %d,"
            " \"burns\": %d, \"burnTiles\": %g, \"walks\": %g, \"mounted\": %s, \"chargeTicks\": %d,"
            " \"chargeStages\": %d, \"summonsAs\": %d, \"airborne\": %s, \"arcHalfDeg\": %g, \"cutTicks\": %d,"
            " \"cutChance\": %g, \"cutShare\": %d, \"primary\": %s, \"onSelf\": %s, \"aids\": %s,"
            " \"castsBare\": %s, \"aimsAtPointer\": %s, \"channelled\": %s, \"floorTicks\": %d}%s\n",
            i, r.number, q(r.name).c_str(), q(kinName(r.kin)).c_str(), kins.c_str(),
            secondSkill(r) ? "true" : "false", r.mana, r.reach, r.force, r.forcePerStrength, r.coolTicks,
            r.knock ? "true" : "false", q(spreadName(r.spread)).c_str(), r.boonTicks, r.damageTaken,
            q(r.tells).c_str(), r.clip, clip ? clip->keys : 0, clip ? clip->speed : 0.0f,
            authoredCastTicks(tables, r), r.built ? "true" : "false", r.families,
            q(familiesListed(r.families)).c_str(), r.needLevel, r.wizardry ? "true" : "false", r.damage,
            r.flies, r.pushes ? "true" : "false", r.channelTicks, r.pulseTicks, r.strikeFrom, r.strikeUntil,
            r.strikesEach, r.fallTicks, r.splash, r.blinks ? "true" : "false", r.chillTicks, r.poisonTicks,
            r.anyHand ? "true" : "false", r.mends ? "true" : "false", r.mightTicks, r.arrows, r.summons,
            r.burns, r.burnTiles, r.walks, r.mounted ? "true" : "false", r.chargeTicks, r.chargeStages,
            r.summonsAs, r.airborne ? "true" : "false", r.arcHalfAngle() * 57.29578f, r.cutTicks,
            r.cutChance, r.cutShare, r.primary() ? "true" : "false", r.onSelf() ? "true" : "false",
            r.aids() ? "true" : "false", r.castsBare() ? "true" : "false",
            r.aimsAtPointer() ? "true" : "false", r.channelled() ? "true" : "false",
            floorTicksFor(r, authoredCastTicks(tables, r)), i + 1 < skillCount() ? "," : "");
    }
    std::printf("],\n");

    // ---- talents
    std::printf("\"talents\": {\n");
    bool firstKin = true;
    for (Kin k : {Kin::DarkKnight, Kin::DarkWizard, Kin::FairyElf, Kin::MagicGladiator}) {
        const TalentTree tree = talentTree(k);
        std::printf("%s%s: [\n", firstKin ? "" : ",\n", q(kinName(k)).c_str());
        firstKin = false;
        for (int c = 0; c < tree.count; ++c) {
            const TalentColumn& col = tree.columns[c];
            std::printf("{\"name\": %s, \"sub\": %s, \"feeds\": [%d,%d,%d,%d,%d,%d], \"nodes\": [\n",
                        q(col.name).c_str(), q(col.sub).c_str(), col.feeds[0], col.feeds[1], col.feeds[2],
                        col.feeds[3], col.feeds[4], col.feeds[5]);
            for (int n = 0; n < kTalentNodes; ++n) {
                const TalentNode& node = col.nodes[n];
                const TalentEffect& e = node.effect;
                std::printf("{\"name\": %s, \"text\": %s, \"icon\": %d, \"stat\": %d, \"amount\": %g, \"hold\": %d,"
                            " \"elements\": %d, \"skills\": [%d,%d,%d,%d,%d], \"special\": %d, \"chance\": %d,"
                            " \"seconds\": %d, \"wizardry\": %s}%s\n",
                            q(node.name).c_str(), q(node.text).c_str(), node.icon, int(e.stat), e.amount,
                            int(e.hold), int(e.elements), e.skills[0], e.skills[1], e.skills[2], e.skills[3],
                            e.skills[4], int(e.special), e.chance, e.seconds, e.wizardry ? "true" : "false",
                            n + 1 < kTalentNodes ? "," : "");
            }
            std::printf("]}%s\n", c + 1 < tree.count ? "," : "");
        }
        std::printf("]");
    }
    std::printf("},\n");
    std::printf("\"talentPoints\": {");
    firstKin = true;
    for (Kin k : {Kin::DarkKnight, Kin::DarkWizard, Kin::FairyElf, Kin::MagicGladiator}) {
        std::printf("%s%s: {", firstKin ? "" : ", ", q(kinName(k)).c_str());
        firstKin = false;
        const int levels[] = {1, 50, 100, 150, 200, 250, 300, 350, 400};
        for (int li = 0; li < 9; ++li) {
            std::printf("%s\"%d\": [%d, %d, %d]", li ? ", " : "", levels[li], talentPoints(k, levels[li], 0),
                        talentPoints(k, levels[li], 1), talentPoints(k, levels[li], 2));
        }
        std::printf("}");
    }
    std::printf("},\n");

    // ---- items: teaching rows and the powers
    std::printf("\"items\": [\n");
    bool firstItem = true;
    for (const content::ItemRow& row : tables.items) {
        std::string extra;
        char buf[512];
        if (row.group == 13 || row.group == 12 || row.teaches) {
            const PetPower p = petPower(row);
            std::snprintf(buf, sizeof buf,
                          ", \"pet\": {\"taken\": %g, \"dealt\": %g, \"health\": %d, \"lifeCost\": %d, \"wear\": %g,"
                          " \"mount\": %s, \"timed\": %s, \"seconds\": %lld}",
                          p.taken, p.dealt, p.health, p.lifeCost, p.wear, p.mount ? "true" : "false",
                          timedPet(row) ? "true" : "false", (long long)petSeconds(row));
            extra += buf;
            if (anyWing(row)) {
                extra += ", \"wing\": {\"level\": ";
                extra += secondWing(row) ? "2" : "1";
                extra += ", \"by_plus\": [";
                for (int plus = 0; plus <= 15; ++plus) {
                    const PetPower w = wingPower(row, plus);
                    std::snprintf(buf, sizeof buf, "%s{\"plus\": %d, \"dealt\": %g, \"taken\": %g, \"defense\": %d,"
                                  " \"lifeCost\": %d, \"needLevel\": %d}",
                                  plus ? "," : "", plus, w.dealt, w.taken, wingDefense(row, plus), w.lifeCost,
                                  asks(row, plus).level);
                    extra += buf;
                }
                extra += "], \"options\": [";
                const char* names[] = {"regeneration", "damage", "wizardry"};
                std::snprintf(buf, sizeof buf, "%s, %s", q(names[int(wingOption(row, 0))]).c_str(),
                              q(names[int(wingOption(row, kWingOptionKind))]).c_str());
                extra += buf;
                extra += "]}";
            }
            if (jewellery(row)) {
                std::snprintf(buf, sizeof buf, ", \"jewellery\": {\"ring\": %s, \"element\": %d, \"signature\": %d}",
                              ring(row) ? "true" : "false", int(elementOf(row)), int(signatureOf(row)));
                extra += buf;
            }
        }
        if (row.teaches) {
            const content::ItemRow r = asRead(row, 0);
            std::snprintf(buf, sizeof buf, ", \"teaches\": %d, \"teachesLevel\": %d, \"teachesEnergy\": %d", r.teaches,
                          r.teachesLevel, r.teachesEnergy);
            extra += buf;
            if (summoningOrb(row)) {
                extra += ", \"by_plus\": [";
                for (int plus = 0; plus <= 6; ++plus) {
                    const content::ItemRow s = asRead(row, plus);
                    std::snprintf(buf, sizeof buf, "%s{\"plus\": %d, \"teaches\": %d, \"label\": %s, \"energy\": %d, \"level\": %d}",
                                  plus ? "," : "", plus, s.teaches, q(s.label).c_str(), s.teachesEnergy, s.teachesLevel);
                    extra += buf;
                }
                extra += "]";
            }
        }
        const Needs n = asks(row, 0);
        std::printf("%s{\"key\": %s, \"label\": %s, \"group\": %d, \"number\": %d, \"dropLevel\": %d, \"classes\": %d,"
                    " \"needs\": [%d,%d,%d,%d,%d], \"defense\": %d, \"durability\": %d, \"tells\": %s%s}\n",
                    firstItem ? "" : ",", q(row.name).c_str(), q(row.label).c_str(), row.group, row.number,
                    row.dropLevel, row.classes, n.level, n.strength, n.agility, n.energy, n.vitality, row.defense,
                    row.durability, q(row.tells).c_str(), extra.c_str());
        firstItem = false;
    }
    std::printf("],\n");

    // ---- affixes
    std::printf("\"affixes\": [");
    for (int a = 1; a <= kAffixes; ++a) {
        std::printf("%s{\"id\": %d, \"name\": %s, \"element\": %d, \"by_plus\": [", a > 1 ? "," : "", a,
                    q(affixName(Affix(a))).c_str(), int(affixElement(Affix(a))));
        for (int p = 0; p <= 11; ++p) std::printf("%s%d", p ? "," : "", affixValue(Affix(a), p));
        std::printf("]}");
    }
    std::printf("],\n\"furyChance\": [");
    for (int p = 0; p <= 11; ++p) std::printf("%s%d", p ? "," : "", furyChance(p));
    std::printf("],\n\"ringSting\": [");
    for (int p = 0; p <= 11; ++p) std::printf("%s%g", p ? "," : "", ringStingChance(p));
    std::printf("],\n\"force_sample\": {}\n}\n");
    return 0;
}
#include <cstdarg>
namespace mu::core {
void logError(const char* f, ...) { va_list a; va_start(a, f); std::vfprintf(stderr, f, a); va_end(a); std::fputc('\n', stderr); }
}
