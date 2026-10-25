// Throwing a skill: what a key press asks for, the refusals in OpenMU's own order, the cooldown
// it sets, and the knock at the far end.
//
// `docs/game/skills-dk.md` is the design and `sim/skills.cpp` is the table. Three things about the
// shape are worth having at the top, because each of them was a decision:
//
//   * **A press is not an order.** `invoke` remembers a wish; it never replaces the standing
//     attack order. So the knight opens on auto-attack, presses a key, spends that swing on the
//     skill and goes on swinging the same monster -- which is the fight the user described and is
//     why nothing here touches `me().order`.
//   * **A cast pays the swing timer as well as its own cooldown.** The swing clock is the length
//     of the clip he swings with (`sim/swings.cpp`), so it is the wall that stops any amount of
//     haste firing a skill inside its own animation. MU2's `Realm.Cast` takes the longer of the
//     two for the same reason and its remark is the argument.
//   * **Every refusal is silent.** The interface asks and redraws from the realm; a no is a
//     message that does not come back. `Swing` and `Move` already work that way.
#include "sim/realm.h"
#include "sim/fmath.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/log.h"
#include "sim/realm_tuning.h"
#include "sim/skills.h"
#include "sim/swings.h"

namespace mu::sim {
namespace {

// How long a press is held for before it is forgotten: a little over a swing, so a key pressed
// on any frame of the current blow is thrown by the one after it. **invention**, and the number
// is small on purpose -- a wish held for a second is a knight who casts at something he stopped
// aiming at. MU2 keeps its `Wants` on the same argument and bounds it by the swing instead.
constexpr int64_t kWishTicks = 30;

// A Teleport's fade out, in ticks: MU takes a tenth off the body's alpha a frame
// (ZzzInterface.cpp:2603), ten frames, 0.4 s. He is put down when it has run.
constexpr int64_t kBlinkFadeTicks = 8;
// And how long after he is put down before he may act again: the first of the fade back in.
// Ours -- MU plays the clip's last eight keys, a second, and a blink held that long read slow.
constexpr int64_t kBlinkSettleTicks = 4;


}  // namespace

void Realm::invoke(int32_t skill, uint32_t at) {
    const Body& hero = mine();
    if (!hero.alive()) return;
    if (skillIndexOf(skill) < 0) return;
    me().wants = skill;
    me().wantsAt = at;
    me().wantsColumn = me().wantsRow = -1;
    me().wantsBody = 0;
    me().wantsUntil = tick_ + kWishTicks;
}

void Realm::invokeAt(int32_t skill, int column, int row, uint32_t on) {
    invoke(skill, 0);
    me().wantsColumn = column;
    me().wantsRow = row;
    me().wantsBody = on;
}

bool Realm::learn(int32_t skill) {
    const int index = skillIndexOf(skill);
    if (index < 0) return false;
    Body& hero = mine();
    const uint64_t bit = uint64_t(1) << index;
    if ((hero.learned & bit) != 0) return false;
    hero.learned |= bit;
    say(What::Learned, hero, skill);
    return true;
}

bool Realm::knows(int32_t skill) const {
    const int index = skillIndexOf(skill);
    if (index < 0) return false;
    // A mount's skill is the horn's, as 0.75's skills were the weapon's: known while it is worn
    // and not saved -- for his class alone, as MuMain marks it the knight's on the horn.
    const SkillRow& row = skillAt(index);
    if (row.mounted) return skillFor(row, mine().kin) && mountWorn(row.horn);
    return (mine().learned & (uint64_t(1) << index)) != 0;
}

bool Realm::mountWorn(int32_t number) const {
    const Held& horn = me().bag[kMount];
    if (horn.empty() || horn.durability <= 0 || tables_ == nullptr ||
        size_t(horn.item) >= tables_->items.size()) {
        return false;
    }
    const content::ItemRow& row = tables_->items[size_t(horn.item)];
    return row.group == kGroupPets && row.number == number;
}

int64_t Realm::cooling(int32_t skill) const {
    const int index = skillIndexOf(skill);
    if (index < 0) return 0;
    return std::max<int64_t>(0, mine().cools[index] - tick_);
}

int32_t Realm::coolsFor(int32_t skill) const {
    const SkillRow* row = skillNumbered(skill);
    if (!row) return 0;
    const Body& hero = mine();
    return cooldownTicks(*row, hero.totalPoints().agility, floorTicksFor(*row, clipTicksOf(hero, *row)));
}

int32_t Realm::clipTicksOf(const Body& hero, const SkillRow& row) const {
    if (!tables_) return 0;
    // The same two hands `reswing` reads, for the same reason: the play speed a clip runs at has
    // the weapon's own attack speed in it.
    const content::Arm* right = hero.weapon >= 0 && size_t(hero.weapon) < tables_->arms.size()
                                    ? &tables_->arms[size_t(hero.weapon)]
                                    : nullptr;
    const content::Arm* left = hero.shield >= 0 && size_t(hero.shield) < tables_->arms.size()
                                   ? &tables_->arms[size_t(hero.shield)]
                                   : nullptr;
    // And his talents': attack speed on a skill's clip, casting speed on a spell's.
    const float talented = row.wizardry ? hero.talentShare.castSpeed
                                        : hero.talentShare.attackSpeed + float(hero.talentSpeed(tick_));
    return castTicks(*tables_, hero.kin, hero.totalPoints().agility, right, left, row,
                     hero.castSpeed(tick_) + int(talented));
}

// The refusals, in the order `TargetedSkillDefaultPlugin` refuses them, with the two this
// design changes marked. The order is behaviour and not presentation: mana is taken after the
// reach test and before the blow, so a knight who is too far away has not paid for the press.
bool Realm::throwSkill(Body& hero, const SkillRow& row, uint32_t at) {
    const int index = skillIndexOf(row.number);
    if (index < 0) return false;

    // Learned. In 0.75 this question was asked of his hands; here it is asked of what he has
    // read, which is the one place the design leaves the original on purpose.
    // A mount's skill asks the horn instead (`knows`).
    if (row.mounted ? !knows(row.number) : (hero.learned & (uint64_t(1) << index)) == 0) {
        return false;
    }
    // And his class's. Learning already asks it -- an orb or a scroll refuses the wrong class --
    // so this is the same answer asked again where the skill is spent.
    if (!skillFor(row, hero.kin)) return false;
    if (secondSkill(row) && !hero.second) return false;

    // The cooldown, which is ours and has no counterpart in 0.75. A wait rather than a refusal,
    // exactly as the swing timer is: the key does nothing and says nothing. A summon key still
    // dismisses the one standing while it cools: that costs nothing and raises nothing.
    // **But not in its first two seconds** (the user's elf, 2026-10-18: E pressed four times
    // while the cast waited on her swing; the fourth press dismissed the Golem the third had
    // just raised, and the key cooled a minute with nothing standing). Read off the key's own
    // clock, so a summon raised without a cast (Realm::shelterSummon, a save's) is not held.
    const bool dismisses = row.summons > 0 && me().summonSlot >= 0 &&
                           bodies_[size_t(me().summonSlot)].alive() &&
                           hero.cools[size_t(index)] - tick_ <= coolsFor(row.number) - kSummonKeepTicks;
    if (tick_ < hero.cools[size_t(index)] && !dismisses) return false;

    // **The hand, and now it is the RIGHT hand rather than any hand.** The user's rule of
    // 2026-09-22 was "nothing is thrown bare-handed, nothing off a bow"; the rule of 2026-09-23
    // is narrower and is the reason this project has a `families` column at all -- a skill
    // belongs to a kind of weapon and is thrown with that kind or not at all (§3.1b). For the
    // five 0.75 carried this is the original restored rather than a new gate: in 0.75 the weapon
    // WAS the skill, so an axe's Falling Slash could never be thrown off a sword; learning made
    // it possible and this line takes it back.
    //
    // Empty hands, a bow, a crossbow, a staff and a shield in the right hand are all
    // `arms::kNone`, so the old rule falls out of the new one rather than sitting beside it.
    if (!row.onSelf()) {
        const content::Arm* weapon = hero.weapon >= 0 &&
                                             size_t(hero.weapon) < tables_->arms.size()
                                         ? &tables_->arms[size_t(hero.weapon)]
                                         : nullptr;
        if (!row.suits(familyOf(weapon))) return false;
    }

    // `player.IsAtSafezone()` refuses everything, buffs included -- so a knight cannot even
    // raise his guard in Lorencia's square. The same rule `press` already applies to a swing.
    // Ours: Blood Castle's court lets him raise his auras for the fight before the barrier lifts
    // (the user, 2026-10-04: 'allow to cast auras on BC waiting zone to prepare for fight') --
    // a boon, a mend or a might on himself, not a summon, and nothing aimed.
    // **And Summon in town**, as MU casts it there (WebZen CGMagicAttack lets it through,
    // protocol.cpp:15230-15299): his party lands round him on the nearest free tiles
    // (Realm::recallTile).
    if (tables_->grid.safe(hero.column(), hero.row()) &&
        !(row.aura() && event() == EventKind::Castle) && !row.recalls) {
        return false;
    }
    // And a mount's skill only on the mount: off it -- on a safe tile, or the horn's life gone this
    // tick -- there is nothing under him to breathe.
    if (row.mounted && !hero.riding) return false;
    // And Power Slash never on one: MuMain's click does nothing on Uniria, Dinorant, Dark Horse
    // or Fenrir (ClassAttack.cpp:660-697), and the bar greys it there.
    if (row.afoot && hero.riding) return false;

    // **A blink** (Teleport): no body, no blow -- the ground the key named, a fade, and him put
    // down there. The fight he was in is dropped, as a Town Portal drops it: left standing, the
    // order walked him straight back to what he had just left.
    if (row.blinks) {
        int column = 0, where = 0;
        if (me().wantsColumn < 0 ||
            !blinkTo(hero, row, me().wantsColumn, me().wantsRow, &column, &where)) {
            return false;
        }
        if (hero.mana < manaCostOf(hero, row)) return false;
        hero.mana -= manaCostOf(hero, row);
        const int32_t cool = cooldownTicks(row, hero.totalPoints().agility,
                                           floorTicksFor(row, clipTicksOf(hero, row)));
        hero.cools[size_t(index)] = tick_ + cool;
        hero.blinkAt = tick_ + kBlinkFadeTicks;
        hero.blinkColumn = column;
        hero.blinkRow = where;
        hero.swingsAt = hero.castUntil = hero.blinkAt + kBlinkSettleTicks;
        hero.castBreaks = false;
        hero.castWaits = false;
        hero.aim = fm::atan2(float(where) - hero.y, float(column) - hero.x);
        hero.walking = false;
        route(hero).clear();
        hero.onStep = 0;
        dropBlow(hero);
        me().order = Request{};
        me().pending = Request{};
        rise(hero);
        say(What::Cast, hero, row.number, cool, 0, hero.id);
        return true;
    }

    // **A charge** (Nova). **Ours: no gathering** -- the user, 2026-10-07: 'in our version of the
    // game we skiping charging part and use only ground hitting part / release'. Pressed, the
    // whole charge is paid at once, every stage's mana, and let go at its last stage on this
    // tick (burstCharge): he leaps and it bursts as he lands. Realm::chargeTick is kept for a
    // charge that is held, which nothing begins now.
    if (row.chargeTicks > 0) {
        const int32_t price = manaCostOf(hero, row) * row.chargeStages;
        if (me().charge.skill != 0 || hero.mana < price) return false;
        hero.walking = false;
        route(hero).clear();
        hero.onStep = 0;
        dropBlow(hero);
        me().order = Request{};
        me().pending = Request{};
        hero.mana -= price;
        me().charge = Charge{row.number, row.chargeStages, tick_, true};
        hero.castBreaks = false;
        hero.castWaits = false;
        burstCharge(hero);
        return true;
    }

    // Who a self-cast fell on: herself, or the one an elf's aid reached. Said with the cast, so
    // its burst and ribbons rise on him (Play::update).
    uint32_t aided = hero.id;
    if (row.onSelf()) {
        // **And the guard needs a shield on the arm** -- the wizard's Soul Barrier as much as
        // the knight's Defense, the user's of 2026-09-28. The user's rule, 2026-09-22, and it is
        // 0.75's own arrangement put back: skill 18 is carried by the Buckler and the nine
        // shields after it (`Version075/Items/Armors.cs:40`), so in the original a knight without
        // one simply did not have Defense. Learning it permanently took that away, and this is
        // the line that gives it back -- the same shape as the weapon test above, one hand each.
        const content::Arm* shield = hero.shield >= 0 &&
                                             size_t(hero.shield) < tables_->arms.size()
                                         ? &tables_->arms[size_t(hero.shield)]
                                         : nullptr;
        // Asked through the same column as an attack's, because `arms::kShield` is a family:
        // one question -- does this hand suit this row -- rather than two rules that can drift.
        // A Bulwark answers for the shield (sim::armFamily).
        if (!row.suits(armFamily(hero, shield))) return false;
        // The share a guard of hers or his takes off a blow, off the caster's stats and shield as
        // they stand at the cast: `guardShare` in sim/skills.h, where the numbers are argued --
        // or the wizard's `barrierShare`, off energy where the knight's is off his body, or the
        // elf's `wardShare`, off agility with no shield at all.
        // A Buckler or a Brace with no shield on his arm lets it up at its share.
        const auto guardOf = [&] {
            float guard = boonShare(row, hero.totalPoints(), hero.shieldDefense);
            if (row.number != skill::kGreaterDefense && hero.excel.bulwark &&
                hero.excel.bulwarkShare < 1.0f && (familyOf(shield) & arms::kShield) == 0) {
                guard *= hero.excel.bulwarkShare;
            }
            // His talents: Bastion on Defense and Mana Ward on Magic Shield lift it past
            // kGuardCap; Blessed Hands on her ward stays under it (sim/talents.h).
            const TalentShares& t = hero.talentShare;
            if (t.guardEffect != 0 && row.number == skill::kDefense) guard *= 1.0f + t.guardEffect / 100.0f;
            if (t.barrierEffect != 0 && row.number == skill::kSoulBarrier) guard *= 1.0f + t.barrierEffect / 100.0f;
            if (t.buffEffect != 0 && row.number == skill::kGreaterDefense) {
                guard = std::min(kGuardCap, guard * (1.0f + t.buffEffect / 100.0f));
            }
            return guard;
        };
        // A self-cast has no target to be far from and reads its victim off the caster -- but for
        // **an elf's Heal, Greater Defense and Greater Damage on another player** (SkillRow::aids:
        // the user's of 2026-10-09, "elfs can heal", for her party; of 2026-10-19 for anyone she
        // points at), within kAidReach and reckoned off her own energy and agility, as on herself
        // -- and a wizard's Magic Shield, off his energy and shield (2026-10-21).
        // Aimed at one too far, or gone, it is refused -- nothing paid -- rather than turned on
        // her: the wish stands its half second and lands if he comes into reach (Realm::press).
        Body* aid = nullptr;
        if (row.aids() && at != 0 && at != hero.id) {
            Body* one = body(at);
            if (one == nullptr || !one->player || one->gone || !one->alive() ||
                !within(hero, *one, kAidReach + hero.talentShare.aidReach)) {
                return false;
            }
            // **Never a weaker guard over a stronger** (the user, 2026-10-19: "just dont make
            // defense weaker if char already has stronger shield aura"): her ward on one whose
            // standing guard -- a knight's Defense, a wizard's Soul Barrier, another elf's ward --
            // already takes more of a blow is refused, nothing paid, and his stays. As strong or
            // weaker, hers replaces it. A wizard's Magic Shield on another the same (the user,
            // 2026-10-21: "for wizards it has to be same rule as elf to apply shield only if its
            // better than other players").
            if (row.boonTicks > 0 && one->boonUntil > tick_ &&
                one->boonDamageTaken < 1.0f - guardOf()) {
                // Said to her once, and the wish dropped: held, it was refused every tick.
                say(What::Outguarded, hero, row.number, 0, 0, one->id);
                happenings_.back().audience = hero.id;
                me().wants = skill::kNone;
                return false;
            }
            aid = one;
        }
        aided = aid != nullptr ? aid->id : hero.id;
        // **Summon's refusals, before anything is paid**: no party, an event map (WebZen's
        // CheckMainToMove on him, ObjUseSkill.cpp:3946), or one of his party here already
        // counting down (RecallPartyCheck, :3892-3926). Ours: WebZen takes the mana for all three.
        if (row.recalls) {
            if (hero.party == 0 || event() != EventKind::None) return false;
            for (const Body& one : bodies_) {
                if (&one == &hero || !one.player || one.gone || one.party != hero.party || one.recallAt == 0) {
                    continue;
                }
                say(What::Recall, hero, int32_t(RecallEvent::Busy), 0, 0, hero.id);
                happenings_.back().audience = hero.id;
                me().wants = skill::kNone;
                return false;
            }
        }
        if (hero.mana < manaCostOf(hero, row)) return false;
        hero.mana -= manaCostOf(hero, row);
        if (row.summons > 0) {
            // **A second cast dismisses the one standing**, and costs nothing: OpenMU's
            // TargetedSkillDefaultPlugin.cs:121-125, which removes the summon and returns before
            // the mana is taken. The mana was taken above, so it is handed back.
            Body& summon = bodies_[size_t(me().summonSlot)];
            if (summon.alive()) {
                hero.mana += manaCostOf(hero, row);
                dismiss(summon);
                return true;
            }
            if (!conjure(hero, row)) {
                hero.mana += manaCostOf(hero, row);
                return false;
            }
        } else if (row.mends) {
            // Heal: health back at once, never past the most he has. And its threat, on the
            // monsters after him, when she is of a party (Realm::healThreat).
            Body& to = aid != nullptr ? *aid : hero;
            const int before = to.health;
            // Her talents (sim/talents.h): Mending on every heal, Patron on a party member's.
            int heal = healOf(hero.totalPoints());
            const float more = talentAid(hero, to) + hero.talentShare.healing;
            if (more != 0.0f) heal = int(float(heal) * (1.0f + more / 100.0f));
            to.health = std::min(to.maxHealth, to.health + heal);
            healThreat(hero, to, to.health - before);
            if (&to != &hero && hero.talentShare.any) {
                // Shared Light: a share of a heal on another back to her.
                const float shared = talentSpecial(hero.kin, hero.talents, Special::SharedLight, TalentAsk{});
                if (shared > 0.0f && to.health > before) {
                    hero.health = std::min(hero.maxHealth, hero.health + int(float(to.health - before) * shared / 100.0f));
                }
                // Mercy: a chance the healed takes less for a while.
                const TalentEffect* mercy = nullptr;
                const float spare = talentSpecial(hero.kin, hero.talents, Special::Mercy, TalentAsk{}, &mercy);
                if (spare > 0.0f && mercy && talentDice_.nextInt(0, 100) < mercy->chance) {
                    to.mercyUntil = tick_ + int64_t(mercy->seconds) * kTalentTicksPerSecond;
                    to.mercy = spare;
                }
            }
        } else if (row.recalls) {
            recallParty(hero);
        } else if (row.sharpens) {
            // Critical Damage: off his Command and energy now, on him and on every member of his
            // party on this map within ten whole tiles (`gObjCalDistance < 10`), himself alone
            // outside one (ObjUseSkill.cpp:4150-4215). A recast replaces it rather than stacking --
            // but a weaker lord's not a stronger one's still standing (ours, the ward's rule).
            const int32_t keen = keenOf(hero.totalPoints());
            const int64_t until = tick_ + keenTicksOf(hero.totalPoints());
            for (Body& one : bodies_) {
                const bool his = &one == &hero;
                if (!his && (hero.party == 0 || !one.player || one.party != hero.party || !one.alive())) continue;
                if (!his) {
                    const float dx = float(one.column() - hero.column()), dy = float(one.row() - hero.row());
                    if (int(std::sqrt(dx * dx + dy * dy)) >= 10) continue;
                }
                if (one.keenUntil > tick_ && one.keen > keen) continue;
                one.keen = keen;
                one.keenUntil = until;
                For theirs(*this, one);
                rearm(one);
            }
        } else if (row.swellTicks > 0) {
            // Swell Life: on him and every member of his party alive within kPartyReach, as MU's
            // ImplicitParty, at the share his points and their count give (swellShare). The most
            // health rises and the health does not -- MU's bar falls back as the cap goes up --
            // so a recast is never a heal. A stronger swell standing on one is left standing.
            Body* on[kPartyMost];
            int members = 0;
            if (hero.party == 0) {
                on[members++] = &hero;
            } else {
                for (const Player& player : heroes_) {
                    Body& one = bodies_[player.body];
                    if (one.gone || !one.alive() || one.party != hero.party || members == kPartyMost) continue;
                    const float dx = one.x - hero.x, dy = one.y - hero.y;
                    if (&one != &hero && dx * dx + dy * dy >= kPartyReach * kPartyReach) continue;
                    on[members++] = &one;
                }
            }
            const float share = swellShare(hero.totalPoints(), hero.party == 0 ? 0 : members);
            for (int i = 0; i < members; ++i) {
                Body& to = *on[i];
                if (&to != &hero && to.swellUntil > tick_ && to.swell > share) continue;
                const int had = to.health;
                to.swell = share;
                to.swellUntil = tick_ + row.swellTicks;
                For theirs(*this, to);
                rearm(to);
                to.health = std::min(to.maxHealth, had);
            }
        } else if (row.mightTicks > 0) {
            // Greater Damage: reckoned off her energy now and held for the minute; a second cast
            // replaces the first rather than stacking, as MU's magic effects do.
            Body& to = aid != nullptr ? *aid : hero;
            to.might = mightOf(hero.totalPoints());
            // Blessed Hands, and Patron on a party member's (sim/talents.h).
            if (const float more = hero.talentShare.buffEffect + talentAid(hero, to); more != 0.0f) {
                to.might = int32_t(float(to.might) * (1.0f + more / 100.0f));
            }
            to.mightUntil = tick_ + row.mightTicks;
            For theirs(*this, to);
            rearm(to);
        } else if (aid != nullptr) {
            // Greater Defense or Magic Shield on another player: her ward, off her own agility, or
            // his barrier, off his energy and shield, on him. It takes the one boon he has -- a
            // knight's Defense guard among them.
            aid->boonSkill = row.number;
            aid->boonLent = true;
            aid->boonDamageTaken = 1.0f - guardOf();
            aid->boonUntil = tick_ + row.boonTicks;
            aid->stats.damageTaken = double(aid->boonDamageTaken) * aid->pet.taken;
        } else {
            hero.boonSkill = row.number;
            hero.boonLent = false;
            // The guard's share as it stands at the cast (guardOf, above), held for its whole
            // length.
            hero.boonDamageTaken = 1.0f - guardOf();
            hero.boonUntil = tick_ + row.boonTicks;
            hero.stats.damageTaken = double(hero.boonDamageTaken) * hero.pet.taken;
        }
    } else {
        Body* target = body(at);
        // The reach, and it is the knight's own. 0.75 gave each skill a range and then added two
        // tiles of slack because three of the five were thrown from a step or two out and closed
        // the gap themselves; nothing closes a gap here (docs/game/skills-dk.md §3.1a), so the test
        // is the swing's.
        const bool aimed = target && target->alive() && target->monster() &&
                           within(hero, *target, row.reach + float(bulkOf(numberOf(*target))));
        // Behind a wall: nothing is thrown at it -- no mana, no cooldown (Realm::seen).
        const bool walled = aimed && !seen(hero, *target);
        // Aimed before the shape is measured, because Arc is measured off where he is looking.
        // Only the aim is set and not the facing: he turns to it at the body's own rate, as he
        // does for a swing (`engage`), and the blow lands half a clip later by which time he has
        // come round. Set even on a refusal below -- a knight turns toward what he tried to hit.
        // **But a refusal does not turn him** (the user, 2026-10-04: out of mana, a Meteorite
        // held and a click to walk, he moonwalked): a wish he cannot pay for is asked again every
        // tick while he walks, and each ask snapped him to the spot behind him. His facing is put
        // back on any refusal below, and the pointer's aim with it; a knight still turns his aim
        // toward a body he tried to hit, as before.
        const float wasAim = hero.aim, wasFacing = hero.facing;
        bool paid = false, pointedAim = false;
        struct Unturn {
            Body& body;
            const float& aim;
            const float& facing;
            const bool& paid;
            const bool& pointed;
            ~Unturn() {
                if (paid) return;
                body.facing = facing;
                if (pointed) body.aim = aim;
            }
        } unturn{hero, wasAim, wasFacing, paid, pointedAim};
        if (aimed) hero.aim = fm::atan2(target->y - hero.y, target->x - hero.x);
        // **A spell turns him at once.** A wizard's clip is cast from the tick it starts, and
        // left to the turn a body coming round from behind began it facing away -- 150 degrees
        // off, the user's "goes to the opposite direction", on two casts in 160 of a hunt. MU
        // snaps the angle onto the target as it attacks (ZzzInterface.cpp:1303, `o->Angle[2] =
        // CreateAngle2D`); the knight's swing keeps its turn, as above.
        // And the Dark Lord's skills: MU snaps him onto the body as he casts Force
        // (SkillCast.cpp:381), its piercing thrust straight down that line.
        if (aimed && (row.wizardry || row.kin == Kin::DarkLord)) hero.facing = hero.aim;
        // **Or at the ground under the pointer**, which wins: a press of a skill with a direction
        // carries the tile the mouse is on (`invokeAt`), and he turns to it and casts that way
        // whatever stands there (SkillRow::aimsAtPointer).
        // **On a monster, at the monster** (bug report #48, 2026-10-20: "even when the cursor is
        // positioned over a mob, the attack ends up missing to the side"): the tile is where the
        // pointer's ray meets the ground, which on a body is behind it and then rounded to a whole
        // tile -- two tiles out, a beam went by him 20 degrees wide. Where he stands now is the aim.
        const Body* pointedOn = me().wantsBody != 0 ? find(me().wantsBody) : nullptr;
        if (pointedOn != nullptr && (!pointedOn->alive() || !pointedOn->monster())) pointedOn = nullptr;
        const float wantX = pointedOn ? pointedOn->x : float(me().wantsColumn);
        const float wantY = pointedOn ? pointedOn->y : float(me().wantsRow);
        const bool pointed = me().wantsColumn >= 0 && row.aimsAtPointer() &&
                             (pointedOn != nullptr || me().wantsColumn != hero.column() ||
                              me().wantsRow != hero.row());
        if (pointed) {
            pointedAim = true;
            hero.aim = fm::atan2(wantY - hero.y, wantX - hero.x);
            if (row.wizardry) hero.facing = hero.aim;
        }
        // Cast at nothing, a storm walks the way he faces (SkillRow::castsBare).
        if (!aimed && !pointed && row.castsBare()) hero.aim = hero.facing;
        // **A shower falls on the ground he named** (SkillRow::showers), his own tile too: a
        // spot past his reach is pulled back along the line to it, as Teleport's is, and one he
        // cannot see over a wall is not called down at all -- no mana, no clip.
        const bool grounded = row.showers() && me().wantsColumn >= 0;
        float spotX = 0.0f, spotY = 0.0f;
        if (grounded) {
            float dx = wantX - hero.x, dy = wantY - hero.y;
            const float far = std::max(std::fabs(dx), std::fabs(dy));
            if (far > row.reach) {
                dx *= row.reach / far;
                dy *= row.reach / far;
            }
            spotX = hero.x + dx;
            spotY = hero.y + dy;
            if (!router_.sees(hero.x, hero.y, spotX, spotY, content::kWallNoMove)) return false;
        }
        if (row.spread == Spread::One || row.spread == Spread::Line ||
            row.spread == Spread::Fan) {
            // A line is thrown AT a body as a single blow is, and goes on through: it needs the
            // body to aim by, and the rest of its way is found when it is let go -- or the
            // pointer's ground, for a shape that has a direction.
            // Or none at all for a spell cast round him (SkillRow::castsBare).
            if (!aimed && !pointed && !grounded && !row.castsBare()) return false;
            if (walled && !pointed && !grounded) return false;
            // Nothing may be thrown at something sheltered either, which is the check the far end
            // of `ApplySkillAsync` makes and `press` already makes for a swing.
            if (aimed && !pointed && !grounded &&
                tables_->grid.safe(target->column(), target->row())) {
                return false;
            }
        } else if (pointed && row.spread == Spread::Beam) {
            // A beam aimed at the pointer goes out whether or not anything stands in it, as MU's
            // does; the realm strikes what is there when it is let go.
        } else {
            // An area skill is not thrown AT a body, it is thrown AROUND him, so what it needs is
            // somebody inside the shape rather than a named target: a knight whose quarry has
            // just died still spins into the three others standing on him. What it will not do is
            // spend mana and a cooldown on empty air, which is what 0.75's "no target, no skill"
            // is really refusing -- except Hellfire's ring, cast on air too (SkillRow::castsBare).
            uint32_t victims[kVictims];
            if (!row.castsBare() && gather(hero, row, victims, kVictims) == 0) return false;
        }
        // A fan is arrows: none in hand or in the bag and there is nothing to loose.
        if (row.arrows > 0 && !quivered(hero)) return false;
        if (hero.mana < manaCostOf(hero, row)) return false;
        hero.mana -= manaCostOf(hero, row);
        paid = true;
        // Held with the blow until his arm is up (Realm::land); `begin` leaves it alone.
        hero.blowGround = grounded;
        hero.blowX = spotX;
        hero.blowY = spotY;
        // **Called on a body, it falls where the body stood at the cast** (the user, 2026-10-04:
        // "dw does meteor cast animation, but meteorits is not flying"): held on a pack, the
        // last rain killed the body the next was called on before his arm came up, and a rain
        // round a corpse let nothing go -- the whole clip for no rocks.
        if (row.showers() && !grounded && aimed) {
            hero.blowGround = true;
            hero.blowX = target->x;
            hero.blowY = target->y;
        }
    }

    // A primary has no cooldown at all -- its clip is its pace, and a key's wipe running down
    // over every Energy Ball would say there was a wait where there is none.
    const int32_t cool =
        row.primary() ? 0
                      : cooldownTicks(row, hero.totalPoints().agility,
                                      floorTicksFor(row, clipTicksOf(hero, row)));
    hero.cools[size_t(index)] = tick_ + cool;
    // **A summon cools every summon** (the user, 2026-10-02: "give elf summon a 1 min cooldown
    // after summon. its global for all summons"), ours: raising the Goblin puts the Golem's key
    // on the same minute, so swapping breeds is no way round it.
    if (row.summons > 0) {
        for (int i = 0; i < kSkills; ++i) {
            if (skillAt(i).summons > 0) hero.cools[size_t(i)] = tick_ + cool;
        }
    }
    // And the swing clock, the longer of his own rhythm and the clip this skill plays. Without
    // the second half a skill whose animation outlasts the weapon's swing is cut off by the next
    // blow -- MU2 measured exactly that on Defense, whose clip is the longest of the six.
    //
    // A spell pays its own clip and not the weapon's: its rhythm is MagicSpeed's and has nothing
    // to do with how fast the staff in his hand would swing.
    const int32_t clip = clipTicksOf(hero, row);
    // **A channel quickened with the clip** (the user, 2026-10-05: 'lighting is not scaling with
    // attack speed ... hands was moving much faster than actual lighting cast speed'): its length
    // and its strike window by the share his casting speed takes off the clip's authored length,
    // the window never fewer ticks than the chain has bodies, so a quick wizard strikes as many.
    const int32_t authored = authoredCastTicks(*tables_, row);
    float haste = row.channelled() && authored > 0 && clip > 0
                      ? std::min(1.0f, float(clip) / float(authored))
                      : 1.0f;
    // Conductor's share off Lightning's channel (sim/talents.h).
    if (row.number == skill::kLightning && hero.talentShare.lightningChannel != 0) {
        haste /= 1.0f + hero.talentShare.lightningChannel / 100.0f;
    }
    const auto quicken = [&](int32_t ticks) {
        return std::max<int32_t>(1, int32_t(std::lround(float(ticks) * haste)));
    };
    const int32_t strikeFrom = quicken(row.strikeFrom);
    const int32_t strikeUntil =
        std::max(quicken(row.strikeUntil),
                 strikeFrom + int32_t(kLightningBodies) + talentChain(hero, row.number) - 1);
    const int32_t channelTicks = std::max(quicken(row.channelTicks), strikeUntil + 1);
    hero.swingsAt = tick_ + (row.channelled() ? channelTicks
                             : row.wizardry  ? std::max<int32_t>(1, clip)
                                             : std::max(hero.swingTicks, clip));
    // And he is locked where he stands for the length of the animation: no turn, no re-path, no
    // step. `press` reads this before it engages, so a quarry that shuffles round him does not
    // spin the body mid-swing.
    //
    // **Not a primary.** The wizard's Energy Ball is thrown over and over, and a lock on every
    // clip would be a wizard who can never be walked away from a fight. Like a swing it is
    // cancelled by a click until it leaves his hand; after that it is in the air and lands.
    // A channel first, primary or not: Lightning has had no cooldown since 2026-09-30 and still
    // roots him for the whole ring.
    //
    // **A knight's skill holds him for the clip the drawing plays**, which is the authored pace:
    // timed off `clip`, quickened by his attack speed, the lock ran out while Twisting Slash was
    // still turning and a click walked him out of the spin (the user, 2026-10-01: "dont allow to
    // use click to move if twisting slash animation is not played to the end"). And no blow of
    // his own begins inside it either. A knight's primary is held as well -- Twisting Slash lost
    // its cooldown the same day, and the walk-out is the wizard's alone.
    //
    // **A primary is held for its quickened clip**, every class's: the user, 2026-10-02 -- "it
    // suppost to speed up all classed" -- of Twisting Slash and Skillshot staying at the authored
    // pace however much agility he had. The drawing plays a primary's clip fitted to `clip`
    // (game/play.cpp), so the hold and the spin still end together; a skill with a cooldown
    // keeps the authored floor.
    //
    // **And every primary is walked out of, every class's**: the user, 2026-10-02 -- "if elf
    // shoots multishot we can cancel it with click to move", then "all primary skills with no
    // cooldown can be canceled ... we dont want that character is moving in mid of casting aura".
    // A click drops the blow until it lands or leaves his hand, Twisting Slash's spin included,
    // which replaces the 2026-10-01 hold on it. What still holds him: a self-cast (an aura, a
    // buff), a channel, and an attack skill with a cooldown.
    //
    // **And a skill with a cooldown is held for its quickened clip too**, as a wizard's spell
    // is: the user, 2026-10-23 -- "we need that animation speed also becomes faster when there
    // is more agi" -- of a knight's Q/W/E/R staying at the authored pace however much agility he
    // had. The drawing fits every cast that is not on himself to `clip` (game/play.cpp). A
    // self-cast's `clip` is its authored length already (castTicks gives it no bonus).
    const int32_t held = clip;
    hero.castUntil = row.channelled() ? tick_ + channelTicks
                     : row.primary()  ? tick_
                                      : tick_ + held;
    hero.swingsAt = std::max(hero.swingsAt, hero.castUntil);
    // **But not an aura**: a self-cast -- a buff, Heal, a summon -- plays to its end and a click
    // inside it is dropped, as Teleport's is (the user, 2026-10-02: "dont allow to cancel any
    // aura casts, click to move not possibel").
    hero.castBreaks = !row.onSelf();
    hero.castWaits = row.onSelf();
    if (row.channelled()) {
        hero.channelSkill = row.number;
        hero.channelFrom = tick_;
        hero.channelUntil = tick_ + channelTicks;
        hero.channelStrikeFrom = strikeFrom;
        hero.channelStrikeUntil = strikeUntil;
        // The first strike when his arm is up in the clip.
        hero.channelNext = tick_ + strikeFrom;
        // The sweep starts where he is facing, and nobody has been struck yet.
        hero.channelTurn = hero.aim;
        hero.channelAim = at;
        hero.channelLast = 0;
        hero.channelStruckCount = 0;
        hero.channelEcho = false;
        // His Ancient spell rune, once a channel as once a cast (Realm::land): a channel never
        // comes down through `land` (sim/items.h kIceStorm...).
        if (hero.player && row.wizardry) {
            Body* aimed = body(at);
            channelRunes(hero, aimed != nullptr ? *aimed : hero);
        }
    }
    hero.walking = false;
    route(hero).clear();
    hero.onStep = 0;
    // **And the auto-attack goes on.** The user's rule, 2026-09-23, which puts back what the
    // first pass did and takes out the `me().order = Request{}` that stood here for a day: a skill is
    // a beat inside the exchange and not the end of it, so the knight spends this swing on the
    // skill and keeps hitting what he was hitting without a second click. That is the fight
    // §3.1a describes -- auto-attack is the floor, the key is the punctuation -- and it is one
    // line either way. MU2's `Realm.Cast` clears its wizard's `Fighting` and is not followed here.

    // Said BEFORE the blow, so the drawing has the skill in hand when the hit arrives and can
    // play the skill's clip instead of the weapon's. MU2 moved its own herald above the blow for
    // this reason and marked the departure; the sparks could not otherwise know a cast happened.
    // On his feet first: nothing is thrown sitting down, a self-cast included.
    rise(hero);
    say(What::Cast, hero, row.number, cool, 0, row.onSelf() ? aided : at);

    if (!row.onSelf() && !row.channelled()) {
        // Begun rather than landed: the blow settles halfway through the clip, as a swing's does
        // (Realm::begin), so what is drawn and what is dealt are the same moment. The multiplier
        // rides with it -- 0.75's own `SkillMultiplier` with strength on it, §3.2 -- and the
        // knock, when a skill has one, belongs to the landing too and is why `shove` is reached
        // from there rather than here.
        //
        // **Over the clip as it is drawn, `held`**: landed at half a `clip` the drawing did not
        // play, the blow came long before the thrust -- a level-150 knight's Death Stab struck
        // while the spear still pointed behind him (2026-10-02, the user: "effect looked
        // buggy"). Since 2026-10-23 every such cast is drawn fitted to `clip`, so `held` is it.
        // **A spell pressed on a monster is a spell at him** (bug report #48, the user: "also
        // power wave"): let go at the body, it turns onto him again as it leaves his hand
        // (Realm::land), so one that walked during the clip is not out of Power Wave's three
        // quarters of a tile. A shower keeps its spot and a knight's arc its way, as before.
        const Body* pressedOn = me().wantsBody != 0 ? find(me().wantsBody) : nullptr;
        const bool atHim = pressedOn != nullptr && pressedOn->alive() && pressedOn->monster() &&
                           me().wantsColumn >= 0 && row.aimsAtPointer() && row.wizardry &&
                           !hero.blowGround;
        begin(hero, atHim ? pressedOn->id : at, heroForce(hero, row), row.number, held);
        // Thrown at the pointer's ground: that way, whatever turns him before it is let go.
        hero.blowAimed = me().wantsColumn >= 0 && row.aimsAtPointer() && !atHim;
        hero.blowAim = hero.aim;
    }
    // The pointer's ground is spent with the press: a later throw off a click is aimed afresh.
    me().wantsColumn = me().wantsRow = -1;
    me().wantsBody = 0;
    return true;
}

// ---- the two area shapes (docs/game/skills-dk.md §3.1a) ------------------------------------------
//
// **Only the area differs, never the range.** Both shapes are centred on the knight and measured
// with his own reach, so neither needs a ground target, a cursor mode or a frustum: Ring is
// everything within a tile of him and Arc is the same ring narrowed to the eighth he faces and
// the two beside it. That is the whole of the geometry, and it is why these are cheap.
//
// **The order is the contract.** Nearest first, then clockwise from north, then by id -- one
// `strike()` and one hit roll a target, in that order, so two runs of the same seed draw the same
// dice in the same sequence and the log stays byte-identical. Walking `bodies_` in index order
// would be deterministic too, but it would put the blow on the monster that spawned first rather
// than the one under his feet, and the log is read by people as well as by diff.
int Realm::gather(const Body& hero, const SkillRow& row, uint32_t* victims, int room) const {
    // What the sort is on, kept beside the id so the comparison never touches a body again.
    float off[kVictims] = {};
    float turnOf[kVictims] = {};
    int found = 0;
    for (const Body& one : bodies_) {
        if (!one.monster() || !one.alive()) continue;
        // A line runs as far as the wave sweeps, past the reach it is aimed within.
        if (!within(hero, one,
                    row.spread == Spread::Line      ? kLineTiles
                    : row.spread == Spread::Beam    ? kBeamTiles
                    : row.spread == Spread::Frustum ? fm::hypot(kFrustumFar, kFrustumDeep)
                                                    : row.reach + talentReach(hero, row.number) +
                                                          float(bulkOf(numberOf(one))))) {
            continue;
        }
        // Sheltered ground is sheltered from a spin as well: the same test a single blow makes.
        if (tables_->grid.safe(one.column(), one.row())) continue;
        // And a wall from all of it: a spin, a wave, a beam or a channel stops at the stone.
        if (!seen(hero, one)) continue;
        const float dx = one.x - hero.x, dy = one.y - hero.y;
        // Clockwise from north, where north is the row decreasing -- the drawing's own negation
        // of the row, borrowed here only to give the sort a stated zero.
        float turn = fm::atan2(dx, -dy);
        if (turn < 0.0f) turn += 6.28318530718f;
        if (row.spread == Spread::Line) {
            // Ahead of him along the aim, and within half the curtain's width of the line.
            const float c = fm::cos(hero.aim), s = fm::sin(hero.aim);
            const float ahead = dx * c + dy * s;
            const float aside = -dx * s + dy * c;
            if (ahead <= 0.0f || ahead > kLineTiles || std::fabs(aside) > kLineHalfWidth) continue;
        }
        if (row.spread == Spread::Beam) {
            // Within a tile and a half of one of the four points MU strikes round, stepped along
            // the aim from his hand (skills.h kBeamStart).
            const float c = fm::cos(hero.aim), s = fm::sin(hero.aim);
            bool struck = false;
            for (int k = 1; k <= kBeamPoints && !struck; ++k) {
                const float along = kBeamStart + kBeamStep * float(k);
                struck = fm::hypot(dx - c * along, dy - s * along) <= kBeamRadius;
            }
            if (!struck) continue;
        }
        if (row.spread == Spread::Frustum) {
            // Power Slash's wedge along the aim: from half a tile behind his middle (the near edge
            // through his own tile) to six out, a tile either side at his feet and six at its far
            // edge.
            const float c = fm::cos(hero.aim), s = fm::sin(hero.aim);
            const float ahead = dx * c + dy * s;
            const float aside = -dx * s + dy * c;
            const float half = kFrustumNear + (kFrustumFar - kFrustumNear) *
                                                  std::max(0.0f, ahead) / kFrustumDeep;
            if (ahead < -kFrustumBehind || ahead > kFrustumDeep || std::fabs(aside) > half) continue;
        }
        if (row.spread == Spread::Arc) {
            // The facing eighth and the two beside it. `aim` and not `facing`, because the throw
            // aims him at what the key named and the body turns to it over the clip; the blow
            // belongs where he threw it. Fire Slash's half-disc is its row's own (arcHalf).
            const float toward = fm::atan2(dy, dx);
            if (std::fabs(wrapped(toward - hero.aim)) > row.arcHalfAngle()) continue;
        }
        if (found >= room) continue;
        // Insertion, because the list is at most a handful long and an insertion sort of a
        // handful is both the fastest thing and the one with no allocation in it.
        const float gap = reach(hero, one);
        int at = found;
        while (at > 0 && (off[at - 1] > gap ||
                          (off[at - 1] == gap &&
                           (turnOf[at - 1] > turn ||
                            (turnOf[at - 1] == turn && victims[at - 1] > one.id))))) {
            off[at] = off[at - 1];
            turnOf[at] = turnOf[at - 1];
            victims[at] = victims[at - 1];
            --at;
        }
        off[at] = gap;
        turnOf[at] = turn;
        victims[at] = one.id;
        ++found;
    }
    return found;
}

void Realm::strikeAround(Body& hero, const SkillRow& row, float force) {
    // A knight's Whirlwind (sim/items.h): Twisting Slash harder, and perhaps the crowd pulled in.
    if (row.number == skill::kTwistingSlash && hero.player && hero.excel.whirlwinds > 0) {
        force = whirl(hero, row, force);
    }
    uint32_t victims[kVictims];
    const int found = gather(hero, row, victims, kVictims);
    // A spell round him -- Hellfire -- rolls his wizardry band off its row, which also lays its
    // element runes (strikeAt), and is said as let go so the drawing lights its circle.
    const bool spell = row.wizardry;
    // His aim rides in `c`, in thousandths of a radian, so Aqua Beam's is drawn down the line it
    // struck (`Spread::Beam`); the rings round him ignore it.
    // Not for a charge's burst, which says its own with its stage (Realm::burstCharge).
    if (spell && row.chargeTicks == 0) {
        say(What::Loosed, hero, row.number, 0, int32_t(std::lround(hero.aim * 1000.0f)), 0);
    }
    // Cyclone's and Twisting Slash's wind, under his element runes (realm_tuning.h).
    if (!spell) force *= elementForce(hero, skillElement(row.number));
    for (int i = 0; i < found; ++i) {
        // Looked up again rather than held: a body killed earlier in this same sweep may have
        // been left where it fell, and `strikeAt` refuses the dead itself. The vector cannot
        // grow inside the loop -- nothing here spawns -- but ids are what survive one that does.
        // A spell's blows are shown on their own tick, with its circle, as a flight's are --
        // not at the drawing's half of the swing (`thrown`).
        if (Body* victim = body(victims[i])) {
            const int before = victim->health;
            sweep_ = spell ? nullptr : &row;
            strikeAt(hero, *victim, force, spell ? &row : nullptr, spell);
            sweep_ = nullptr;
            // Twisting Slash's whirl pays a little back for each body it wounds (kTwistManaShare).
            if (row.number == skill::kTwistingSlash && hero.player && victim->monster() &&
                victim->health < before && hero.mana < hero.maxMana) {
                const int back = std::max(1, int(float(hero.maxMana) * kTwistManaShare));
                hero.mana = std::min(hero.maxMana, hero.mana + back);
            }
        }
    }
}

float Realm::talentAid(const Body& hero, const Body& to) const {
    if (!hero.talentShare.any || &to == &hero || hero.party == 0 || to.party != hero.party) return 0.0f;
    return talentSpecial(hero.kin, hero.talents, Special::Patron, TalentAsk{});
}

bool Realm::armed(const Body& hero, const SkillRow& row) const {
    const int index = skillIndexOf(row.number);
    if (index < 0) return false;
    if (row.mounted ? !knows(row.number) || !hero.riding
                    : (hero.learned & (uint64_t(1) << index)) == 0 || (row.afoot && hero.riding)) {
        return false;
    }
    if (!skillFor(row, hero.kin) || hero.mana < manaCostOf(hero, row) || (secondSkill(row) && !hero.second)) return false;
    // A fan with nothing to loose falls back to the bow, which then says there are no arrows.
    if (row.arrows > 0 && !quivered(hero)) return false;
    const auto armAt = [&](int32_t at) -> const content::Arm* {
        return at >= 0 && size_t(at) < tables_->arms.size() ? &tables_->arms[size_t(at)] : nullptr;
    };
    return row.onSelf() ? row.suits(armFamily(hero, armAt(hero.shield)))
                        : row.suits(familyOf(armAt(hero.weapon)));
}

void Realm::keepBoon(Body& hero) {
    // The guard and the Guardian Angel multiply, as two DamageReceiveDecrement power-ups do.
    hero.stats.damageTaken =
        (hero.boonUntil > tick_ ? double(hero.boonDamageTaken) : 1.0) * hero.pet.taken;
}

void Realm::shove(Body& target) {
    // Nor the raid's dragon: ten runed fighters shoved it tile by tile out of its fight and into
    // a guard's reach (sim/raid.h's bulk). Ours.
    if (fixed(target) || shrugs(target)) return;
    // One tile at random, and only onto something standable: OpenMU's `MoveRandomlyAsync` picks
    // a neighbour and a blocked one is simply not taken, which is what the grid test is. The
    // draw happens whether or not the tile is free, so the seeded log does not depend on the
    // map's walls for its NUMBER of draws -- only for the outcome.
    const int step = dice_.nextInt(0, 8);
    static const int kAround[8][2] = {{0, -1}, {1, -1}, {1, 0},  {1, 1},
                                      {0, 1},  {-1, 1}, {-1, 0}, {-1, -1}};
    const int column = target.column() + kAround[step][0];
    const int row = target.row() + kAround[step][1];
    if (!tables_->grid.open(column, row, content::kWallCharacter)) return;
    target.x = float(column);
    target.y = float(row);
    // The walk it was on is void: it has been put somewhere its route does not start from.
    target.walking = false;
    route(target).clear();
    target.onStep = 0;
    say(What::Shoved, target, column, row);
}

void Realm::channel(Body& hero) {
    if (hero.channelSkill == skill::kNone) return;
    if (tick_ >= hero.channelUntil) {
        // An Arcane Echo (sim/items.h): the sweep once more, a beat after, from where he stands
        // and without his arm -- the clip is not played again and nothing holds him -- striking
        // afresh, each body its share again. The echo's own end does not echo.
        const SkillRow* row = skillNumbered(hero.channelSkill);
        if (row != nullptr && !hero.channelEcho && echoes(hero)) {
            hero.channelEcho = true;
            hero.channelNext = tick_ + kEchoTicks;
            hero.channelFrom = hero.channelNext - hero.channelStrikeFrom;
            hero.channelUntil = hero.channelFrom + hero.channelStrikeUntil + 1;
            hero.channelLast = 0;
            hero.channelStruckCount = 0;
            core::logf("arcane echo: tick %lld, %s sweeps again", (long long)tick_, row->name);
            return;
        }
        hero.channelSkill = skill::kNone;
        hero.channelEcho = false;
        return;
    }
    if (tick_ < hero.channelNext) return;
    const SkillRow* row = skillNumbered(hero.channelSkill);
    if (row == nullptr) {
        hero.channelSkill = skill::kNone;
        return;
    }
    // Past the window, his arm is coming down: the channel runs out without striking.
    if (tick_ > hero.channelFrom + hero.channelStrikeUntil) return;
    hero.channelNext += std::max<int32_t>(1, row->pulseTicks);
    // **A chain** (the user, 2026-10-03: "lightning also is a chain spell like pyroblast rune";
    // until then it went round him, the first body clockwise from the last strike). The first
    // strike goes from his hand into the body he cast at, or the nearest in his reach when that
    // one is gone; each next leaps from the last body struck, from where it stood, to the
    // nearest living monster it has not struck, within the row's reach of it and in its sight,
    // as the Pyroblaster's hop finds its next (Realm::hop). With nowhere to leap the chain ends
    // and the rest of the channel strikes nothing. One `Loosed` a strike, `c` the body it leaps
    // from (0 for his hand), and the blow on the same tick: lightning does not fly. Ties go to
    // the lower id, so the log is fixed.
    // How often this channel has struck a body already.
    const auto times = [&](uint32_t id) -> int {
        for (int k = 0; k < hero.channelStruckCount; ++k) {
            if (hero.channelStruck[k] == id) return hero.channelTimes[k];
        }
        return 0;
    };
    const auto spent = [&](const Body& b) {
        return row->strikesEach > 0 && times(b.id) >= row->strikesEach;
    };
    // Lightning's chain ends at its most (kLightningBodies), his arm held up the rest of the window.
    if (row->number == skill::kLightning &&
        hero.channelStruckCount >= kLightningBodies + talentChain(hero, row->number)) {
        hero.channelNext = hero.channelUntil;
        return;
    }
    Body* next = nullptr;
    if (hero.channelLast == 0) {
        uint32_t victims[kVictims];
        const int found = gather(hero, *row, victims, kVictims);
        // Nearest first, as `gather` sorts them; the aimed body before all of them.
        for (int i = 0; i < found; ++i) {
            Body* victim = body(victims[i]);
            if (victim == nullptr || !victim->alive() || spent(*victim)) continue;
            if (victim->id == hero.channelAim) {
                next = victim;
                break;
            }
            if (next == nullptr) next = victim;
        }
    } else {
        const float x = hero.channelLastX, y = hero.channelLastY;
        float best = row->reach * row->reach;
        for (Body& b : bodies_) {
            if (!b.monster() || !b.alive() || spent(b)) continue;
            const float dx = b.x - x, dy = b.y - y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > best || (d2 == best && next != nullptr && b.id > next->id)) continue;
            if (!router_.sees(x, y, b.x, b.y, content::kWallNoMove)) continue;
            best = d2;
            next = &b;
        }
    }
    if (next == nullptr && hero.channelLast == 0) {
        // **Nobody left for the first strike**: the bolt still leaves his hand, into the body
        // he cast at where it stands or fell, striking nothing, and the chain ends there (the
        // user, 2026-10-04: no cast without its spell).
        if (const Body* aimed = body(hero.channelAim); aimed != nullptr) {
            say(What::Loosed, hero, row->number, 0, 0, aimed->id);
            hero.channelLast = aimed->id;
            hero.channelNext = hero.channelUntil;
        }
        return;
    }
    if (next == nullptr) {
        if (hero.channelLast != 0) {
            core::logf("lightning: tick %lld, the chain ends at #%u", (long long)tick_,
                       hero.channelLast);
            hero.channelNext = hero.channelUntil;
        }
        return;
    }
    // Counted before the blow, so a death under it changes nothing.
    bool counted = false;
    for (int k = 0; k < hero.channelStruckCount; ++k) {
        if (hero.channelStruck[k] == next->id) {
            ++hero.channelTimes[k];
            counted = true;
        }
    }
    if (!counted && hero.channelStruckCount < kVictims) {
        hero.channelStruck[hero.channelStruckCount] = next->id;
        hero.channelTimes[hero.channelStruckCount] = 1;
        ++hero.channelStruckCount;
    }
    say(What::Loosed, hero, row->number, 0, int32_t(hero.channelLast), next->id);
    hero.channelLast = next->id;
    hero.channelLastX = next->x;
    hero.channelLastY = next->y;
    strikeAt(hero, *next, heroForce(hero, *row), row, true);
}

