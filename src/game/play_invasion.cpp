// The Golden Invasion as it is drawn: the realm says it has begun and where its dragon will land
// (What::Invasion), the storm is held, the sky (game/invasion_sky.h) flies its dragons in and
// dives the last onto that tile on the landing tick, and the realm's dragon rises there into the
// roar the dive turned into -- MU's AppearMonster: MONSTER01_STOP2 and SOUND_MONSTER_BULLATTACK1
// (WSclient.cpp:2767-2772).
#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/play.h"
#include "game/play_tuning.h"

namespace mu::game {
namespace {

// Where the dive's roar has got to when the realm's dragon takes it over (InvasionSky's
// kRoarFade), so the landed body carries on from the dive's pose rather than starting it again.
constexpr float kRoarTaken = 0.5f;

}  // namespace

void Play::openInvasionSky(bgfx::TextureHandle glow, bgfx::TextureHandle haze) {
    const FigureBody* dragon = figures_ ? figures_->body("GoldenDragon01") : nullptr;
    sky_.open(dragon, glow, haze);
    invasionStorm_ = false;
    // Its sounds are loaded as it begins: the device is opened after the effects are, and a load
    // before it answers nothing.
    invasionCry_ = invasionChime_ = -1;
    if (dragon) core::logf("invasion: the golden dragons are ready over this map");
}

void Play::invasionSaid(const sim::Happening& happening) {
    const sim::Body* dragon = realm_.invader();
    if (dragon == nullptr || happening.who != dragon->id) return;
    if (happening.what == sim::What::Invasion) {
        if (happening.a == 1) {
            const float metresPerTile = ground_ ? ground_->metresPerTile() : 1.0f;
            const float x = (float(happening.b) + 0.5f) * metresPerTile;
            const float z = -(float(happening.c) + 0.5f) * metresPerTile;
            // The sim's facing turned into a model's yaw, as Play::gather turns every body's.
            const float yaw = std::atan2(std::cos(dragon->facing), -std::sin(dragon->facing));
            // The raid's dragon comes alone: no crossings, only its own dive.
            sky_.begin(x, z, yaw, float(sim::kInvasionRainTicks) * float(kTickSeconds),
                       float(sim::kInvasionLandTicks) * float(kTickSeconds), raidPlayers_ == 0);
            invasionStorm_ = true;
            if (invasionCry_ < 0) invasionCry_ = sound_.load("goldendragon_attack", true, true);
            if (invasionChime_ < 0) invasionChime_ = sound_.load("invasion_start", false);
            // MU has no sound of its own for a dragon's coming; the event's chime is ours.
            if (invasionChime_ >= 0) sound_.play(invasionChime_);
        } else {
            sky_.end();
            invasionStorm_ = false;
        }
        return;
    }
    // Its roar waits for the frame's own pass (Play::invasion): the loop that said this stands
    // every risen body in its idle after asking here.
    if (happening.what == sim::What::Rose) {
        roarOwed_ = happening.who;
        // Risen, the raid's dragon is the only one: a dive not yet down -- --raid-now lands it at
        // once -- is never flown, so a second does not come down onto the fight.
        if (raidPlayers_ > 0) sky_.end();
    }
}

void Play::invasion(float seconds) {
    if (roarOwed_ != 0) {
        const uint32_t who = roarOwed_;
        roarOwed_ = 0;
        roar(who, roarWhole_);
        roarWhole_ = false;
    }
    invasionSky(seconds);
}

void Play::roar(uint32_t who, bool whole) {
    Drawn* risen = drawnOf(who);
    const sim::Body* body = realm_.find(who);
    if (risen == nullptr || body == nullptr || risen->figure.body() == nullptr ||
        risen->figure.body()->library == nullptr) {
        return;
    }
    // Down out of the sky, not faded in as a respawn is.
    risen->spawnFade = 1e9f;
    const int clip = risen->figure.body()->library->find(1);  // MONSTER01_STOP2
    if (clip >= 0) {
        risen->figure.play(clip, true, 0.0f, true);
        const float from = whole ? 0.0f : kRoarTaken;
        risen->figure.setClock(from);
        // Held as a swing is, so the idle does not take it back on the next frame.
        risen->swinging = std::max(0.0f, risen->figure.length() - from);
        risen->swingPace = 1.0f;
        ++risen->swingToken;
    }
    if (invasionCry_ >= 0 && ground_ != nullptr) {
        const float metresPerTile = ground_->metresPerTile();
        emit(invasionCry_, (body->x + 0.5f) * metresPerTile, -(body->y + 0.5f) * metresPerTile);
    }
}

void Play::invasionSky(float seconds) {
    if (!sky_.isOpen() || ground_ == nullptr) return;
    const Drawn* hero = drawnOf(realm_.hero().id);
    if (hero == nullptr) return;
    const float at[3] = {hero->crown[0], ground_->heightAt(hero->crown[0], hero->crown[2]),
                         hero->crown[2]};
    skyAsks_.calls = 0;
    skyAsks_.sparks.clear();
    skyAsks_.landed = false;
    sky_.update(seconds, at, *ground_, skyAsks_);
    // Heard wherever he stands, as MU's PlayBuffer(SOUND_MONSTER_BULLATTACK1) in MoveBoids is --
    // placed at him rather than unplaced: an unplaced event has one voice, and each cry cut the
    // last one off (the user, 2026-10-06: 'it keeps reseting').
    for (int i = 0; i < skyAsks_.calls; ++i) {
        if (invasionCry_ >= 0) emit(invasionCry_, at[0], at[2]);
    }
    // MU's BITMAP_FIRE at full size: a dragon is not the half-size animal Breath scales for.
    for (size_t i = 0; i + 4 < skyAsks_.sparks.size(); i += 5) {
        const float along[2] = {skyAsks_.sparks[i + 3], skyAsks_.sparks[i + 4]};
        breath_.spark(&skyAsks_.sparks[i], along, 1.0f);
    }
}

}  // namespace mu::game
