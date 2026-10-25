#include "game/shine.h"

#include "core/log.h"
#include "sim/items.h"

namespace mu::game {

void lendShine(const content::Showing& table, const std::string& assetDir,
               content::Textures& textures, gfx::Renderer& renderer) {
    const content::EffectSheet* chrome = table.effect("chrome");
    const content::EffectSheet* shiny = table.effect("shiny");
    if (!chrome || !shiny) {
        core::logError("the showing has no chrome or shiny sheet; refined items take their "
                       "tint and nothing more");
        return;
    }
    // Chrome02 for an excellent thing, said once when the showing was cooked without it.
    const content::EffectSheet* chrome2 = table.effect("chrome2");
    if (!chrome2) core::logError("the showing has no chrome2 sheet; excellent items do not glow");
    renderer.setShine(textures.load(assetDir + "/" + chrome->path, content::TextureRole::Albedo),
                      textures.load(assetDir + "/" + shiny->path, content::TextureRole::Albedo),
                      chrome2 ? textures.load(assetDir + "/" + chrome2->path,
                                              content::TextureRole::Albedo)
                              : bgfx::TextureHandle BGFX_INVALID_HANDLE);
}

namespace {

// PartObjectColor's own colours, by its number (ZzzObject.cpp:6776). Only the ones a row of
// this game's item table can reach are here.
struct Rgb {
    float r, g, b;
};
constexpr Rgb kColours[] = {
    {1.0f, 0.5f, 0.0f},     // 0  everything not named
    {1.0f, 0.2f, 0.0f},     // 1
    {0.0f, 0.5f, 1.0f},     // 2
    {0.0f, 0.5f, 1.0f},     // 3
    {0.0f, 0.8f, 0.4f},     // 4
    {1.0f, 1.0f, 1.0f},     // 5
    {0.6f, 0.8f, 0.4f},     // 6
    {0.9f, 0.8f, 1.0f},     // 7
    {0.8f, 0.8f, 1.0f},     // 8
    {0.5f, 0.5f, 0.8f},     // 9
    {0.75f, 0.65f, 0.5f},   // 10
    {0.35f, 0.35f, 0.6f},   // 11
    {0.47f, 0.67f, 0.6f},   // 12
    {0.0f, 0.3f, 0.6f},     // 13
    {0.65f, 0.65f, 0.55f},  // 14
    {0.2f, 0.3f, 0.6f},     // 15
    {0.8f, 0.46f, 0.25f},   // 16
    {0.65f, 0.45f, 0.3f},   // 17
    {0.5f, 0.4f, 0.3f},     // 18
    {0.37f, 0.37f, 1.0f},   // 19
    {0.3f, 0.7f, 0.3f},     // 20
    {0.5f, 0.4f, 1.0f},     // 21
    {0.45f, 0.45f, 0.23f},  // 22
    {0.3f, 0.3f, 0.45f},    // 23
    {0.6f, 0.5f, 0.2f},     // 24
    {0.6f, 0.6f, 0.6f},     // 25
    {0.3f, 0.7f, 0.3f},     // 26
    {0.5f, 0.6f, 0.7f},     // 27
    {0.45f, 0.45f, 0.23f},  // 28
    {0.2f, 0.7f, 0.3f},     // 29
    {0.7f, 0.3f, 0.3f},     // 30
    {0.7f, 0.5f, 0.3f},     // 31
    {0.5f, 0.2f, 0.7f},     // 32
    {0.8f, 0.4f, 0.6f},     // 33
    {0.6f, 0.4f, 0.8f},     // 34
    {0.7f, 0.4f, 0.4f},     // 35
    {0.5f, 0.5f, 0.7f},     // 36
    {0.7f, 0.5f, 0.7f},     // 37
    {0.2f, 0.4f, 0.7f},     // 38
    {0.3f, 0.6f, 0.4f},     // 39
    {0.7f, 0.2f, 0.2f},     // 40
    {0.7f, 0.2f, 0.7f},     // 41
    {0.8f, 0.4f, 0.0f},     // 42
};

// Which colour: the named models first, then the armour by its set, as PartObjectColor asks.
int colourOf(int group, int number) {
    if (group == 3 && number == 9) return 1;                     // MODEL_BILL_OF_BALROG
    if (group == 4 && (number == 5 || number == 13)) return 5;   // SILVER_BOW, BLUEWING_CROSSBOW
    if (group == 0 && number == 14) return 2;                    // MODEL_LIGHTING_SWORD
    if (group == 5 && number == 5) return 2;                     // MODEL_LEGENDARY_STAFF
    // The second-class arms (2026-10-10: they came after this table was cut, and drew orange).
    if (group == 4 && (number == 17 || number == 19)) return 9;  // CELESTIAL_BOW, GREAT_REIGN_CROSSBOW
    if (group == 4 && number == 18) return 10;                   // DIVINE_CB_OF_ARCHANGEL
    if (group == 4 && number == 20) return 16;                   // ARROW_VIPER_BOW
    if (group == 5 && number == 9) return 5;                     // DRAGON_SOUL_STAFF
    if (group == 5 && number == 11) return 17;                   // STAFF_OF_KUNDUN
    if (group == 6 && number == 16) return 6;                    // ELEMENTAL_SHIELD
    if (group == 3 && number == 10) return 9;                    // DRAGON_SPEAR
    if (group == 0 && number == 20) return 10;                   // KNIGHT_BLADE
    if (group == 0 && number == 21) return 5;                    // DARK_REIGN_BLADE
    if (group == 2 && number == 8) return 9;                     // BATTLE_SCEPTER (ZzzObject.cpp:6584)
    if (group == 2 && number == 9) return 10;                    // MASTER_SCEPTER (:6588)
    if (group == 2 && number == 10) return 12;                   // GREAT_SCEPTER (:6592); LORD's 0
    if (group == 2 && number == 12) return 16;                   // GREAT_LORD_SCEPTER (:6612)
    if (group == 2 && number == 15) return 28;                   // SHINING_SCEPTER (:6648)
    if (group == 2 && number == 14) return 22;                   // SOLEIL_SCEPTER (:6632)
    if (group == 2 && number == 17) return 40;                   // ABSOLUTE_SCEPTER (:6678)
    if (group == 6 && number == 21) return 30;                   // CROSS_SHIELD (:6686)
    if (group >= 7 && group <= 11) {
        switch (number) {
            case 1: return 1;    // Dragon
            case 3: return 3;    // Legendary
            case 4: return 5;    // Bone
            case 6: return 6;    // Scale
            case 9: return 2;    // Plate
            case 12: return 2;   // Wind
            case 13: return 4;   // Spirit
            case 14: return 5;   // Guardian
            case 15: return 7;   // Storm Crow
            case 16: return 10;  // Black Dragon
            case 17: return 9;   // Dark Phoenix
            case 18: return 5;   // Grand Soul
            case 19: return 9;   // Divine
            case 20: return 9;   // Thunder Hawk
            case 21: return 16;  // Great Dragon
            case 22: return 17;  // Dark Soul
            case 23: return 11;  // Hurricane
            case 24: return 16;  // Red Spirit
            case 25: return 11;  // Light Plate (ZzzObject.cpp:6739)
            // The later sets (2026-10-25: past 25 they drew orange), MU's switch on (:6745-6766).
            case 26: return 12;  // Adamantine
            case 27: return 10;  // Dark Steel
            case 28: return 15;  // Dark Master
            case 29: return 18;  // Dragon Knight
            case 30: return 19;  // Venom Mist
            case 31: return 20;  // Sylphid Ray
            case 32: return 21;  // Volcano
            case 33: return 22;  // Sunlight
            case 34: return 24;  // Ashcrow
            case 35: return 25;  // Eclipse
            case 36: return 26;  // Iris
            case 37: return 27;  // Valiant
            case 38: return 28;  // Glorious
            case 39: return 29;
            case 40: return 30;
            case 41: return 31;
            case 42: return 32;
            case 43: return 33;
            case 44: return 34;
            case 45: return 36;  // Titan
            case 46: return 42;  // Brave
            case 47: return 37;  // Destroy
            case 48: return 1;   // Phantom
            case 49: return 35;  // Seraphim
            case 50: return 39;  // Faith
            case 51: return 40;  // Paewang
            case 52: return 36;  // Hades
            case 53: return 41;
            default: return 0;
        }
    }
    return 0;
}

// The +11 sweep's colour, PartObjectColor2's (ZzzObject.cpp:6827-6904): named weapons, then the
// armour by its set.
int sweepOf(int group, int number) {
    if (group == 4 && (number == 5 || number == 13)) return 2;  // SILVER_BOW, BLUEWING_CROSSBOW
    if (group == 0 && number == 14) return 2;                   // MODEL_LIGHTING_SWORD
    if (group == 5 && number == 5) return 2;                    // MODEL_LEGENDARY_STAFF
    if (group >= 7 && group <= 11) {
        switch (number) {
            case 4: case 14: case 15: case 17: case 39: case 40: case 41: case 42: return 1;
            case 18: case 43: return 2;
            case 21: case 44: return 3;
            default: return 0;
        }
    }
    return 0;
}

// The models MuMain draws at a level of their own (ZzzObject.cpp:9531-9650). Group 12 is
// numbered the same in OpenMU and MuMain (docs/reference/mu-scrolls-and-orbs.md).
int levelOf(int group, int number, int plus) {
    if (group == 14 && (number == 13 || number == 14)) return 8;  // JEWEL_OF_BLESS, _SOUL
    if (group == 12 && number == 15) return 8;                    // JEWEL_OF_CHAOS
    if (group == 12 && number == 11) return 0;                    // ORB_OF_SUMMONING
    if (group == 12 && number >= 0 && number <= 2) return 0;      // the wings
    // And the 2nd wings at our numbers: MU forces Level 0 on Spirits, Soul, Dragon and Darkness
    // with the 1st (ZzzObject.cpp:9562-9569), so a +7 one shows no chrome on the ground or in
    // the bag.
    if (group == 12 && (number == sim::kSpiritsNumber || number == sim::kSoulNumber ||
                        number == sim::kDragonNumber || number == sim::kDarknessNumber)) {
        return 0;
    }
    // MuMain's later orbs at +9 are left out: a knight orb here sits on 7, 12 or 19 by
    // borrowing that number, or on a free one, and drew the chrome by which it happened to
    // be. One family, one look; 0.75's own orbs (8-10) draw at their plus.
    // MODEL_BOLT and MODEL_ARROWS: a quiver's plus is drawn doubled and one more.
    if (group == 4 && (number == 7 || number == 15)) return plus >= 1 ? plus * 2 + 1 : 0;
    return plus;
}

}  // namespace

ShineLook shineOf(const content::ItemRow& row, int plus, bool excellent) {
    ShineLook look;
    look.excellent = excellent;
    look.level = levelOf(row.group, row.number, plus);
    const Rgb& c = kColours[colourOf(row.group, row.number)];
    look.colour[0] = c.r;
    look.colour[1] = c.g;
    look.colour[2] = c.b;
    look.sweep = sweepOf(row.group, row.number);
    const bool gladiators = row.group >= 8 && row.group <= 11;
    look.hurricane = gladiators && row.number == 23;                     // Hurricane
    look.crow = gladiators && (row.number == 15 || look.hurricane);      // Storm Crow
    return look;
}

}  // namespace mu::game