bool Realm::blinkTo(const Body& hero, const SkillRow& row, int column, int row_, int* outColumn,
                    int* outRow) const {
    const float dx = float(column) - hero.x, dy = float(row_) - hero.y;
    const float far = std::sqrt(dx * dx + dy * dy);
    if (far < 1.0f) return false;
    // Pulled back to its reach along the line, and then walked back toward him a quarter tile at a
    // time until a tile will take him: open to a character, not sheltered (MU's `Wall == 0`
    // refuses both), and not the one he stands on.
    const float reach = std::min(far, row.reach);
    for (float along = reach; along >= 1.0f; along -= 0.25f) {
        const int c = int(std::lround(hero.x + dx / far * along));
        const int r = int(std::lround(hero.y + dy / far * along));
        if (c == hero.column() && r == hero.row()) continue;
        if (!tables_->grid.open(c, r, content::kWallCharacter)) continue;
        if (tables_->grid.safe(c, r)) continue;
        *outColumn = c;
        *outRow = r;
        return true;
    }
    return false;
}

void Realm::blink(Body& hero) {
    hero.x = float(hero.blinkColumn);
    hero.y = float(hero.blinkRow);
    hero.blinkAt = 0;
    hero.walking = false;
    route(hero).clear();
    hero.onStep = 0;
    hero.repathsAt = 0;
    // A summon, if one stands, goes with any warp of his (the Town Portal's rule): no summoner
    // casts Teleport in 0.75, so this only keeps the rule whole.
    if (me().summonSlot >= 0) dismiss(bodies_[size_t(me().summonSlot)]);
    say(What::Blinked, hero, hero.blinkColumn, hero.blinkRow);
}

