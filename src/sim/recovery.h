// What comes back on its own, and what the shield takes: the realm's recovery rates, moved out
// of sim/realm_tuning.h on 2026-09-28 so the HUD's cards for life, mana and the shield read the
// numbers the realm spends rather than a copy of them. Only constants; the realm does the work
// (`Realm::recover`, `Realm::strikeAt`).
#pragma once

#include <cstdint>

namespace mu::sim {

// ---- what comes back on its own -----------------------------------------------------------
//
// **The passive is 0.75's own and the attack's return is ours.** OpenMU regenerates on a timer:
// `GameConfiguration.RecoveryInterval = 3000` ms (GameConfigurationInitializerBase.cs:51) and
// `current += maximum * multiplier` (Player.RegenerateAsync), with the multiplier `1/27.5` for
// every class (CharacterClassInitialization.cs:163). That is 3.6% of the pool every three
// seconds, and it is what a character standing still gets back.
//
// It is also nowhere near enough to pay for a skill, which is the fault the player found: a
// level-30 knight holds 35 mana, Falling Slash costs 9, and the passive alone is one cast every
// twenty-seven seconds against a cooldown of under four. 0.75's answer was potions, because 0.75
// had no cooldowns and no reason to press anything but the attack.
//
// So the knight takes mana off what he hits. **invention**, and it is Diablo 3's shape, which is
// what PLAN.md chose for this bar: the basic attack is the generator and the skills are the
// spenders, so a fight has a rhythm of its own -- swing, swing, spend -- rather than a bar that
// empties in three presses and then plays like a game with no skills in it. A LANDED swing only,
// and never a skill's own blow: hitting is what pays, and a skill does not pay for the next one.
constexpr int32_t kRecoverEveryTicks = 60;       // 3000 ms at 20 Hz
constexpr float kManaRecoveryShare = 1.0f / 27.5f;
constexpr float kAttackManaShare = 0.05f;        // invention: a twentieth of the pool a blow
// **The one skill that pays**: Twisting Slash gives back this share of his pool for every body
// its whirl wounds (the user, 2026-10-02: "DK has to restore little bit mana every time twisting
// slash touch monster", "has to scale with something"). A share of the pool, so it grows with
// his energy and level as the swing's does; at 22 a cast it takes a crowd of eight to break
// even, so it slows the drain on a pack rather than filling him. invention.
constexpr float kTwistManaShare = 0.03f;
// Health comes back in a safe zone and nowhere else: OpenMU's HealthRecoveryMultiplier is the one
// relationship `0.01 x IsInSafezone` (AddCommonAttributeRelationships), with no base and no flat
// part, so outside town 0.75 is played off potions. MU2's Realm.HealthRecoveryInSafeZone.
constexpr float kHealthRecoveryInSafeZone = 0.01f;

// At rest -- sat on a bench, leant on a wall, hung from a rail -- three hundredths of each pool
// every five seconds, anywhere: WebZen's gObjRestPotionFill (user.cpp:23246-23380, 1.00.93),
// run on the second and paying on every fifth, while m_Rest holds the sit, pose or healing
// action. The user's pick of 2026-09-30; OpenMU has no rest.
constexpr int32_t kRestEveryTicks = 100;  // 5000 ms at 20 Hz
constexpr float kRestShare = 0.03f;
// And the kill's life comes two seconds after it (kKillLifeTicks, realm_fight.cpp).
constexpr int32_t kKillLifeTicks = 40;

// The shield: its share of a blow and its safe-zone recovery, every three seconds. Rates.cs.
constexpr float kShieldShare = 0.9f;
constexpr float kShieldRecovery = 0.02f;
constexpr int kRecoveryTicks = 60;

}  // namespace mu::sim
