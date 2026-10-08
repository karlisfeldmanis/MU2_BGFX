// The Golden Dragon's raid as it is drawn (docs/golden-dragon-raid.md, sprint 2). The realm
// decides it all (sim/realm_raid.cpp) and says it as What::Raid; this shows it:
//
//   * every told Hazard marked on the ground for its tell (fx/omen.h): a disc where fire falls
//     or the roar shocks, the Breath's cone from the mouth, the Inferno's field, and its wings'
//     shadows darkened;
//   * the blows themselves with the effects the game has: a meteor from the sky for each strafe
//     fire and storm rock (fx/meteor.h), MU's fire breathed from bone 11 along the cone for as
//     long as it burns (fx/breath.h), a low flame where a pool burns (fx/flame.h), Hellfire's
//     ring and wall for the Inferno and the roar (fx/hellfire.h);
//   * the dragon drawn up off the ground while aloft, on its flight clip 7 (MONSTER01_DIE + 1,
//     which only MU's event dragons play, GOBoid.cpp:1398), and down again;
//   * the raiders: their class bodies in their kits (Figures::dress) and their wings;
//   * the camera's pull near it (§2c), and whether he is in its fight, for the music.
#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/play.h"
#include "game/play_tuning.h"
#include "game/roster.h"
#include "game/shine.h"
#include "sim/realm_tuning.h"

