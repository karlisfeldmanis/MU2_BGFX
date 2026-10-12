// Blood Castle's run (sim/castle_instance.h): which castle, the clock, the drawbridge and its
// sorcerers, the quotas, the win and its pay (sim/event.h, docs/blood-castle-port.md). Moved out
// of realm_move.cpp unchanged but for how it reaches the realm (the Instance's I3).

#include "sim/castle_instance.h"

#include "core/log.h"
#include "sim/realm.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace mu::sim {

void CastleInstance::set(Realm& r, int castle) {
    if (r.event() != EventKind::Castle) return;
    castle = std::clamp(castle, 1, kCastles);
    this->castle = castle;
    const auto kindNumbered = [&](int32_t number) {
        for (size_t i = 0; i < r.tables_->kinds.size(); ++i) {
            if (r.tables_->kinds[i].number == number) return int32_t(i);
        }
        return int32_t(-1);
    };
    // One of the three statues, as WebZen raises one at random (BloodCastle.cpp:2216) -- of
    // those with a figure cooked, so none stands invisible.
    int32_t statues[3];
    int drawn = 0;
    for (int32_t number : kCastleStatues) {
        const int32_t kind = kindNumbered(number);
        if (kind >= 0 && !r.tables_->kinds[size_t(kind)].figure.empty()) statues[drawn++] = number;
    }
    const int32_t statue = drawn > 0 ? statues[r.dice_.nextInt(0, drawn)] : kCastleStatue;
    core::logf("castle: Blood Castle %d, the statue %d of %d standing", castle, statue, drawn);
    for (Body& one : r.bodies_) {
        if (!one.monster()) continue;
        const int32_t number = r.tables_->kinds[size_t(one.kind)].number;
        int32_t want = number;
        if (castleStatue(number)) {
            want = statue;
        } else {
            for (int slot = 0; slot < 6; ++slot) {
                if (kCastleBreeds[0][slot] == number) want = kCastleBreeds[castle - 1][slot];
            }
        }
        const int32_t kind = kindNumbered(want);
        if (kind < 0) continue;
        // As Realm::raise dresses a body in its kind.
        const content::MonsterKind& row = r.tables_->kinds[size_t(kind)];
        one.kind = kind;
        one.level = row.level;
        one.maxHealth = castleStatue(want) ? kCastleStatueHealth[castle - 1] : row.health;
        one.stats.level = row.level;
        one.stats.attackRate = row.attackRate;
        one.stats.defenseRate = row.defenseRate;
        one.stats.defense = row.defense;
        one.stats.minimumDamage = row.minimumDamage;
        one.stats.maximumDamage = row.maximumDamage;
        one.swingTicks = row.attackTicks;
        one.speed = 1.0f / float(std::max(1, row.moveTicks));
    }
}

void CastleInstance::freeRun(Realm& r) {
    if (r.event() != EventKind::Castle) return;
    *this = CastleInstance{};
    for (const GridBox& box : {kCastleEntrance, kCastleBridge, kCastleDoor[0], kCastleDoor[1],
                               kCastleDoor[2]}) {
        r.changeGrid(box.x1, box.y1, box.x2, box.y2, box.bits, false);
    }
    // The garrison kept down for good, and the Statue of Saint stood in its hall at once, to be
    // looked at (the user, 2026-10-03: 'did not see the archachel statue').
    for (Body& one : r.bodies_) {
        if (!one.monster()) continue;
        if (castleStatue(r.tables_->kinds[size_t(one.kind)].number)) {
            if (!one.alive()) one.risesAt = r.tick_;
        } else {
            one.health = 0;
            one.risesAt = std::numeric_limits<int64_t>::max();
        }
    }
}

