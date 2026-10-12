// The Aida breeds' own shows, fed each frame from Play::shade: a Blue Golem's water
// (fx/golem_water.h) and a Witch Queen's lights and strikes (fx/witch_queen.h), each by its
// recipe's show tag. Out of play_show.cpp, which is at its size.
#include "game/play.h"

#include "game/play_tuning.h"

namespace mu::game {

void Play::breedShows(Drawn& one, float fade, float seconds) {
    const FigureBody* look = one.figure.body();
    if (!look || (!look->has("blue_golem") && !look->has("witch_queen"))) return;
    // What it hunts: its quarry, or its blow's target; that one's feet, or null.
    const sim::Body* self = realm_.find(one.id);
    const Drawn* prey = self ? drawnOf(self->quarry ? self->quarry : self->blowTarget) : nullptr;
    const FigureBody* aim = prey && prey->placed ? prey->figure.body() : nullptr;
    const float target[3] = {aim ? prey->crown[0] : 0.0f,
                             aim ? prey->crown[1] - aim->height * aim->scale : 0.0f, aim ? prey->crown[2] : 0.0f};
    // A Blue Golem's drips, and its first blow drawing a life back from what it hunts.
    if (look->has("blue_golem")) {
        shadowStars_.golemWater().watch(one.id, one.figure, one.figure.clip() == one.attackClip,
                                        keyOf(one.figure), aim ? target : nullptr, fade, seconds);
        for (float burst[3]; shadowStars_.golemWater().takeBurst(burst);)
            if (heard_.golemBurst >= 0) emit(heard_.golemBurst, burst[0], burst[2]);
        return;
    }
    // A Witch Queen's lights, her blows' flashes and Attack 2's ring, its thunder where it falls.
    const int attack = one.figure.clip() == one.attackClip ? 1 : one.figure.clip() == one.attackClip2 ? 2 : 0;
    const float feet[3] = {one.crown[0], ground_ ? ground_->heightAt(one.crown[0], one.crown[2]) : 0.0f,
                           one.crown[2]};
    shadowStars_.witchQueen().watch(one.id, one.figure, attack, keyOf(one.figure), feet, one.yaw,
                                    aim ? target : nullptr, fade, seconds);
    // Her joints are the wizard's Lightning's (fx/thunder.h): a strike from the sky, or a crackle.
    for (WitchQueen::Throw one; shadowStars_.witchQueen().take(one);) {
        if (one.fork) {
            thunder_.fork(one.from, one.to);
            continue;
        }
        thunder_.strike(one.from, one.to, 0);
        if (one.loud && heard_.witchThunder >= 0) emit(heard_.witchThunder, one.to[0], one.to[2]);
    }
}

}  // namespace mu::game