namespace mu::game {
namespace {

// How high it flies while aloft, in metres, and how fast it climbs or comes down. Ours: high
// enough to read as up and out of a sword's reach, low enough to stay in MU's frame -- at 6 m
// it rose to the camera's own height and left the picture.
constexpr float kAloft = 30.0f;  // leaving: up and out of the frame
constexpr float kClimb = 5.0f;   // metres a second
// And away along its facing, gathering pace: metres a second, a second. Ours.
constexpr float kDepartPace = 6.0f;
// The camera's pull near a boss (§2c): MU's TW_CAMERA_UP eases 10 units a frame at 25 frames,
// 2.5 m a second (DefaultCamera.cpp:703-724); ours reaches 3 m, within 14 tiles of a roused
// boss and held to 18.
// Eased by a critically damped spring rather than at MU's flat pace, which started and stopped
// dead (the user, 2026-10-08: "camera zoom in / out too fast has to be more smooth and littel bit
// slower"): kPullSettle is its time constant, about three seconds to settle where the flat 2.5 m
// a second took 1.2.
constexpr float kPullMost = 3.0f;
constexpr float kPullSettle = 0.75f;
constexpr float kPullNear = 14.0f;
constexpr float kPullHold = 18.0f;
// The breath's sparks, a frame of MU's 25 each, three abreast for its three jets.
constexpr float kBreathSparkEvery = 1.0f / 25.0f;
// A pool's fire: a low ember now and then at a random point in it -- MU's foot fire, small and
// short -- and never Flame's pillar, which stood a column of fire on every pool (the user,
// 2026-10-06: 'that flame ground effects was not lookiing to good very bright nit polished').
constexpr float kPoolEmberEvery = 0.35f;
// The storm's rocks and the strafe's fires at half MU's rock: ten of them at once at full size
// were a wall of blasts. And the roar's and the Inferno's bursts at under half. Ours.
constexpr float kRockWeight = 0.5f;
constexpr float kShockBlast = 0.3f;
constexpr float kInfernoBlast = 0.45f;
// The mouth: 50 units out along bone 11's -y, as the sky's diver breathes (invasion_sky.cpp).
constexpr float kMouth[3] = {0.0f, -0.5f, 0.0f};
// **Its fire at rest** (the user, 2026-10-08: "lets add some fire effects also to dragon, but
// nothing crazy subtle but cool"): while it fights, an ember falls from its jaws every
// kJawEmberEvery, drifting out along its facing at kJawEmberScale of the breath's spark; and
// enraged, from the third stage, a low flame licks at a foot every kFootEmberEvery. Ours.
constexpr float kJawEmberEvery = 0.35f;
constexpr float kJawEmberScale = 0.6f;
constexpr float kFootEmberEvery = 0.5f;
constexpr float kFootEmberReach = 1.6f;  // metres round its feet

float ticksToSeconds(int64_t ticks) { return float(ticks) * float(kTickSeconds); }

}  // namespace

const FigureBody* Play::raiderLook(const sim::Body& body) {
    const sim::Satchel* bag = realm_.raiderBag(body.raider);
    if (!figures_ || bag == nullptr) return nullptr;
    // The two hands and the five armour pieces, as Play::redress reads the hero's satchel.
    std::string weapon, shield;
    ShineLook weaponShine, shieldShine;
    if (body.weapon >= 0 && size_t(body.weapon) < tables_.arms.size()) {
        weapon = tables_.arms[size_t(body.weapon)].name;
    }
    if (body.shield >= 0 && size_t(body.shield) < tables_.arms.size()) {
        shield = tables_.arms[size_t(body.shield)].name;
    }
    std::vector<std::string> worn;
    std::vector<ShineLook> wornShine;
    for (int slot = sim::kWeaponRight; slot <= sim::kBoots; ++slot) {
        const sim::Held& held = (*bag)[slot];
        if (held.empty() || size_t(held.item) >= tables_.items.size()) continue;
        const content::ItemRow& row = tables_.items[size_t(held.item)];
        const ShineLook shine = shineOf(row, held.refinement, held.excellent != 0);
        if (slot >= sim::kHelm) {
            worn.push_back(row.name);
            wornShine.push_back(shine);
        } else if (row.name == weapon) {
            weaponShine = shine;
        } else if (row.name == shield) {
            shieldShine = shine;
        }
    }
    return figures_->dress("Raider" + std::to_string(body.raider), bareBody(body.kin, body.second, figures_),
                           weapon, shield, worn, wornShine, weaponShine, shieldShine);
}

void Play::raidSaid(const sim::Happening& happening) {
    const sim::Body* dragon = realm_.invader();
    if (happening.what != sim::What::Raid || dragon == nullptr || happening.who != dragon->id ||
        ground_ == nullptr) {
        return;
    }
    const float metresPerTile = ground_->metresPerTile();
    const float x = (happening.x + 0.5f) * metresPerTile;
    const float z = -(happening.y + 0.5f) * metresPerTile;
    const auto event = sim::RaidEvent(happening.a);
    if (event == sim::RaidEvent::Stage) {
        core::logf("raid: stage %d drawn at tick %u", happening.b, happening.tick);
        // Its roar at every stage, the landed dragon's own (Play::roar).
        roarOwed_ = dragon->id;
        return;
    }
    if (event == sim::RaidEvent::Immune) {
        // A hold it shrugged off: the word over it, as a miss is (the user, 2026-10-06: 'there
        // has to be damage text immune').
        // Where its numbers stand (Play::update's landing): over its crown, from the ground.
        const Drawn* drawn = drawnOf(dragon->id);
        const float cx = drawn && drawn->placed ? drawn->crown[0] : x;
        const float cz = drawn && drawn->placed ? drawn->crown[2] : z;
        const float feet[3] = {cx, ground_->heightAt(cx, cz), cz};
        showing_.word(Mark::Immune, feet);
        return;
    }
    if (event == sim::RaidEvent::Wave) {
        // It summons its minions with its roar, the landing's own, and does not swing through it
        // (the realm holds it, sim::kSummonTicks). Not the clip whole: its opening reads as a
        // wing-beat (the user, 2026-10-06: 'for some reason 1 time dragon used fly animation').
        roarOwed_ = dragon->id;
        return;
    }
    if (event == sim::RaidEvent::Shadow) {
        // Not drawn: no warning of the dragon's moves at all (the user, 2026-10-06: 'dont show
        // spell warning from dragon spell just happend and players will learn that').
        return;
        // A wing's shelter, dark until the Inferno's fire has passed.
        float tell = 0.0f;
        for (int k = 0; k < sim::kHazards; ++k) {
            const sim::Hazard& h = realm_.hazards()[k];
            if (h.kind == sim::HazardKind::Inferno) tell = ticksToSeconds(h.landsAt - realm_.tick());
        }
        omen_.tell(Omen::Shape::Shadow, x, z, (sim::kShadowReach + 0.5f) * metresPerTile, tell, 0.0f);
        return;
    }
    if (event == sim::RaidEvent::Tell) {
        const auto kind = sim::HazardKind(happening.b);
        const float tell = ticksToSeconds(happening.c);
        switch (kind) {
            case sim::HazardKind::Breath: {
                // Its facing as the realm turned it, as the drawing yaws a body (Play::gather).
                const float yaw = std::atan2(std::cos(dragon->facing), -std::sin(dragon->facing));
                breathOn_.wait = tell;
                breathOn_.left = ticksToSeconds(sim::kBreathTicks);
                breathOn_.yaw = yaw;
                break;
            }
            case sim::HazardKind::Shock:
                break;
            case sim::HazardKind::Hellfire:
                break;
            case sim::HazardKind::Impact:
                // No mark for a rock: it just falls (the user, 2026-10-06: 'dont show the meteor
                // warning they just has to happen'). Let go from the sky so it lands on the tick
                // the realm strikes.
                rocksOwed_.push_back({std::max(0.0f, tell - Meteor::fallSeconds()), x, z});
                break;
            case sim::HazardKind::Inferno:
                // No field laid on the ground (the user: 'dont dimm the ground'): its shadows are
                // the mark, and the bar names it.
                break;
            default:
                break;
        }
        return;
    }
    if (event == sim::RaidEvent::Strike) {
        const auto kind = sim::HazardKind(happening.b);
        const float at[3] = {x, ground_->heightAt(x, z), z};
        if (kind == sim::HazardKind::Shock) {
            meteor_.blast(at, kShockBlast);
            meteor_.stones(x, z, at[1], 2);
        } else if (kind == sim::HazardKind::Hellfire) {
            // MU's Hellfire round it: the sigil and the wall of fire, and its sound.
            const float yaw = std::atan2(std::cos(dragon->facing), -std::sin(dragon->facing));
            hellfire_.cast(at, yaw);
            if (heard_.hellfire >= 0) emit(heard_.hellfire, x, z);
        } else if (kind == sim::HazardKind::Inferno) {
            // The wizard's Inferno ring of bombs round it (fx/inferno.h), and a burst.
            const float yaw = std::atan2(std::cos(dragon->facing), -std::sin(dragon->facing));
            inferno_.cast(at, yaw, [&](const float* stone) { meteor_.stones(stone[0], stone[2], stone[1], 2); });
            meteor_.blast(at, kInfernoBlast);
            if (heard_.explosion >= 0) emit(heard_.explosion, x, z);
        } else if (kind == sim::HazardKind::Impact) {
            // A storm rock's pool: the realm lays it now, a burning tile for its twelve seconds.
            for (int k = 0; k < sim::kHazards; ++k) {
                const sim::Hazard& h = realm_.hazards()[k];
                if (h.kind != sim::HazardKind::Pool) continue;
                if (std::fabs(h.x - happening.x) > 0.01f || std::fabs(h.y - happening.y) > 0.01f) continue;
                // Its embers alone: no char laid on the ground.
                const float burns = ticksToSeconds(h.endsAt - realm_.tick());
                poolsOn_.push_back({x, z, burns, 0.0f});
            }
        }
    }
}

void Play::raidCircle(const sim::Body& body, float* x, float* z, float* yaw) const {
    const sim::Body* dragon = realm_.invader();
    if (dragon == nullptr || &body != dragon || raidLift_ <= 0.0f) return;
    // Leaving: away along the way it faced as it rose, faster as it climbs (raidOrbit_ is the
    // seconds it has been going). It flies only to come and to go (the user, 2026-10-06).
    const float out = kDepartPace * raidOrbit_ * raidOrbit_ * 0.5f;
    *x += std::sin(departYaw_) * out;
    *z += std::cos(departYaw_) * out;
    *yaw = departYaw_;
}

float Play::raidLift(const sim::Body& body) const {
    const sim::Body* dragon = realm_.invader();
    if (dragon != nullptr && &body == dragon) return raidLift_;
    return 0.0f;
}

bool Play::raidFlies(Drawn& one, const sim::Body& body) {
    const sim::Body* dragon = realm_.invader();
    if (dragon == nullptr || &body != dragon || raidLift_ <= 0.05f || dragonFlyClip_ < 0) return false;
    // Aloft, or coming down: its flight clip at MU's 0.5 (the sky's), whatever the realm says it
    // is doing, until its feet are on the ground again.
    one.swinging = 0.0f;
    one.casting = 0.0f;
    one.shocked = 0.0f;
    one.figure.play(dragonFlyClip_, false, 0.3f);
    one.clipRate = 1.0f;
    return true;
}

bool Play::raidFighting() const {
    const sim::Body* dragon = realm_.invader();
    if (dragon == nullptr || !dragon->alive() || realm_.raidStage() == sim::RaidStage::None) return false;
    const sim::Body& hero = realm_.hero();
    return hero.alive() && sim::within(hero, *dragon, kPullHold);
}

void Play::raid(float seconds) {
    omen_.update(seconds);
    const sim::Body* dragon = realm_.invader();
    if (dragon == nullptr || ground_ == nullptr) return;
    // --raid-stage: laid once it stands.
    if (raidSkipOwed_ > 0 && realm_.raidStage() != sim::RaidStage::None) {
        if (here("--raid-stage")) local_.raidSkipTo(sim::RaidStage(raidSkipOwed_));
        core::logf("raid: --raid-stage %d", raidSkipOwed_);
        raidSkipOwed_ = 0;
    }
    Drawn* drawn = drawnOf(dragon->id);
    // Its clip and its mouth, found once its body is drawn.
    if (drawn && drawn->figure.body() && (dragonFlyClip_ < 0 || dragonMouth_ < 0)) {
        const FigureBody* look = drawn->figure.body();
        if (look->library) dragonFlyClip_ = look->library->find(7);
        if (look->skeletonMesh) {
            const auto& bones = look->skeletonMesh->bones();
            for (size_t i = 0; i < bones.size(); ++i) {
                if (bones[i].name == "attack01") dragonMouth_ = int(i);
            }
        }
    }
    // Up and down at its own pace.
    const float want = realm_.raidAloft() && dragon->alive() ? kAloft : 0.0f;
    raidLift_ = want > raidLift_ ? std::min(want, raidLift_ + kClimb * seconds)
                                 : std::max(want, raidLift_ - kClimb * seconds);
    // Its facing as it rose, kept for the whole of its going.
    if (raidLift_ > 0.0f) {
        if (raidOrbit_ == 0.0f && drawn) departYaw_ = drawn->yaw;
        raidOrbit_ += seconds;
    } else {
        raidOrbit_ = 0.0f;
    }
    // The rocks owed to the sky.
    for (RockOwed& rock : rocksOwed_) {
        rock.wait -= seconds;
        if (rock.wait <= 0.0f) {
            meteor_.cast(rock.x, rock.z, 0, kRockWeight);
            if (heard_.meteorite >= 0) emit(heard_.meteorite, rock.x, rock.z);
        }
    }
    rocksOwed_.erase(std::remove_if(rocksOwed_.begin(), rocksOwed_.end(),
                                    [](const RockOwed& r) { return r.wait <= 0.0f; }),
                     rocksOwed_.end());
    // The breath, from the mouth along the cone: MU's three jets at -30, 0 and +30 degrees
    // (ZzzCharacter.cpp:1939-1948), a spark each a frame.
    if (breathOn_.left > 0.0f) {
        if (breathOn_.wait > 0.0f) {
            breathOn_.wait -= seconds;
        } else {
            breathOn_.left -= seconds;
            breathOn_.spark -= seconds;
            float at[3];
            if (drawn && dragonMouth_ >= 0 && drawn->figure.pointOn(dragonMouth_, kMouth, at)) {
                if (breathOn_.spark <= 0.0f && heard_.meteorite >= 0) emit(heard_.meteorite, at[0], at[2]);
                while (breathOn_.spark <= 0.0f) {
                    breathOn_.spark += kBreathSparkEvery;
                    for (const float turn : {-sim::kBreathHalfAngle, 0.0f, sim::kBreathHalfAngle}) {
                        const float along[2] = {std::sin(breathOn_.yaw + turn), std::cos(breathOn_.yaw + turn)};
                        breath_.spark(at, along, 1.4f);
                    }
                }
            }
        }
    }
    // Its fire at rest: embers at the jaws, and enraged, flames at its feet.
    if (drawn && drawn->placed && dragon->alive() && realm_.raidStage() != sim::RaidStage::None) {
        const auto roll = [&]() {
            emberDice_ ^= emberDice_ << 13;
            emberDice_ ^= emberDice_ >> 17;
            emberDice_ ^= emberDice_ << 5;
            return float(emberDice_ % 10000) / 10000.0f;
        };
        const float yaw = std::atan2(std::cos(dragon->facing), -std::sin(dragon->facing));
        jawEmber_ -= seconds;
        float at[3];
        if (jawEmber_ <= 0.0f && dragonMouth_ >= 0 && drawn->figure.pointOn(dragonMouth_, kMouth, at)) {
            jawEmber_ = kJawEmberEvery * (0.7f + 0.6f * roll());
            const float turn = (roll() - 0.5f) * 0.8f;
            const float along[2] = {std::sin(yaw + turn), std::cos(yaw + turn)};
            breath_.spark(at, along, kJawEmberScale);
        }
        if (realm_.raidStage() >= sim::RaidStage::Enraged) {
            footEmber_ -= seconds;
            if (footEmber_ <= 0.0f && ground_) {
                footEmber_ = kFootEmberEvery * (0.6f + 0.8f * roll());
                const float a = roll() * 6.2832f, r = kFootEmberReach * (0.4f + 0.6f * roll());
                const float x = drawn->crown[0] + std::cos(a) * r, z = drawn->crown[2] + std::sin(a) * r;
                const float feet[3] = {x, ground_->heightAt(x, z), z};
                const float along[2] = {std::cos(a), std::sin(a)};
                breath_.footFire(feet, along);
            }
        }
    }
    // The pools' low fire.
    for (PoolOn& pool : poolsOn_) {
        pool.left -= seconds;
        pool.relight -= seconds;
        if (pool.relight <= 0.0f && pool.left > 0.0f) {
            pool.relight = kPoolEmberEvery;
            // A point in the pool off a cheap hash of its clock, so two pools do not flicker
            // in step.
            const float seed = pool.left * 12.9898f + pool.x * 78.233f;
            const float r = std::fabs(std::sin(seed)) * 0.9f, a = std::fabs(std::cos(seed * 1.7f)) * 6.2832f;
            const float x = pool.x + std::cos(a) * r, z = pool.z + std::sin(a) * r;
            const float at[3] = {x, ground_->heightAt(x, z), z};
            const float along[2] = {std::sin(a), std::cos(a)};
            breath_.footFire(at, along);
        }
    }
    poolsOn_.erase(std::remove_if(poolsOn_.begin(), poolsOn_.end(),
                                  [](const PoolOn& p) { return p.left <= 0.0f; }),
                   poolsOn_.end());
    // Its fight ended: whatever it laid goes with it.
    if (!dragon->alive()) {
        rocksOwed_.clear();
        poolsOn_.clear();
        breathOn_ = BreathOn{};
    }
    // The raiders' wings: worn while they have a wing on, beating slow as the hero's at rest.
    if (raiderWings_.size() != size_t(realm_.raiderCount())) raiderWings_.assign(size_t(realm_.raiderCount()), WingLook{});
    for (int i = 0; i < realm_.raiderCount(); ++i) {
        const sim::Body* raider = realm_.raiderAt(i);
        const sim::Satchel* bag = realm_.raiderBag(i);
        Drawn* figure = raider ? drawnOf(raider->id) : nullptr;
        if (!figure || !bag || !figures_) continue;
        const sim::Held& wing = (*bag)[sim::kWings];
        const content::ItemRow* row =
            !wing.empty() && size_t(wing.item) < tables_.items.size() ? &tables_.items[size_t(wing.item)] : nullptr;
        raiderWings_[size_t(i)].wear(row ? wingBody(*figures_, row->group, row->number) : nullptr, figure->figure);
        raiderWings_[size_t(i)].update(seconds, false);
    }
    // The camera's pull, near a roused boss (§2c).
    const sim::Body& hero = realm_.hero();
    const bool near = dragon->alive() && realm_.raidStage() != sim::RaidStage::None && hero.alive() &&
                      sim::within(hero, *dragon, cameraPull_ > 0.0f ? kPullHold : kPullNear);
    const float pull = near ? kPullMost : 0.0f;
    const float omega = 1.0f / kPullSettle;
    const float step = std::min(seconds, 0.1f);  // a long frame does not fling it
    const float accel = omega * omega * (pull - cameraPull_) - 2.0f * omega * cameraPullSpeed_;
    cameraPullSpeed_ += accel * step;
    cameraPull_ = std::clamp(cameraPull_ + cameraPullSpeed_ * step, 0.0f, kPullMost);
}

void Play::gatherRaiders(gfx::Renderer& renderer, std::vector<gfx::Drawable>& out,
                         std::vector<gfx::Drawable>* casters) {
    for (int i = 0; i < realm_.raiderCount() && size_t(i) < raiderWings_.size(); ++i) {
        const sim::Body* raider = realm_.raiderAt(i);
        Drawn* figure = raider ? drawnOf(raider->id) : nullptr;
        if (!figure || !figure->visible || !raiderWings_[size_t(i)].worn()) continue;
        raiderWings_[size_t(i)].gather(renderer, figure->figure, scratch_, out, casters);
    }
}

}  // namespace mu::game
