#include <algorithm>
#include <cmath>

#include "game/play.h"

namespace mu::game {

// Moved out of play_show.cpp's Play::shade on 2026-10-11 to pay the size ratchet for the Death
// Tree's roots (fx/tree_roots.h), unchanged.
void Play::ribbonStars(float seconds) {
    // An Alquamos blow's ribbons: MoveJoint's BITMAP_FLARE sub 7 (ZzzEffectJoint.cpp:5603-5697),
    // in MU's units and axes (x east, y north, z up) about the target's feet, a frame at a time.
    for (StarRibbon& one : starRibbons_) {
        const Drawn* target = drawnOf(one.target);
        if (target == nullptr || !target->placed) {
            one.life = 0.0f;
            continue;
        }
        one.owed += seconds * 25.0f;
        while (one.owed >= 1.0f && one.life > 0.0f) {
            one.owed -= 1.0f;
            one.life -= 1.0f;
            const int frame = int(folkClock_ * 25.0f);
            const float fi = float(((one.index % 2) ? frame : -frame) + one.index * 53731);
            const float t0 = std::sin((fi + 55555.0f) * 0.048f) * std::cos(fi * 0.0613f);
            const float t1 = std::sin((fi + 55555.0f) * 0.048f) * std::sin(fi * 0.0613f);
            const float t2 = std::cos((fi + 55555.0f) * 0.048f);
            const float sinAdd = std::sin((fi + 11111.0f) * 0.1113f);
            const float cosAdd = std::cos((fi + 11111.0f) * 0.1113f);
            const float dir[3] = {cosAdd * t1 - sinAdd * t2, sinAdd * t1 + cosAdd * t2, t0};
            const float life = one.life * 40.0f / 30.0f;
            float pos = life < 10.0f ? life * 7.0f : life + 60.0f;
            pos = pos / float(30 + one.multi) * 30.0f;
            const float circle = std::min(std::max(0.0f, 40.0f - life) * 15.0f, 150.0f);
            // The target's feet in MU's frame, in units.
            const float floor =
                ground_ ? ground_->heightAt(target->crown[0], target->crown[2]) : target->crown[1];
            const float feet[3] = {target->crown[0] * 100.0f, -target->crown[2] * 100.0f,
                                   floor * 100.0f};
            float mu[3];
            for (int k = 0; k < 3; ++k) {
                const float swing =
                    25.0f * std::cos(float(one.index * 51231 + k * 3711 + frame / 10) * 0.01f);
                const float last = (100.0f - pos) * (feet[k] + swing);
                mu[k] = (pos * (feet[k] + dir[k] * circle) + last) * 0.01f;
            }
            mu[2] += 100.0f;
            for (int t = std::min(one.tails, 14); t > 0; --t)
                for (int k = 0; k < 3; ++k) one.trail[t][k] = one.trail[t - 1][k];
            one.trail[0][0] = mu[0] * 0.01f;
            one.trail[0][1] = mu[2] * 0.01f;
            one.trail[0][2] = -mu[1] * 0.01f;
            one.tails = std::min(one.tails + 1, 15);
        }
        if (one.life > 0.0f) shadowStars_.ribbon(one.trail, one.tails, 1.0f);
    }
    starRibbons_.erase(std::remove_if(starRibbons_.begin(), starRibbons_.end(),
                                      [](const StarRibbon& r) { return r.life <= 0.0f; }),
                       starRibbons_.end());
}

}  // namespace mu::game