// ---- Summon (docs/plans/dark-lord-spells/summon.md) ------------------------------------------

bool Realm::recallParty(Body& lord) {
    // Each of his party on this map, alive, wherever he stands: his tile drawn now and kept, his
    // seven seconds on his own clock (WebZen ObjUseSkill.cpp:4040-4095). The level a map asks
    // is asked of one coming from another; here he already stands in it.
    int called = 0;
    for (Body& one : bodies_) {
        if (&one == &lord || !one.player || one.gone || !one.alive() || one.party != lord.party) continue;
        int column = lord.column(), row = lord.row();
        recallTile(lord, &column, &row);
        one.recallAt = tick_ + kRecallTicks;
        one.recallColumn = column;
        one.recallRow = row;
        one.recallBy = lord.id;
        say(What::Recall, one, int32_t(RecallEvent::Called), -1, 0, lord.id);
        happenings_.back().audience = one.id;
        ++called;
    }
    say(What::Recall, lord, int32_t(RecallEvent::Called), called, 0, lord.id);
    happenings_.back().audience = lord.id;
    return called > 0;
}

bool Realm::recallTile(const Body& lord, int* column, int* row) {
    // WebZen's: fifty draws in the square four each way, a tile with no attribute at all -- not
    // safe, not stood on, no wall -- and none while he stands on a safe tile himself
    // (gObjCheckTeleportArea, user.cpp:29656-29690). The draws are made either way, so the
    // stream moves the same in town and out.
    const content::Grid& grid = tables_->grid;
    const bool safe = grid.safe(lord.column(), lord.row());
    const auto free = [&](int c, int r) {
        if (!grid.open(c, r, content::kWallCharacter)) return false;
        for (const Body& one : bodies_) {
            if (!one.gone && one.alive() && one.column() == c && one.row() == r) return false;
        }
        return true;
    };
    bool found = false;
    for (int tries = 0; tries < kRecallTries; ++tries) {
        const int c = lord.column() - kRecallReach + recallDice_.nextInt(0, 2 * kRecallReach + 1);
        const int r = lord.row() - kRecallReach + recallDice_.nextInt(0, 2 * kRecallReach + 1);
        if (found || safe || grid.safe(c, r) || !free(c, r)) continue;
        *column = c;
        *row = r;
        found = true;
    }
    if (found) return true;
    // **Ours, where WebZen sets him on the lord's own tile** (:4077-4081, every time in town): the
    // nearest free tile round the lord, ring by ring, safe or not.
    for (int ring = 1; ring <= 8; ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                if (!free(lord.column() + dx, lord.row() + dy)) continue;
                *column = lord.column() + dx;
                *row = lord.row() + dy;
                return true;
            }
        }
    }
    *column = lord.column();
    *row = lord.row();
    return false;
}