void CastleInstance::tick(Realm& r) {
    // Each player's hand-in and thanks, in the order they joined. The castle's run is the
    // world's; its win goes to whoever hands the staff in.
    for (size_t p = 0; p < r.heroes_.size(); ++p) {
        Realm::For him(r, p);
        // The staff given back, inside the tick so what it says is this tick's (as enterCastle).
        // "Ah! Great warrior..." (ServerCmd 1,23, NpcTalk.cpp:1461-1588), and GiveReward_Win.
        if (r.me().event.staffOwed) {
            r.me().event.staffOwed = false;
            const int slot = r.staffSlot();
            if (slot >= 0 && phase == CastlePhase::Running) {
                r.me().bag.lift(slot);
                Body& hero = r.mine();
                const int64_t seconds = r.castleSecondsLeft();
                // This castle's pay (sim/event.h): castle 1's 20,000 / 5,000 / 160 a second / 20,000
                // Zen up to castle 6's 110,000 / 30,000 / 260 / 250,000.
                const int c = std::clamp(castle, 1, kCastles) - 1;
                const int64_t experience =
                    (statueBroken ? kCastleStatueExps[c] : 0) + kCastleHandInExps[c] +
                    seconds * kCastleExpPerSeconds[c];
                // At the game's experience rate, as every kill's (rules.h kExperienceRate), and the
                // better win's (kCastleExpTimes, kCastleZenTimes).
                paidExperience = int64_t(double(experience * kCastleExpTimes) * kExperienceRate);
                paidZen = kCastleWinZens[c] * kCastleZenTimes;
                phase = CastlePhase::Won;
                winner = hero.id;
                // And the castle is theirs again: every monster in it gone on the tick, out of the
                // picture without a fall, and none rises for the rest of the run (the user,
                // 2026-10-05: 'when quest is given all monsters have to desepear'). Ours.
                for (Body& one : r.bodies_) {
                    if (!one.monster()) continue;
                    one.risesAt = std::numeric_limits<int64_t>::max();
                    if (!one.alive()) continue;
                    one.health = 0;
                    one.quarry = 0;
                    r.halt(one);
                    r.dropBlow(one);
                    r.say(What::Dismissed, one);
                }
                hero.quarry = 0;
            }
        }
        // Complete on his thanks: the win paid, and out to Devias now rather than after the rest.
        if (r.me().event.claimOwed) {
            r.me().event.claimOwed = false;
            if (phase == CastlePhase::Won && !claimed && r.mine().id == winner) {
                pay(r);
                r.me().event.angeling = -1;
                leavesAt = r.tick_;
                sentOut = true;
            }
        }
    }
    if (phase == CastlePhase::Waiting && r.tick_ >= startsAt) {
        // "Blood Castle 1 quest has begun" (lMsg 1161): the barrier lifted, the clock started.
        phase = CastlePhase::Running;
        endsAt = r.tick_ + int64_t(kCastleRun) * kCastleTicksPerSecond;
        // Quota 1, by who is alive in the castle now (SetMonsterKillCount).
        killsWanted = kCastleKillsEach * std::max(1, r.castleLiving());
        r.changeGrid(kCastleEntrance.x1, kCastleEntrance.y1, kCastleEntrance.x2, kCastleEntrance.y2,
                   kCastleEntrance.bits, false);
    } else if (phase == CastlePhase::Running && r.tick_ >= endsAt) {
        phase = CastlePhase::Ended;
    }
    // The drawbridge down: its gap is ground as the door lands (kCastleBridgeTicks), and the
    // castle's door open behind it, the courtyard with it -- ours (the user, 2026-10-03: 'when
    // gates drop allopw to go inside'); WebZen opens the door when the Castle Gate dies.
    if (bridgeAt >= 0 && !bridgeDown && r.tick_ >= bridgeAt + kCastleBridgeTicks) {
        bridgeDown = true;
        for (const GridBox& box : {kCastleBridge, kCastleDoor[0], kCastleDoor[1], kCastleDoor[2]}) {
            r.changeGrid(box.x1, box.y1, box.x2, box.y2, box.bits, false);
        }
        // And quota 2's Spirit Sorcerers rise in the courtyard, as many as it asks -- 2 for each
        // player alive now, at most 10 and at most the castle's 8 -- raised there when the gate
        // falls (gObjMonster.cpp:1302-1308, SetBossMonster).
        sorcerersWanted = std::min(kCastleMostSorcerers,
                                   kCastleSorcerersEach * std::max(1, r.castleLiving()));
        int raised = 0;
        for (Body& one : r.bodies_) {
            if (raised >= sorcerersWanted) break;
            if (!one.monster() || one.alive()) continue;
            if (!castleSorcerer(r.tables_->kinds[size_t(one.kind)].number)) continue;
            one.risesAt = r.tick_;
            ++raised;
        }
    }
    // The run over: a minute's rest, then out to Devias.
    if ((phase == CastlePhase::Won || phase == CastlePhase::Ended) && leavesAt < 0) {
        leavesAt = r.tick_ + int64_t(kCastleRest) * kCastleTicksPerSecond;
    }
    if (leavesAt >= 0 && !sentOut && r.tick_ >= leavesAt) {
        // Won and never claimed: paid on the way out to whoever won it, so the win is not lost.
        if (const int at = r.playerOfId(winner);
            phase == CastlePhase::Won && at >= 0 && !r.bodies_[r.heroes_[size_t(at)].body].gone) {
            Realm::For him(r, size_t(at));
            pay(r);
        }
        sentOut = true;
    }
}

