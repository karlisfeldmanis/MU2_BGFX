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
    // Bare hands make no swing sound in MU. They do here: an empty hand swings the sword's clip
    // now (sim/swings.cpp), and the same swing in silence read as something missing. Ours.
    return heard_.swing;
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

void Play::chatter(float seconds) {
    for (Standing& one : folk_) {
        if (one.voice < 0) continue;
        one.busy -= seconds;
        if (one.busy > 0.0f) continue;
        one.dice ^= one.dice << 13;
        one.dice ^= one.dice >> 17;
        one.dice ^= one.dice << 5;
        const float roll = float(one.dice % 10000u) / 10000.0f;
        if (roll >= seconds / one.every) continue;
        const float* at = one.figure.position();
        emit(one.voice, at[0], at[2]);
        one.busy = sound_.seconds(one.voice);
    }
}

void Play::landed(uint32_t drop) {
    if (ground_ == nullptr) return;
    for (const sim::Lying& one : realm_.lying()) {
        if (one.id != drop) continue;
        // CreateItemDrop's branch: SOUND_JEWEL01 for the jewels, SOUND_DROP_ITEM01 for any
        // other thing. MU also rings CreateMoneyDrop's SOUND_DROP_MONEY01 for Zen and this
        // does NOT -- the user's call, 2026-09-23, and it follows from the sweep: Zen is picked
        // up the moment he walks onto it, so the coins land and are collected within a second
        // of each other and the two sounds tread on one another. The coins are kept for the
        // half that is worth hearing, which is the taking.
        if (one.what.empty()) return;
        int sound = heard_.itemDrop;
        if (one.what.item >= 0 && size_t(one.what.item) < tables_.items.size() &&
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
    // Past the frame's edge the voice would be silent anyway (Sound's kEdgeSilent); this is the
    // early out, and the fade is the rule.
    const float y = ground_ ? ground_->heightAt(x, z) + kHeardHeight : 0.0f;
    if (shotKnown_ && ground_) {
        const Frustum frustum(shot_);
        const float centre[3] = {x, y, z};
        if (!frustum.holds(centre, kHeardReach)) return;
    }
    sound_.playAt(event, x, y, z, following);
    // The hero's big moments lean the world back (docs/spatial-sound.md, C): his fall, and a
    // skill he casts. Not a landed blow, which is every second of a fight.
    const uint32_t him = realm_.hero().id;
    bool big = event == heard_.die;
    for (int skill : heard_.skill) big = big || (skill >= 0 && event == skill && following == him);
    if (big) sound_.duck();
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
    // And the same switch is the room: a slap off the town's walls in the open, a small room
    // under a roof (docs/spatial-sound.md, E).
    sound_.room(indoors ? Sound::Room::Roofed : Sound::Room::Open);
    // The walls are the rules' own line of sight on the tile grid (F). The far end is pulled a
    // tile back toward the ears first: a smith at his anvil or a thing lying against a house
    // stands on or beside a closed tile, and is not behind it. Within two tiles nothing is.
    sound_.walls(
        [](void* context, const float from[3], const float to[3]) {
            const Play& play = *static_cast<const Play*>(context);
            if (play.ground_ == nullptr) return true;
            const float perTile = std::max(play.ground_->metresPerTile(), 0.001f);
            const float ax = from[0] / perTile - 0.5f, ay = -from[2] / perTile - 0.5f;
            float bx = to[0] / perTile - 0.5f, by = -to[2] / perTile - 0.5f;
            const float dx = ax - bx, dy = ay - by;
            const float tiles = std::sqrt(dx * dx + dy * dy);
            if (tiles <= 2.0f) return true;
            bx += dx / tiles;
            by += dy / tiles;
            return play.realm_.router().sees(ax, ay, bx, by, content::kWallNoMove);
        },
        this);
    const Drawn* hero = drawnOf(realm_.hero().id);
    if (hero == nullptr || !hero->placed) return;
    // The ears at the character, the pan from the shot point() kept this frame.
    if (!shotKnown_) return;
    (void)camera;
    sound_.listen(hero->id, hero->crown, shot_);
    sound_.follow(
        [](void* context, uint32_t id, float* x, float* y, float* z) {
            const Drawn* one = static_cast<Play*>(context)->drawnOf(id);
            if (one == nullptr || !one->placed || !one->visible) return false;
            *x = one->crown[0];
            *y = one->crown[1];
            *z = one->crown[2];
            return true;
        },
        this);
}

}  // namespace mu::game
