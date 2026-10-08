#include "sim/cradle.h"

#include <algorithm>

#include "core/log.h"
#include "sim/realm.h"

namespace mu::sim {

const char* cradleWeapon(Kin kin) {
    switch (kin) {
        case Kin::DarkWizard: return "Staff01";  // the Skull Staff: ours, see game/roster.h
        case Kin::FairyElf: return "Bow01";
        case Kin::DarkKnight: return "Axe01";
        // The Short Sword (the user, 2026-10-07: 'mg will start with short sword').
        case Kin::MagicGladiator: return "Sword02";
    }
    return "";
}

const char* homeWorld(Kin kin) { return kin == Kin::FairyElf ? "noria" : "lorencia"; }

bool goodName(const std::string& name) {
    if (name.size() < size_t(kNameFewest) || name.size() > size_t(kNameMost)) return false;
    for (const char c : name) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && !(c >= '0' && c <= '9')) return false;
    }
    return true;
}


namespace {

const content::Arm* armAt(const content::Tables& tables, int32_t index) {
    return index >= 0 && size_t(index) < tables.arms.size() ? &tables.arms[size_t(index)] : nullptr;
}

// His points into what the arms ask, strength then agility, out of what he has in hand.
void spendFor(Realm& realm, int32_t held, int32_t worn) {
    if (realm.hero().pointsInHand <= 0) return;
    const content::Tables& tables = *realm.tables();
    int points = realm.hero().pointsInHand;
    int wantsStrength = 0, wantsAgility = 0;
    for (const int32_t index : {held, worn}) {
        if (const content::Arm* one = armAt(tables, index)) {
            wantsStrength = std::max(wantsStrength, one->wantsStrength);
            wantsAgility = std::max(wantsAgility, one->wantsAgility);
        }
    }
    const int intoStrength =
        std::min(points, std::max(0, wantsStrength - realm.hero().points.strength));
    points -= intoStrength;
    const int intoAgility =
        std::min(points, std::max(0, wantsAgility - realm.hero().points.agility));
    if (intoStrength + intoAgility > 0) realm.spend(intoStrength, intoAgility, 0, 0);
}

}  // namespace

bool outfit(Realm& realm, const std::string& weapon, const std::string& shield) {
    const content::Tables& tables = *realm.tables();
    const int32_t held = weapon.empty() ? -1 : tables.armNamed(weapon);
    const int32_t worn = shield.empty() ? -1 : tables.armNamed(shield);
    if ((!weapon.empty() && held < 0) || (!shield.empty() && worn < 0)) {
        // His points are still spent for what could be found, as they always were.
        spendFor(realm, held, worn);
        core::logError("no arm called %s%s%s", weapon.c_str(), shield.empty() ? "" : " or ",
                       shield.c_str());
        return false;
    }
    return outfitArms(realm, held, worn);
}

bool outfitArms(Realm& realm, int32_t held, int32_t worn) {
    const content::Tables& tables = *realm.tables();
    if (!armAt(tables, held)) held = -1;
    if (!armAt(tables, worn)) worn = -1;
    spendFor(realm, held, worn);
    if (held < 0 && worn < 0) return true;
    const std::string weapon = held >= 0 ? armAt(tables, held)->name : std::string();
    const std::string shield = worn >= 0 ? armAt(tables, worn)->name : std::string();
    if (realm.equip(held, worn)) {
        core::logf("play: holding %s%s%s -- damage %d to %d, defence %d, a swing every %d ms "
                   "(%d ticks)", weapon.c_str(), shield.empty() ? "" : " and ", shield.c_str(),
                   realm.hero().stats.minimumDamage, realm.hero().stats.maximumDamage,
                   realm.hero().stats.defense, realm.hero().swingMs, realm.hero().swingTicks);
        return true;
    }
    const std::string why = realm.refusal();
    if (!realm.equip(held, worn, true)) {
        core::logError("he cannot hold that: %s", realm.refusal().c_str());
    } else {
        core::logf("play: %s -- given anyway, as a new character is given what his class starts "
                   "with", why.c_str());
    }
    return true;
}

}  // namespace mu::sim
