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
    {1.0f, 0.5f, 0.0f},    // 0  everything not named
    {1.0f, 0.2f, 0.0f},    // 1
    {0.0f, 0.5f, 1.0f},    // 2
    {0.0f, 0.5f, 1.0f},    // 3
    {0.0f, 0.8f, 0.4f},    // 4
    {1.0f, 1.0f, 1.0f},    // 5
    {0.6f, 0.8f, 0.4f},    // 6
};

// Which colour: the named models first, then the armour by its set, as PartObjectColor asks.
int colourOf(int group, int number) {
    if (group == 3 && number == 9) return 1;                     // MODEL_BILL_OF_BALROG
    if (group == 4 && (number == 5 || number == 13)) return 5;   // SILVER_BOW, BLUEWING_CROSSBOW
    if (group == 0 && number == 14) return 2;                    // MODEL_LIGHTING_SWORD
    if (group == 5 && number == 5) return 2;                     // MODEL_LEGENDARY_STAFF
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
            default: return 0;
        }
    }
    return 0;
}

// The models MuMain draws at a level of their own (ZzzObject.cpp:9531-9650). Group 12 is
// numbered the same in OpenMU and MuMain (docs/mu-scrolls-and-orbs.md).
int levelOf(int group, int number, int plus) {
    if (group == 14 && (number == 13 || number == 14)) return 8;  // JEWEL_OF_BLESS, _SOUL
    if (group == 12 && number == 15) return 8;                    // JEWEL_OF_CHAOS
    if (group == 12 && number == 11) return 0;                    // ORB_OF_SUMMONING
    if (group == 12 && number >= 0 && number <= 2) return 0;      // the wings
    // And the 2nd wings at our numbers: MU forces Level 0 on Spirits, Soul and Dragon with the
    // 1st (ZzzObject.cpp:9562-9569), so a +7 one shows no chrome on the ground or in the bag.
    if (group == 12 && (number == sim::kSpiritsNumber || number == sim::kSoulNumber ||
                        number == sim::kDragonNumber)) {
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
    return look;
}

}  // namespace mu::game
