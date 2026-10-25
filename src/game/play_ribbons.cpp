#include <algorithm>
#include <cmath>

#include "game/play.h"
#include "game/play_tuning.h"

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

// Out of play_show.cpp's Play::shade on 2026-10-12, to pay the size ratchet for the bleed's drips
// beside it: the debuffs drawn on a body while they last, a frame at a time.
void Play::debuffShows(float frames) {
    // A poisoned body, a monster's or a character's (the user, 2026-10-12: 'update char poison
    // effect same as monster with that subtle smoke'): green smoke off bones drawn at random.
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        const FigureBody* look = one.figure.body();
        if (!body || !body->alive() || body->poisonUntil <= realm_.tick() ||
            !one.visible || !one.placed || look == nullptr || look->boneCount() < 2) {
            one.sickOwed = 0.0f;
            continue;
        }
        if (one.burningDice == 0) one.burningDice = 0x9e3779b9u ^ (one.id * 2654435761u);
        const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        const bool character = body->player;
        one.sickOwed += frames * (character ? kSickCharPuffs : kSickPuffs);
        while (one.sickOwed >= 1.0f) {
            one.sickOwed -= 1.0f;
            one.burningDice ^= one.burningDice << 13;
            one.burningDice ^= one.burningDice >> 17;
            one.burningDice ^= one.burningDice << 5;
            const int b = int(one.burningDice % uint32_t(bones.size()));
            float at[3];
            if (one.figure.pointOn(b, origin, at)) {
                poison_.sicken(at, std::clamp(look->height * look->scale / 1.8f, 0.7f, 2.0f),
                               character ? kSickCharGlow : kSickGlow);
            }
        }
    }
    // A body under a Weakness curse (the Ancient rune, sim/items.h kWeakness...): MU's
    // eDeBuff_AttackDown, a red star and a red streak shed off a bone drawn at random, each one
    // frame in two (fx/weakness.h).
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        const FigureBody* look = one.figure.body();
        if (!body || !body->alive() || body->weakUntil <= int64_t(realm_.tick()) ||
            !one.visible || !one.placed || look == nullptr || look->boneCount() < 2) {
            one.weakOwed = 0.0f;
            continue;
        }
        if (one.burningDice == 0) one.burningDice = 0x9e3779b9u ^ (one.id * 2654435761u);
        const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        one.weakOwed += frames;
        while (one.weakOwed >= 1.0f) {
            one.weakOwed -= 1.0f;
            one.burningDice ^= one.burningDice << 13;
            one.burningDice ^= one.burningDice >> 17;
            one.burningDice ^= one.burningDice << 5;
            const int b = int(one.burningDice % uint32_t(bones.size()));
            float at[3];
            if (one.figure.pointOn(b, origin, at)) weakness_.mark(at);
        }
    }
    // A bleeding body (sim kBleeders, the user, 2026-10-12: 'bleed', 'Blood drips on the hero'):
    // drops of blood off bones drawn at random, more with each stack.
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        const FigureBody* look = one.figure.body();
        if (!body || !body->alive() || body->bleed <= 0 || body->bleedUntil <= realm_.tick() ||
            body->bleedRequiem || !one.visible || !one.placed || look == nullptr ||
            look->boneCount() < 2 || !ground_) {
            one.bleedOwed = 0.0f;
            continue;
        }
        if (one.burningDice == 0) one.burningDice = 0x9e3779b9u ^ (one.id * 2654435761u);
        const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        const float scale = std::clamp(look->height * look->scale / 1.8f, 0.7f, 2.0f);
        one.bleedOwed += frames * kBleedDrips * float(body->bleed);
        while (one.bleedOwed >= 1.0f) {
            one.bleedOwed -= 1.0f;
            one.burningDice ^= one.burningDice << 13;
            one.burningDice ^= one.burningDice >> 17;
            one.burningDice ^= one.burningDice << 5;
            const int b = int(one.burningDice % uint32_t(bones.size()));
            float at[3];
            if (one.figure.pointOn(b, origin, at)) {
                showing_.drip(at, ground_->heightAt(at[0], at[2]), scale);
            }
        }
    }
    // A body a Requiem bleeds (the Ancient rune, sim/items.h kRequiem...): MU's eDeBuff_NeilDOT,
    // one magenta puff a frame off a bone drawn at random, in place of the drips above
    // (fx/requiem.h).
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        const FigureBody* look = one.figure.body();
        if (!body || !body->alive() || !body->bleedRequiem || body->bleed <= 0 ||
            body->bleedUntil <= realm_.tick() || !one.visible || !one.placed || look == nullptr ||
            look->boneCount() < 2) {
            one.requiemOwed = 0.0f;
            continue;
        }
        if (one.burningDice == 0) one.burningDice = 0x9e3779b9u ^ (one.id * 2654435761u);
        const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        one.requiemOwed += frames;
        while (one.requiemOwed >= 1.0f) {
            one.requiemOwed -= 1.0f;
            one.burningDice ^= one.burningDice << 13;
            one.burningDice ^= one.burningDice >> 17;
            one.burningDice ^= one.burningDice << 5;
            const int b = int(one.burningDice % uint32_t(bones.size()));
            float at[3];
            if (one.figure.pointOn(b, origin, at)) requiem_.mist(at);
        }
    }
    // A body a Sleep rune holds (sim/items.h kSleep...): MU's eDeBuff_Sleep, one violet puff off a
    // bone drawn at random one frame in two (fx/sleep.h).
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        const FigureBody* look = one.figure.body();
        if (!body || !body->alive() || body->sleepUntil <= realm_.tick() || !one.visible ||
            !one.placed || look == nullptr || look->boneCount() < 2) {
            one.sleepOwed = 0.0f;
            continue;
        }
        if (one.burningDice == 0) one.burningDice = 0x9e3779b9u ^ (one.id * 2654435761u);
        const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        one.sleepOwed += frames * 0.5f;
        while (one.sleepOwed >= 1.0f) {
            one.sleepOwed -= 1.0f;
            one.burningDice ^= one.burningDice << 13;
            one.burningDice ^= one.burningDice >> 17;
            one.burningDice ^= one.burningDice << 5;
            const int b = int(one.burningDice % uint32_t(bones.size()));
            float at[3];
            if (one.figure.pointOn(b, origin, at)) sleep_.puff(at);
        }
    }
}

}  // namespace mu::game