void Realm::recall(Body& hero) {
    const auto end = [&](RecallEvent event) {
        hero.recallAt = 0;
        say(What::Recall, hero, int32_t(event), 0, 0, hero.recallBy);
        happenings_.back().audience = hero.id;
    };
    // **A step of his own ends it** -- MU's move packet (PMoveProc, protocol.cpp:14110-14119): a
    // click to walk, or walking in to a fight; striking or casting where he stands does not.
    if (hero.walking) return end(RecallEvent::Cancelled);
    const int64_t left = hero.recallAt - tick_;
    if (left > 0) {
        // "Will be summoned in N seconds.", from five down (user.cpp:25503-25508).
        if (left % 20 == 0 && left / 20 <= 5) {
            say(What::Recall, hero, int32_t(RecallEvent::Count), int32_t(left / 20), 0, hero.recallBy);
            happenings_.back().audience = hero.id;
        }
        return;
    }
    // Down where the lord stood at the cast, the tile not asked again (WebZen's gObjRecall on the
    // same map, user.cpp:32481-32500): what he was doing dropped, as a gate drops it, and what
    // hunted him forgets him.
    hero.recallAt = 0;
    rise(hero);
    dropBlow(hero);
    me().order = Request{};
    me().pending = Request{};
    me().wants = skill::kNone;
    me().trading = -1;
    me().banking = -1;
    closeMachine();
    me().gating = -1;
    setDown(hero, hero.recallColumn, hero.recallRow);
    for (Body& one : bodies_) {
        if (one.player || one.quarry != hero.id) continue;
        one.quarry = 0;
        one.provoked = false;
    }
    say(What::Recall, hero, int32_t(RecallEvent::Landed), hero.recallColumn, hero.recallRow, hero.recallBy);
}

