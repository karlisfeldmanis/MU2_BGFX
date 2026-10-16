// The inspect window's doll (game/ui/inspect.h): another player dressed as he is, standing as in a
// safe zone, photographed whole by the desk's figure portrait and turned by a drag in the window.
#include <bx/math.h>

#include <algorithm>
#include <chrono>

#include "game/play.h"
#include "game/roster.h"
#include "game/wings.h"

namespace mu::game {

void Play::wantDoll(uint32_t id) {
    if (id == dollId_) return;
    dollId_ = id;
    dollDressed_ = false;
}

uint64_t Play::dollGearOf(const sim::Body& body) const {
    uint64_t h = gearOf(body);
    if (const sim::Satchel* bag = realm_.satchelOf(body.id)) {
        const sim::Held& wing = (*bag)[sim::kWings];
        h ^= (uint64_t(uint32_t(wing.item)) << 20 | uint64_t(wing.durability > 0)) * 0x9E3779B97F4A7C15ull;
    }
    return h;
}

bool Play::dollStale() const {
    const sim::Body* body = dollId_ != 0 ? realm_.find(dollId_) : nullptr;
    if (!body) return false;
    return !dollDressed_ || dollGearOf(*body) != dollGear_;
}

bool Play::poseDoll(gfx::Renderer& renderer, float turn, std::vector<gfx::Drawable>& out, float head[3],
                    float* scale) {
    out.clear();
    const sim::Body* body = dollId_ != 0 ? realm_.find(dollId_) : nullptr;
    const sim::Satchel* bag = body ? realm_.satchelOf(dollId_) : nullptr;
    if (!body || !bag || !figures_) return false;
    // Away from everything, at the world's origin: the stage draws only what it is handed.
    static const float kAt[3] = {0.0f, 0.0f, 0.0f};
    if (!dollDressed_ || dollGearOf(*body) != dollGear_) {
        // **Under a name of its own**: Figures keeps a dressed body by its name and a second dressing
        // under the same name frees the first, so dressing the doll as "Player<id>" (otherLook) took
        // the body out from under his figure in the world, which crashed the next frame that drew him.
        figures_->setStreaming(streamGear_);
        const FigureBody* look = dressOf(*body, *bag, "InspectDoll", bareBody(body->kin, body->second, figures_),
                                         quiverOf(*bag));
        // A piece still streaming in dresses him again next time, rather than leaving it off.
        const bool missing = figures_->lastMissed() > 0;
        figures_->setStreaming(false);
        if (!look) return false;
        // As in a safe zone: the weapon on his back and the unarmed idle (Figure::stand).
        doll_.stand(look, kAt, turn, look->scale, true);
        dollGear_ = dollGearOf(*body);
        dollDressed_ = !missing;
        // His wings, as Play::otherWings wears them: while the wing has life left.
        const sim::Held& wing = (*bag)[sim::kWings];
        const content::ItemRow* row = !wing.empty() && wing.durability > 0 && size_t(wing.item) < tables_.items.size()
                                          ? &tables_.items[size_t(wing.item)]
                                          : nullptr;
        dollWing_.wear(row ? wingBody(*figures_, row->group, row->number) : nullptr, doll_);
        // On wings he hovers: PLAYER_STOP_FLY, or its crossbow's, with his weapons still on his back.
        if (dollWing_.worn()) {
            const int hover = look->stance == "crossbow" && look->flyIdleCrossbowClip >= 0 ? look->flyIdleCrossbowClip
                                                                                          : look->flyIdleClip;
            if (hover >= 0) doll_.play(hover, true, 0.0f);
        }
        dollClock_ = -1.0;
    }
    // Its own seconds, off the steady clock: the desk poses it once a frame while it moves.
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float seconds = dollClock_ < 0.0 ? 0.0f : float(std::clamp(now - dollClock_, 0.0, 0.1));
    dollClock_ = now;
    doll_.place(kAt, turn, true);
    doll_.update(seconds);
    if (dollWing_.worn()) dollWing_.update(seconds, true);
    if (dollRows_.size() < size_t(gfx::Renderer::kMaxBones) * 12) dollRows_.resize(size_t(gfx::Renderer::kMaxBones) * 12);
    const int bones = doll_.pose(dollRows_.data());
    const int palette = bones > 0 ? renderer.addPalette(dollRows_.data(), bones) : -1;
    if (scratch_.size() < size_t(gfx::Renderer::kMaxBones) * 12) scratch_.resize(size_t(gfx::Renderer::kMaxBones) * 12);
    doll_.poseHeld(renderer, scratch_.data());
    doll_.gather(palette, out);
    if (dollWing_.worn()) dollWing_.gather(renderer, doll_, scratch_, out, nullptr);

    // His head joint where this pose holds it, as the target's face is found (Play::gather's shoot).
    const FigureBody* look = doll_.body();
    head[0] = kAt[0];
    head[1] = kAt[1] + (look ? look->height * doll_.scale() * 0.9f : 1.6f);
    head[2] = kAt[2];
    if (look && look->skeletonMesh) {
        const std::vector<content::Bone>& bonesOf = look->skeletonMesh->bones();
        for (size_t b = 0; b < bonesOf.size(); ++b) {
            if (bonesOf[b].name != "Bip01 Head") continue;
            float bind[16];
            bx::mtxInverse(bind, bonesOf[b].inverseBind);
            const float joint[3] = {bind[12], bind[13], bind[14]};
            doll_.pointOnBind(int(b), joint, head);
            break;
        }
    }
    // On his own axis, so he turns in place under a fixed lens rather than swinging round it.
    head[0] = kAt[0];
    head[2] = kAt[2];
    *scale = look ? look->height * doll_.scale() / 1.8f : 1.0f;
    return !out.empty();
}

}  // namespace mu::game
