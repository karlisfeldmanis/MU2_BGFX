#include "sim/cradle.h"

#include <algorithm>

#include "core/log.h"
#include "sim/realm.h"

namespace mu::sim {

bool outfit(Realm& realm, const std::string& weapon, const std::string& shield) {
    const content::Tables& tables = *realm.tables();
    if (realm.hero().pointsInHand > 0) {
        int points = realm.hero().pointsInHand;
        int wantsStrength = 0, wantsAgility = 0;
        for (const std::string& name : {weapon, shield}) {
            const int32_t index = name.empty() ? -1 : tables.armNamed(name);
            if (index < 0) continue;
            const content::Arm& arm = tables.arms[size_t(index)];
            wantsStrength = std::max(wantsStrength, arm.wantsStrength);
            wantsAgility = std::max(wantsAgility, arm.wantsAgility);
        }
        const int intoStrength =
            std::min(points, std::max(0, wantsStrength - realm.hero().points.strength));
        points -= intoStrength;
        const int intoAgility =
            std::min(points, std::max(0, wantsAgility - realm.hero().points.agility));
        if (intoStrength + intoAgility > 0) realm.spend(intoStrength, intoAgility, 0, 0);
    }
    if (weapon.empty() && shield.empty()) return true;
    const int32_t held = weapon.empty() ? -1 : tables.armNamed(weapon);
    const int32_t worn = shield.empty() ? -1 : tables.armNamed(shield);
    if ((!weapon.empty() && held < 0) || (!shield.empty() && worn < 0)) {
        core::logError("no arm called %s%s%s", weapon.c_str(), shield.empty() ? "" : " or ",
                       shield.c_str());
        return false;
    }
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