// How long a push takes, in ticks: three tenths of a second -- quick enough to read as a blow,
// slow enough that the drawing slides it rather than jumping it. A body caught mid-step on a
// diagonal is off its tile's centre and can go up to 2.1 tiles to reach the next, so at five
// ticks the worst tick was 0.42 of a tile; at six it is 0.35.
constexpr int32_t kPushTicks = 6;

void Realm::push(Body& target, float fromX, float fromY, int tiles) {
    if (fixed(target) || shrugs(target)) return;  // nor the raid's dragon (Realm::shove)
    // Not again while it is still sliding: a push restarted mid-slide begins off the tile's
    // centre and can go two tiles in five ticks. The next strike finds it landed.
    if (target.pushTicks > 0) return;
    // Straight away from him, snapped to the nearest of the eight compass steps. No draw is taken,
    // so the seeded log's dice are the same with or without it.
    const float dx = target.x - fromX, dy = target.y - fromY;
    const float far = std::sqrt(dx * dx + dy * dy);
    if (far < 1e-3f) return;
    const int stepX = int(std::lround(dx / far)), stepY = int(std::lround(dy / far));
    if (stepX == 0 && stepY == 0) return;
    // Step by step: a wall or a safe tile stops it where it got to (Earthshake's second step as
    // OpenMU's EarthShakeSkillPlugIn stops; WebZen's BackSpringCheck turns it back toward him).
    int column = target.column(), row = target.row(), went = 0;
    for (; went < tiles; ++went) {
        const int nextColumn = column + stepX, nextRow = row + stepY;
        if (!tables_->grid.open(nextColumn, nextRow, content::kWallCharacter)) break;
        if (tables_->grid.safe(nextColumn, nextRow)) break;
        column = nextColumn;
        row = nextRow;
    }
    if (went == 0) return;
    // At the one tile's pace, a tile each kPushTicks.
    const int32_t ticks = kPushTicks * went;
    target.pushX = (float(column) - target.x) / float(ticks);
    target.pushY = (float(row) - target.y) / float(ticks);
    target.pushTicks = ticks;
    // The walk it was on is void, and it is not reaching anybody while it slides.
    target.walking = false;
    route(target).clear();
    target.onStep = 0;
    say(What::Shoved, target, column, row);
}


