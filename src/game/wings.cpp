#include "game/wings.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "content/placement.h"
#include "core/maths.h"
#include "game/shine.h"
#include "sim/items.h"

namespace mu::game {
namespace {

// ZzzCharacter.cpp:15430: RenderLinkObject(0, 0, 15, ...) -- where the wing sits in Bone05's
// frame, in metres. MU's (x, y, z) is the cook's (x, z, -y) in every bone's frame (as
// game/pets.cpp maps), so MU's 15 up is +y here; left as z it pushed the wing 15 cm forward
// into the chest instead of up between the shoulders.
constexpr float kWingOffset[3] = {0.0f, 0.15f, 0.0f};

// Ours: the 2nd wings pulled in against the back (+z is MU's -y, forward). Their joints are
// modelled further behind their origins than the 1st wings' (Satan's nearest 3.5 cm), so on
// MU's own (0, 0, 15) they stood off the back with a gap -- the user, 2026-10-05: 'looks liek
// there is some gap on DK 2nd wings', and at 5 cm 'stlll some minimal gap'. Dragon passed at
// 8 cm, its joint's nearest 6.1 cm; Soul (4.8) and Spirits (16.2, the whole membrane behind)
// take the same rule, the nearest point plus 1.9 cm.
struct WingInward {
    const char* name;
    float metres;
};
constexpr float kHeavenTone = 0.82f;
// And seen through: drawn at this opacity after the opaque frame, its own depth first so its
// far feathers stay hidden (gfx::Drawable::fade) -- the user, 2026-10-05: 'make angel wings
// actual transparent so its very smooth'. Ours; MU's are opaque past the alpha.
constexpr float kHeavenOpacity = 0.75f;
constexpr WingInward kInward[] = {{"Wing04", 0.18f}, {"Wing05", 0.065f}, {"Wing06", 0.08f}};

// The Wings of Darkness's sparks (ZzzObject.cpp:9981-10017). MuMain, every frame, for i 0-4: a
// BITMAP_FLARE_BLUE at bone 22 - i and at bone 7 - i, Light (0.6, 0.3, 0.8), Scale/28 big, Scale
// sin(WorldTime * 0.004) * 3 + 23 (20 to 26); and a BITMAP_JOINT_THUNDER from bone 30 - i, and
// 11 + i, to it, with a BITMAP_JOINT_SPIRIT back. By name here, as the cook keeps MU's names
// and not its order: 22-18 are Bone17, 18, 19, 20, 33; 30-26 Bone14, 13, 12, 11, 32; 7-3
// Bone30, 29, 34, 27, 26; 11-15 Bone03-07.
constexpr const char* kRibs[10][2] = {
    {"Bone17", "Bone14"}, {"Bone18", "Bone13"}, {"Bone19", "Bone12"}, {"Bone20", "Bone11"},
    {"Bone33", "Bone32"}, {"Bone30", "Bone03"}, {"Bone29", "Bone04"}, {"Bone34", "Bone05"},
    {"Bone27", "Bone06"}, {"Bone26", "Bone07"}};
constexpr float kRibLight[3] = {0.6f, 0.3f, 0.8f};
// Both joints are sub-types that live one frame and lay ten tails in a straight line from where
// they start to their target (thunder 14, ZzzEffectJoint.cpp:1239-1271; spirit 4, :688-720),
// each at Light (0.3, 0.3, 1) -- blue, where the flare is violet -- and Scale wide: the thunder
// Scale, the spirit Scale + 5, both added: the spirit's RENDER_TYPE_ALPHA_BLEND is MU's
// EnableAlphaBlend, glBlendFunc(GL_ONE, GL_ONE) (ZzzOpenglUtil.cpp:402) -- drawn with true alpha
// its JPEG, which has none, laid solid blue bands over the shards.
constexpr float kBoltLight[3] = {0.3f, 0.3f, 1.0f};
constexpr float kSpiritWider = 5.0f;  // MU's units
// Ours: a flare's half-width at MU's Scale 20, in metres, and the light all three are added at,
// faint as the monster auras are kept.
constexpr float kRibFlare = 0.10f;
constexpr float kRibGlow = 0.7f;
constexpr float kUnit = 0.01f;  // metres in one of MU's units
bgfx::TextureHandle gFlare = BGFX_INVALID_HANDLE;
bgfx::TextureHandle gThunder = BGFX_INVALID_HANDLE;
bgfx::TextureHandle gSpirit = BGFX_INVALID_HANDLE;

}  // namespace

const FigureBody* wingBody(const Figures& figures, int group, int number) {
    if (group != 12) return nullptr;
    if (number >= 0 && number <= 2) return figures.body("Wing0" + std::to_string(number + 1));
    // And the 2nd wings, Wing04-07 (ZzzOpenData.cpp:1023, `Wing`, 4 + i), at our numbers.
    if (number == sim::kSpiritsNumber) return figures.body("Wing04");
    if (number == sim::kSoulNumber) return figures.body("Wing05");
    if (number == sim::kDragonNumber) return figures.body("Wing06");
    if (number == sim::kDarknessNumber) return figures.body("Wing07");
    return nullptr;
}

void WingLook::wear(const FigureBody* wing, const Figure& bearer) {
    if (wing == wing_) return;
    wing_ = wing;
    tipBones_.clear();
    posed_ = false;
    if (!wing_) return;
    figure_.stand(wing_, bearer.position(), 0.0f, bearer.scale());
    // The tips: every bone no other bone hangs from, MU's dummies aside.
    if (wing_->skeletonMesh) {
        const std::vector<content::Bone>& bones = wing_->skeletonMesh->bones();
        std::vector<uint8_t> parent(bones.size(), 0);
        for (const content::Bone& one : bones) {
            if (one.parent >= 0 && size_t(one.parent) < bones.size()) parent[size_t(one.parent)] = 1;
        }
        for (size_t i = 0; i < bones.size(); ++i) {
            if (!parent[i] && bones[i].parent >= 0 && bones[i].name.rfind("dummy", 0) != 0) {
                tipBones_.push_back(int(i));
            }
        }
    }
    if (wing_->idleClip >= 0) figure_.play(wing_->idleClip, true, 0.0f);
    ribs_.clear();
    if (wing_->name == "Wing07" && wing_->skeletonMesh) {
        const std::vector<content::Bone>& bones = wing_->skeletonMesh->bones();
        const auto named = [&](const char* name) {
            for (size_t i = 0; i < bones.size(); ++i) {
                if (bones[i].name == name) return int(i);
            }
            return -1;
        };
        for (const auto& rib : kRibs) {
            const int centre = named(rib[0]), end = named(rib[1]);
            if (centre >= 0 && end >= 0) ribs_.push_back({centre, end});
        }
    }
}

void WingLook::lendSparks(const content::Showing& table, const std::string& assetDir,
                          content::Textures& textures) {
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    gFlare = take("flare_blue");
    gThunder = take("joint_thunder");
    gSpirit = take("joint_spirit");
}

void WingLook::sparks(gfx::Effects& effects) {
    if (ribs_.empty() || !posed_ || !bgfx::isValid(gFlare) || !bgfx::isValid(gThunder)) return;
    // MU's Scale: 20 to 26 units on sin(WorldTime * 0.004).
    const float scale = std::sin(clock_ * 4.0f) * 3.0f + 23.0f;
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    // A joint's tail is a cross, a band flat across its run and one upright (CreateTailAxis):
    // both drawn here, a straight strip from `from` to `to`, `width` across.
    const auto strip = [&](const float* from, const float* to, float width,
                           bgfx::TextureHandle sheet, gfx::Blend blend, const float* light) {
        float run[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
        const float length = std::sqrt(run[0] * run[0] + run[1] * run[1] + run[2] * run[2]);
        if (length < 1e-4f || !bgfx::isValid(sheet)) return;
        for (float& r : run) r /= length;
        float flat[3] = {run[2], 0.0f, -run[0]};
        const float f = std::sqrt(flat[0] * flat[0] + flat[2] * flat[2]);
        if (f < 1e-4f) {
            flat[0] = 1.0f;
            flat[2] = 0.0f;
        } else {
            flat[0] /= f;
            flat[2] /= f;
        }
        const float up[3] = {run[1] * flat[2] - run[2] * flat[1],
                             run[2] * flat[0] - run[0] * flat[2],
                             run[0] * flat[1] - run[1] * flat[0]};
        const float half = width * 0.5f;
        for (const float* across : {static_cast<const float*>(flat), up}) {
            gfx::Sprite band;
            band.placed = true;
            band.sheet = sheet;
            band.blend = blend;
            for (int k = 0; k < 3; ++k) {
                band.colour[k] = light[k] * kRibGlow;
                band.corner[0][k] = from[k] - across[k] * half;
                band.corner[1][k] = from[k] + across[k] * half;
                band.corner[2][k] = to[k] + across[k] * half;
                band.corner[3][k] = to[k] - across[k] * half;
                band.position[k] = (from[k] + to[k]) * 0.5f;
            }
            band.colour[3] = 1.0f;
            const float uv[4][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
            for (int c = 0; c < 4; ++c) {
                band.cornerUv[c][0] = uv[c][0];
                band.cornerUv[c][1] = uv[c][1];
            }
            effects.add(band);
        }
    };
    for (const auto& [centreBone, endBone] : ribs_) {
        float centre[3], end[3];
        if (!figure_.pointOn(centreBone, origin, centre) || !figure_.pointOn(endBone, origin, end)) {
            continue;
        }
        // The thunder from the rib's far bone to its centre, the spirit back.
        strip(end, centre, scale * kUnit, gThunder, gfx::Blend::Additive, kBoltLight);
        strip(centre, end, (scale + kSpiritWider) * kUnit, gSpirit, gfx::Blend::Additive, kBoltLight);
        gfx::Sprite flare;
        for (int k = 0; k < 3; ++k) {
            flare.position[k] = centre[k];
            flare.colour[k] = kRibLight[k] * kRibGlow;
        }
        flare.colour[3] = 1.0f;
        flare.halfWidth = flare.halfHeight = kRibFlare * scale / 20.0f;
        flare.sheet = gFlare;
        flare.blend = gfx::Blend::Additive;
        effects.add(flare);
    }
}

void WingLook::update(float seconds, bool flying, bool safe) {
    if (!wing_) return;
    // MuMain sets the Wings of Darkness's action to 1 while he is in a safe zone, the one wing
    // with a second action; every other wing has the flap alone and finds no 1.
    if (wing_->library && wing_->idleClip >= 0) {
        const int rest = wing_->library->find(1);
        const int wanted = safe && rest >= 0 ? rest : wing_->idleClip;
        if (figure_.clip() != wanted) figure_.play(wanted, true, 0.2f);
    }
    figure_.update(seconds, flying ? kWingFlyRate : kWingRestRate);
    clock_ += seconds;
}

void WingLook::gather(gfx::Renderer& renderer, const Figure& bearer, std::vector<float>& scratch,
                      std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters,
                      float fade) {
    if (!wing_ || !bearer.body() || bearer.body()->backBone < 0) return;
    float bone[16];
    if (!bearer.boneWorld(bearer.body()->backBone, bone)) return;
    // In the bone's own frame, moved along it by MU's offset, as the Imp is (game/pets.cpp).
    float offset[3] = {kWingOffset[0], kWingOffset[1], kWingOffset[2]};
    for (const WingInward& in : kInward) {
        if (wing_->name == in.name) offset[2] += in.metres;
    }
    float local[16];
    content::placementTransform(0.0f, 0.0f, 0.0f, 1.0f, offset, local);
    float parent[16];
    core::mulMatrix(local, bone, parent);
    figure_.mount(parent);
    const int bones = figure_.pose(scratch.data());
    posed_ = bones > 0;
    const int palette = bones > 0 ? renderer.addPalette(scratch.data(), bones) : -1;
    const size_t from = out.size();
    if (casters) figure_.gather(palette, *casters);
    figure_.gather(palette, out);
    // Drawn at its sheet's own colour in any light, as the Ice Queen is (Figure::gather): MU
    // links a wing with lighting off (RenderLinkObject sets b->LightEnable = false), so its
    // painted feathers are what show, never the sun's dark side or the shadow map's blocks on
    // a thin card -- the user, 2026-10-05: 'angel wings wierdly accepts light and shadows'.
    // Ours: the Wings of Heaven a little under their sheet, whose pure white glared at noon
    // once self-lit -- the user, 2026-10-05: 'tone them down slightly'.
    const bool heaven = wing_->name == "Wing02";
    const float tone = heaven ? kHeavenTone : 1.0f;
    // And the Wings of Darkness drawn twice, the second a violet chrome (game/shine.h).
    const int darkness = wing_->name == "Wing07" ? kShineDarkness : 0;
    for (size_t i = from; i < out.size(); ++i) {
        out[i].fade = fade * (heaven ? kHeavenOpacity : 1.0f);
        out[i].light[3] = 2.0f;
        for (int k = 0; k < 3; ++k) out[i].light[k] = tone;
        out[i].refine += darkness;
    }
    sparks(renderer.effects());
}

int WingLook::tips(float out[][3], int most) const {
    if (!wing_ || !posed_ || tipBones_.empty() || most <= 0) return 0;
    const int count = std::min(most, int(tipBones_.size()));
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    int made = 0;
    for (int i = 0; i < count; ++i) {
        // Spread along the list when it holds more than are asked for.
        const size_t at = size_t(i) * tipBones_.size() / size_t(count);
        if (figure_.pointOn(tipBones_[at], origin, out[made])) ++made;
    }
    return made;
}

}  // namespace mu::game
