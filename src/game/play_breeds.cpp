// The Aida breeds' own shows, fed each frame from Play::shade: a Blue Golem's water
// (fx/golem_water.h), a Witch Queen's lights and strikes (fx/witch_queen.h), and the sparks off a
// Death Rider's head and a Death Tree's horns (fx/aida_sparks.h), and Hell Maine's lights, storm and
// fire lay (fx/hell_maine.h), each by its recipe's show tag.
// Out of play_show.cpp, which is at its size.
#include "game/play.h"

#include "game/play_tuning.h"

namespace mu::game {

void Play::breedShows(Drawn& one, float fade, float seconds) {
    const FigureBody* look = one.figure.body();
    if (!look) return;
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    // A Death Rider's head, its aura bone; a Death Tree's six horns, its star bones.
    if (look->has("death_rider")) {
        float head[3];
        if (one.auraBone >= 0 && one.figure.pointOn(one.auraBone, origin, head))
            shadowStars_.aidaSparks().head(head, fade, seconds);
        return;
    }
    if (look->has("death_tree")) {
        for (const int bone : one.starBones)
            if (float horn[3]; one.figure.pointOn(bone, origin, horn)) shadowStars_.aidaSparks().horn(horn, fade, seconds);
        return;
    }
    // Hell Maine's lights and hand fire, Attack 1's storm (its stones are the meteor's) and
    // Attack 2's fire lay.
    if (look->has("hell_maine")) {
        const int attack = one.figure.clip() == one.attackClip ? 1 : one.figure.clip() == one.attackClip2 ? 2 : 0;
        const float feet[3] = {one.crown[0], ground_ ? ground_->heightAt(one.crown[0], one.crown[2]) : 0.0f,
                               one.crown[2]};
        shadowStars_.hellMaine().watch(one.id, one.figure, attack, keyOf(one.figure), feet, one.yaw, fade, seconds);
        for (float stone[3]; shadowStars_.hellMaine().takeStone(stone);) meteor_.stones(stone[0], stone[2], stone[1], 1);
        return;
    }
    if (!look->has("blue_golem") && !look->has("witch_queen")) return;
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