void CastleInstance::pay(Realm& r) {
    if (claimed) return;
    claimed = true;
    Body& hero = r.mine();
    const int c = std::clamp(castle, 1, kCastles) - 1;
    r.gain(hero, int32_t(std::min<int64_t>(paidExperience, INT32_MAX)));
    r.me().money += paidZen;
    // And the castle's jewels, one each (BloodCastle.dat "Reward Items"), into the bag as a
    // quest's pay goes (the user, 2026-10-04: 'when finish quest on BC put items on bag similiar
    // like receving quests'); WebZen lays them at his feet, and one the bag cannot hold still
    // falls there -- the win is not refused, as a quest's hand-in is.
    for (const auto& jewel : kCastleRewardJewels[c]) {
        if (jewel[0] < 0) break;
        const int32_t item = r.tables_->itemAt(jewel[0], jewel[1]);
        for (int i = 0; item >= 0 && i < jewel[2]; ++i) {
            if (r.give(item) < 0) r.lay(item);
        }
    }
}

void CastleInstance::kill(Realm& r, const Body& dead) {
    if (phase != CastlePhase::Running) return;
    const int32_t number = r.tables_->kinds[size_t(dead.kind)].number;
    // The statue is the run's target, not one of its garrison: broken, it pays its bonus
    // (kCastleStatueExp) and counts toward neither quota.
    if (castleStatue(number)) {
        // Broken, it lets fall the weapon it held -- the staff, the sword or the crossbow, by
        // which of the three it was -- on the stone where it lay, to be carried back to the
        // Archangel (the user, 2026-10-03; docs/blood-castle-port.md 5). Ours: WebZen's statue
        // drops nothing by the drop table (gObjMonster.cpp:3985) and its weapon comes by another
        // way. It lies there until the run's time is up.
        statueBroken = true;
        const int* weapon = kArchangelWeapons[std::clamp(number - kCastleStatues[0], 0, 2)];
        const int32_t staff = r.tables_->itemAt(weapon[0], weapon[1]);
        if (staff >= 0) {
            const uint32_t id = r.lay(staff, 0, false, 0, 0, 0);
            if (!r.lying_.empty() && r.lying_.back().id == id) {
                std::tie(r.lying_.back().column, r.lying_.back().row) =
                    r.clearing(dead.column(), dead.row());
                r.lying_.back().vanishesAt = endsAt;
            }
        }
    } else if (castleSorcerer(number)) {
        // Quota 2 met: the Statue of Saint rises in its hall, and the dead sorcerers, this one
        // with them, rise no more (Realm::kill set them rising again).
        if (++sorcerers == sorcerersWanted) {
            for (Body& one : r.bodies_) {
                if (!one.monster() || one.alive()) continue;
                const int32_t kind = r.tables_->kinds[size_t(one.kind)].number;
                if (castleStatue(kind)) one.risesAt = r.tick_;
                if (castleSorcerer(kind)) one.risesAt = std::numeric_limits<int64_t>::max();
            }
        }
    } else {
        ++kills;
    }
    // Quota 1: "monsters cleared! attack the castle gate" (lMsg 1168), and the drawbridge falls.
    if (kills >= killsWanted && bridgeAt < 0) bridgeAt = r.tick_;
}

void CastleInstance::dropBridge(Realm& r, int seconds) {
    if (r.event() != EventKind::Castle) return;
    if (phase == CastlePhase::Waiting) startsAt = r.tick_;
    kills = std::max(kills, killsWanted);
    bridgeAt = r.tick_ + int64_t(seconds) * kCastleTicksPerSecond;
}

}  // namespace mu::sim
