// What the realm sounds like: the swing from what is in his hands, the footsteps read off the
// walk cycle's own clock, Hanzo's hammer, a thing landing on the grass, and the ears placed on
// the camera each frame.
//
// Every placed sound goes through `emit`, and is heard only if the camera holds where it is.
#include "game/play.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/frustum.h"
#include "game/play_tuning.h"

namespace mu::game {

int Play::swingSound(const sim::Body& body) const {
    // The player half of the `AnimationFrame == 0.f` block at the end of SetPlayerAttack, in
    // the client's own order, and NOT derived from the clip: a Berdysh and a Kris play
    // different actions and the same sound. MU2's Crowd.Swinging.
    const auto arm = [&](int32_t i) -> const content::Arm* {
        return i >= 0 && size_t(i) < tables_.arms.size() ? &tables_.arms[size_t(i)] : nullptr;
    };
    const content::Arm* right = arm(body.weapon);
    const content::Arm* left = arm(body.shield);
    for (const content::Arm* held : {right, left}) {
        if (held && held->bow()) return heard_.bow;
    }
    for (const content::Arm* held : {right, left}) {
        if (held && held->crossbow()) return heard_.crossbow;
    }
    // MODEL_SWORD+10 and MODEL_SPEAR -- the Light Saber and the Light Spear at (3,0), the
    // group's first item and not the group, so the Berdysh is not given the long swing.
    if (right && ((right->group == 0 && right->number == 10) ||
                  (right->group == 3 && right->number == 0))) {
        return heard_.swingLong;
    }
    // Bare hands make no swing sound; the hit they land is separate.
    return right || left ? heard_.swing : -1;
}

void Play::steps() {
    // PlayWalkSound, and the hero's alone: the client guards it with `c == Hero`, so nobody
    // else in the world has feet you can hear. MU2's Crowd.Steps.
    Drawn* hero = drawnOf(realm_.hero().id);
    const sim::Body& him = realm_.hero();
    const FigureBody* look = hero ? hero->figure.body() : nullptr;
    const int clip = hero ? hero->figure.clip() : -1;
    const bool walking = hero && look && him.alive() && hero->visible && clip >= 0 &&
                         (clip == look->walkClip || clip == look->walkSafeClip);
    if (!walking || ground_ == nullptr) {
        // Not walking, so the next cycle starts fresh: the client clears both latches the
        // moment the animation is not running.
        leftFoot_ = rightFoot_ = striding_ = false;
        return;
    }
    const float key = keyOf(hero->figure);
    // Setting off part way through a cycle -- a walk resumes where it was left -- a foot the
    // phase has already gone past counts as heard, or a walk picked up at three quarters would
    // crunch on the frame it starts with no foot landing under it.
    if (!striding_) {
        striding_ = true;
        leftFoot_ = key >= kFirstFoot;
        rightFoot_ = key >= kSecondFoot;
    }
    // The cycle wrapped, which is where MU clears them.
    if (key < kFirstFoot) {
        leftFoot_ = rightFoot_ = false;
        return;
    }
    const auto tread = [&]() {
        const float metresPerTile = ground_->metresPerTile();
        const int column = int(std::floor(hero->crown[0] / metresPerTile));
        const int row = int(std::floor(-hero->crown[2] / metresPerTile));
        const int sound = ground_->floorAt(column, row) == kGrassFloor ? heard_.grass : heard_.soil;
        if (sound >= 0) emit(sound, hero->crown[0], hero->crown[2], hero->id);
    };
    if (!leftFoot_) {
        leftFoot_ = true;
        tread();
    }
    if (!rightFoot_ && key >= kSecondFoot) {
        rightFoot_ = true;
        tread();
    }
}

void Play::hammer() {
    if (heard_.hammer < 0) return;
    for (Standing& one : folk_) {
        if (!one.smith) continue;
        // The blow, and not whatever else he does: action0 is the eleven-key swing and his
        // other clip is a look along a blade that would ring the anvil at its side.
        const float key = keyOf(one.figure);
        if (slotOf(one.figure) != 0 || key < kHammerFrom || key > kHammerTo) {
            one.rung = false;
            continue;
        }
        if (one.rung) continue;
        one.rung = true;
        // Placed at him, where MU plays it unplaced: a smith heard from the far bank is the
        // wrong half of MU's simplification to keep. MU2's call, marked there too.
        const float* at = one.figure.position();
        emit(heard_.hammer, at[0], at[2]);
    }
}

void Play::landed(uint32_t drop) {
    if (ground_ == nullptr) return;
    for (const sim::Lying& one : realm_.lying()) {
        if (one.id != drop) continue;
        // CreateItemDrop's branch: SOUND_JEWEL01 for the jewels, SOUND_DROP_ITEM01 for any
        // other thing, and CreateMoneyDrop's SOUND_DROP_MONEY01 for Zen. MU plays the coins
        // unplaced, a reward mixed like one; MU2 placed both, and so does this -- the heap is
        // always a few steps off, which the carry puts at full volume anyway.
        int sound = heard_.itemDrop;
        if (one.what.empty()) {
            sound = heard_.moneyDrop;
        } else if (one.what.item >= 0 && size_t(one.what.item) < tables_.items.size() &&
                   tables_.items[size_t(one.what.item)].jewel() && heard_.jewel >= 0) {
            sound = heard_.jewel;
        }
        const float metresPerTile = ground_->metresPerTile();
        emit(sound, (float(one.column) + 0.5f) * metresPerTile,
                      -(float(one.row) + 0.5f) * metresPerTile);
        return;
    }
}

void Play::emit(int event, float x, float z, uint32_t following) {
    // Only what the camera holds is heard. **A departure from MU**, whose falloff mixes every
    // attached object however far off it is and plays the townspeople's noises unplaced
    // everywhere: at 1/d past two and a half metres, Hanzo's anvil carried from the square to
    // Harold's campfire fifty tiles away. MU2 already culled its scenery sounds to the shot;
    // this is that rule for everything placed. A voice already sounding is not cut off when
    // its source leaves the frame -- only a new one is refused.
    if (shotKnown_ && ground_) {
        const Frustum frustum(shot_);
        const float centre[3] = {x, ground_->heightAt(x, z) + kHeardHeight, z};
        if (!frustum.holds(centre, kHeardReach)) return;
    }
    sound_.playAt(event, x, z, following);
}

void Play::ui(Ui which) {
    switch (which) {
        case Ui::Click: sound_.play(heard_.click); break;
        case Ui::Refused: sound_.play(heard_.refused); break;
        case Ui::Took: sound_.play(heard_.take); break;
        case Ui::Opened: sound_.play(heard_.opened); break;
    }
}

void Play::hear(const gfx::Camera& camera, bool indoors) {
    if (!sound_.isOpen()) return;
    // The air: on while he is not under a roof, which is the client's own switch -- it stops
    // SOUND_WIND01 on HeroTile 4. Unplaced: wind is not somewhere, it is everywhere.
    sound_.loop(heard_.wind, !indoors);
    const Drawn* hero = drawnOf(realm_.hero().id);
    if (hero == nullptr || !hero->placed) return;
    sound_.listen(hero->crown[0], hero->crown[2], camera.target[0] - camera.position[0],
                  camera.target[2] - camera.position[2]);
    sound_.follow(
        [](void* context, uint32_t id, float* x, float* z) {
            const Drawn* one = static_cast<Play*>(context)->drawnOf(id);
            if (one == nullptr || !one->placed || !one->visible) return false;
            *x = one->crown[0];
            *z = one->crown[2];
            return true;
        },
        this);
}

}  // namespace mu::game