// A Whirlwind's slide, in ticks a tile: 0.15 s, so five tiles take three quarters of a second.
constexpr float kWhirlTicksPerTile = 3.0f;

float Realm::whirl(Body& hero, const SkillRow& row, float force) {
    // Eddies and Swirls ride in the same sums at their own numbers (sim/items.h kEddyDamage).
    force *= float(1.0 + hero.excel.whirlDamage);
    // One draw a cast, and only while one is carried, so nobody else's dice move.
    if (!dice_.nextBool(hero.excel.whirlPull)) return force;
    // Those beyond the slash's reach and within the pull's, seen from him and off sheltered
    // ground, gathered first: a body the strike below kills must not move the list under it.
    uint32_t pulled[kVictims];
    int count = 0;
    for (const Body& one : bodies_) {
        if (count >= kVictims) break;
        if (!one.monster() || !one.alive() || fixed(one) || one.pushTicks > 0) continue;
        if (within(hero, one, row.reach) || !within(hero, one, kWhirlwindReach)) continue;
        if (tables_->grid.safe(one.column(), one.row()) || !seen(hero, one)) continue;
        if (shrugs(one)) continue;  // nor the raid's dragon (sim::kImmuneSayTicks)
        pulled[count++] = one.id;
    }
    core::logf("whirlwind: tick %lld, %d to pull in", (long long)tick_, count);
    for (int i = 0; i < count; ++i) {
        Body* target = body(pulled[i]);
        if (target == nullptr || !target->alive()) continue;
        // Along the line to him a tile at a time, while the ground is open and not sheltered,
        // until it stands beside him (reach's square measure): a wall holds it where the wall is.
        const float dx = hero.x - target->x, dy = hero.y - target->y;
        const float far = std::sqrt(dx * dx + dy * dy);
        int column = target->column(), rowAt = target->row();
        const auto beside = [&](int c, int r) {
            return std::max(std::abs(c - hero.column()), std::abs(r - hero.row())) <= 1;
        };
        for (float along = 0.5f; along < far && !beside(column, rowAt); along += 0.5f) {
            const int c = int(std::lround(target->x + dx / far * along));
            const int r = int(std::lround(target->y + dy / far * along));
            if (c == column && r == rowAt) continue;
            if ((c == hero.column() && r == hero.row()) ||
                !tables_->grid.open(c, r, content::kWallCharacter) || tables_->grid.safe(c, r)) {
                break;
            }
            column = c;
            rowAt = r;
        }
        if (column == target->column() && rowAt == target->row()) continue;
        // Slid as the Lightning push slides, but kWhirlTicksPerTile a tile so the pull is seen
        // (the user, 2026-10-04: 'i did no see the pulling'), and struck by this cast when it
        // lands beside him (realm.cpp's slide), not before it moves -- one blow killed it where
        // it stood.
        const float lx = float(column) - target->x, ly = float(rowAt) - target->y;
        const int32_t ticks = std::max(
            kPushTicks, int32_t(std::ceil(std::max(std::fabs(lx), std::fabs(ly)) * kWhirlTicksPerTile)));
        target->pushX = lx / float(ticks);
        target->pushY = ly / float(ticks);
        target->pushTicks = ticks;
        target->walking = false;
        route(*target).clear();
        target->onStep = 0;
        target->whirledBy = hero.id;
        // With his element runes on it, which strikeAround lays only after this returns: the
        // pulled were struck softer than the slash's own (Piddy on Discord, 2026-10-25).
        target->whirlForce = force * elementForce(hero, skillElement(row.number));
        say(What::Shoved, *target, column, rowAt);
    }
    return force;
}

