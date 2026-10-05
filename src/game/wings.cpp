#include "game/wings.h"

#include <algorithm>
#include <string>

#include "content/placement.h"
#include "core/maths.h"
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
constexpr WingInward kInward[] = {{"Wing04", 0.18f}, {"Wing05", 0.065f}, {"Wing06", 0.08f}};

}  // namespace

const FigureBody* wingBody(const Figures& figures, int group, int number) {
    if (group != 12) return nullptr;
    if (number >= 0 && number <= 2) return figures.body("Wing0" + std::to_string(number + 1));
    // And the 2nd wings, Wing04-06 (ZzzOpenData.cpp:1023, `Wing`, 4 + i), at our numbers.
    if (number == sim::kSpiritsNumber) return figures.body("Wing04");
    if (number == sim::kSoulNumber) return figures.body("Wing05");
    if (number == sim::kDragonNumber) return figures.body("Wing06");
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
}

void WingLook::update(float seconds, bool flying) {
    if (!wing_) return;
    figure_.update(seconds, flying ? kWingFlyRate : kWingRestRate);
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
    for (size_t i = from; i < out.size(); ++i) out[i].fade = fade;
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
