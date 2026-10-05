#include "game/wings.h"

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

// Ours: the Wings of Dragon pulled 5 cm in against the back (+z is MU's -y, forward). Wing06's
// joint is modelled 6-9 cm behind its origin where Satan's and Soul's sit 3.5-5, so on MU's own
// (0, 0, 15) it stood off a knight's back with a gap -- the user, 2026-10-05: 'looks liek
// there is some gap on DK 2nd wings'.
constexpr float kDragonInward = 0.05f;

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
    if (!wing_) return;
    figure_.stand(wing_, bearer.position(), 0.0f, bearer.scale());
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
    if (wing_->name == "Wing06") offset[2] += kDragonInward;
    float local[16];
    content::placementTransform(0.0f, 0.0f, 0.0f, 1.0f, offset, local);
    float parent[16];
    core::mulMatrix(local, bone, parent);
    figure_.mount(parent);
    const int bones = figure_.pose(scratch.data());
    const int palette = bones > 0 ? renderer.addPalette(scratch.data(), bones) : -1;
    const size_t from = out.size();
    if (casters) figure_.gather(palette, *casters);
    figure_.gather(palette, out);
    for (size_t i = from; i < out.size(); ++i) out[i].fade = fade;
}

}  // namespace mu::game
