// source/raid/party.json read into the raid's kits (sim/raid.h). The realm checks a kit is
// legal (Realm::kitRefusal); this only says what the file holds, and refuses a file it cannot
// read whole.
#include "sim/raid_party.h"

#include <cstring>

#include "core/json.h"
#include "core/log.h"
#include "sim/items.h"

namespace mu::sim {
namespace {

int slotNamed(const std::string& name) {
    static const struct {
        const char* name;
        int slot;
    } kSlots[] = {
        {"right", kWeaponRight}, {"left", kWeaponLeft},   {"helm", kHelm},
        {"armour", kArmour},     {"pants", kPants},       {"gloves", kGloves},
        {"boots", kBoots},       {"wings", kWings},       {"pet", kPet},
        {"amulet", kAmulet},     {"ring_right", kRingRight}, {"ring_left", kRingLeft},
        {"mount", kMount},
    };
    for (const auto& one : kSlots) {
        if (name == one.name) return one.slot;
    }
    return -1;
}

bool roleNamed(const std::string& name, RaidRole* out) {
    if (name == "tank") *out = RaidRole::Tank;
    else if (name == "melee") *out = RaidRole::Melee;
    else if (name == "healer") *out = RaidRole::Healer;
    else if (name == "archer") *out = RaidRole::Archer;
    else if (name == "wizard") *out = RaidRole::Wizard;
    else return false;
    return true;
}

bool kinNamed(const std::string& name, Kin* out) {
    if (name == "knight") *out = Kin::DarkKnight;
    else if (name == "elf") *out = Kin::FairyElf;
    else if (name == "wizard") *out = Kin::DarkWizard;
    else if (name == "gladiator") *out = Kin::MagicGladiator;
    else return false;
    return true;
}

}  // namespace

bool readParty(const std::string& path, std::vector<RaiderKit>* out) {
    const core::Json doc = core::parseJsonFile(path);
    const core::Json& party = doc["party"];
    if (party.type != core::Json::Type::Array || party.size() == 0) {
        core::logError("raid: %s holds no party", path.c_str());
        return false;
    }
    out->clear();
    for (size_t i = 0; i < party.size(); ++i) {
        const core::Json& one = party.at(i);
        RaiderKit kit;
        kit.name = one["name"].stringOr("raider");
        if (!roleNamed(one["role"].stringOr(""), &kit.role) ||
            !kinNamed(one["class"].stringOr(""), &kit.kin)) {
            core::logError("raid: %s's role or class is not known", kit.name.c_str());
            return false;
        }
        kit.second = one["second"].boolOr(false);
        kit.level = int(one["level"].numberOr(1));
        const core::Json& points = one["points"];
        kit.points.strength = int(points["strength"].numberOr(0));
        kit.points.agility = int(points["agility"].numberOr(0));
        kit.points.vitality = int(points["vitality"].numberOr(0));
        kit.points.energy = int(points["energy"].numberOr(0));
        kit.potions = int(one["potions"].numberOr(20));
        const core::Json& skills = one["skills"];
        for (size_t s = 0; s < skills.size(); ++s) {
            kit.skills.push_back(int32_t(skills.at(s).numberOr(0)));
        }
        const core::Json& wear = one["wear"];
        for (size_t w = 0; w < wear.size(); ++w) {
            const core::Json& piece = wear.at(w);
            KitPiece put;
            put.slot = slotNamed(piece["slot"].stringOr(""));
            put.item = piece["item"].stringOr("");
            if (put.slot < 0 || put.item.empty()) {
                core::logError("raid: %s wears something with no slot or item", kit.name.c_str());
                return false;
            }
            put.refinement = int(piece["plus"].numberOr(0));
            put.luck = piece["luck"].boolOr(false);
            put.option = int(piece["option"].numberOr(0));
            put.excellent = uint8_t(piece["excellent"].numberOr(0));
            const core::Json& sockets = piece["sockets"];
            put.sockets = uint8_t(std::min<size_t>(sockets.size(), 3));
            for (int s = 0; s < put.sockets; ++s) {
                put.powers[s] = uint8_t(sockets.at(size_t(s)).numberOr(0));
            }
            kit.pieces.push_back(put);
        }
        out->push_back(std::move(kit));
    }
    return true;
}

}  // namespace mu::sim
