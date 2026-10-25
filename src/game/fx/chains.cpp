#include "game/fx/chains.h"

#include <cmath>

#include "core/log.h"

namespace mu::game {

bool Chains::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table) {
    const content::EffectSheet* sheet = table.effect("whirl_chain");
    if (sheet == nullptr) {
        core::logError("chains: no cooked effect named 'whirl_chain'");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    for (Chain& one : chains_) one = Chain{};
    return bgfx::isValid(sheet_);
}

void Chains::cast(uint32_t knight, uint32_t target, float hold) {
    if (!bgfx::isValid(sheet_)) return;
    Chain* free = nullptr;
    for (Chain& one : chains_) {
        // A second pull on the same body throws its chain again.
        if (one.alive && one.target == target) free = &one;
        if (free == nullptr && !one.alive) free = &one;
    }
    if (free == nullptr) return;
    *free = Chain{};
    free->alive = true;
    free->knight = knight;
    free->target = target;
    free->hold = hold;
}

void Chains::gather(gfx::Effects& effects, const float eye[3]) const {
    for (const Chain& one : chains_) {
        if (!one.alive || one.age <= 0.0f) continue;
        float along[3] = {one.to[0] - one.from[0], one.to[1] - one.from[1], one.to[2] - one.from[2]};
        const float full = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        if (full < 1e-3f) continue;
        for (float& k : along) k /= full;
        // Thrown out from him over kThrow; then whole, and faded once let go.
        const float reach = full * std::min(1.0f, one.age / kThrow);
        const float gone = one.age - std::max(one.hold, kThrow);
        const float alpha = gone > 0.0f ? std::max(0.0f, 1.0f - gone / kLetGo) : 1.0f;
        if (alpha <= 0.0f) continue;
        // Across its line and facing the eye, from its middle.
        const float mid[3] = {one.from[0] + along[0] * reach * 0.5f, one.from[1] + along[1] * reach * 0.5f,
                              one.from[2] + along[2] * reach * 0.5f};
        const float look[3] = {eye[0] - mid[0], eye[1] - mid[1], eye[2] - mid[2]};
        float side[3] = {along[1] * look[2] - along[2] * look[1], along[2] * look[0] - along[0] * look[2],
                         along[0] * look[1] - along[1] * look[0]};
        const float sw = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (sw < 1e-4f) continue;
        for (float& k : side) k *= kHalf / sw;
        // A quad a length of the strip, counted from the far end so the links ride with it:
        // thrown, they fly out with the tip; held, they come in with the body, into his hands.
        float left = reach;
        float at = 0.0f;  // metres from the far end
        while (left > 1e-3f) {
            const float piece = std::min(left, kRepeat);
            float a[3], b[3];
            for (int k = 0; k < 3; ++k) {
                const float end = one.from[k] + along[k] * reach;
                a[k] = end - along[k] * at;
                b[k] = end - along[k] * (at + piece);
            }
            gfx::Sprite quad;
            quad.placed = true;
            quad.sheet = sheet_;
            quad.blend = gfx::Blend::Alpha;
            quad.colour[3] = alpha;
            const float u = piece / kRepeat;
            for (int k = 0; k < 3; ++k) {
                quad.corner[0][k] = a[k] - side[k];
                quad.corner[1][k] = b[k] - side[k];
                quad.corner[2][k] = b[k] + side[k];
                quad.corner[3][k] = a[k] + side[k];
                quad.position[k] = (a[k] + b[k]) * 0.5f;
            }
            quad.cornerUv[0][0] = 0.0f; quad.cornerUv[0][1] = 1.0f;
            quad.cornerUv[1][0] = u;    quad.cornerUv[1][1] = 1.0f;
            quad.cornerUv[2][0] = u;    quad.cornerUv[2][1] = 0.0f;
            quad.cornerUv[3][0] = 0.0f; quad.cornerUv[3][1] = 0.0f;
            if (!effects.add(quad)) return;
            at += piece;
            left -= piece;
        }
    }
}

}  // namespace mu::game