void Realm::chargeTick(Body& hero) {
    if (me().charge.skill == 0) return;
    const SkillRow* row = skillNumbered(me().charge.skill);
    if (row == nullptr || !hero.alive()) {
        // Dead, or the row gone: nothing goes off.
        me().charge = Charge{};
        hero.swingsAt = hero.castUntil = tick_;
        return;
    }
    // A stage every chargeTicks while there is mana for it; short, it holds where it is.
    if (tick_ >= me().charge.nextAt && me().charge.stage < row->chargeStages && hero.mana >= manaCostOf(hero, *row)) {
        hero.mana -= manaCostOf(hero, *row);
        ++me().charge.stage;
        me().charge.nextAt = tick_ + row->chargeTicks;
    }
    // Let go, or full: it bursts (OpenMU's loop ends on its twelfth stage and strikes).
    if (me().charge.letGo || me().charge.stage >= row->chargeStages) burstCharge(hero);
}

void Realm::burstCharge(Body& hero) {
    const SkillRow* row = skillNumbered(me().charge.skill);
    const int stage = me().charge.stage;
    me().charge = Charge{};
    if (row == nullptr) return;
    // Let go before a stage gathered, nothing goes.
    if (stage <= 0) {
        hero.swingsAt = hero.castUntil = tick_;
        return;
    }
    const int index = skillIndexOf(row->number);
    const int32_t clip = clipTicksOf(hero, *row);
    const int32_t cool = cooldownTicks(*row, hero.totalPoints().agility, floorTicksFor(*row, clip));
    if (index >= 0) hero.cools[size_t(index)] = tick_ + cool;
    hero.swingsAt = hero.castUntil = tick_ + clip;
    say(What::Loosed, hero, row->number, 0, stage, 0);
    // Its stages and the strength's half on the blow (OpenMU's SkillBaseDamageBonus), for this
    // burst alone.
    // The stages at his energy's share of kNovaFullEnergy, to the whole (skills.h).
    const HeroPoints points = hero.totalPoints();
    const int stageDamage = int(int64_t(kNovaStageDamage[std::clamp(stage, 0, 12)]) *
                                std::min(points.energy, kNovaFullEnergy) / kNovaFullEnergy);
    me().chargeDamage = stageDamage + points.strength / 2;
    strikeAround(hero, *row, row->force);
    me().chargeDamage = 0;
    core::logf("nova: tick %lld, burst at stage %d", (long long)tick_, stage);
}

}  // namespace mu::sim
