// The Golden Dragon's raid as it is drawn (docs/game/golden-dragon-raid.md, sprint 2). The realm
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

#include "core/jobs.h"
#include "core/log.h"
#include "game/frustum.h"
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

// The other players' wings, as the raiders' above and the hero's in Play::update: worn while
// the wing has life left, beating fast only on a fly clip, the Wings of Darkness' second action
// on a safe tile. Not the hero (wing_) and not a raider (raiderWings_).
void Play::otherWings(float seconds) {
    if (!figures_) return;
    const std::vector<sim::Body>& bodies = realm_.bodies();
    if (otherWings_.size() < drawn_.size()) otherWings_.resize(drawn_.size());
    const uint32_t hero = realm_.hero().id;
    for (size_t i = 0; i < drawn_.size() && i < bodies.size(); ++i) {
        const sim::Body& body = bodies[i];
        WingLook& look = otherWings_[i];
        const sim::Satchel* bag = body.player && body.id != hero && body.raider < 0
                                      ? realm_.satchelOf(body.id) : nullptr;
        const sim::Held* wing = bag ? &(*bag)[sim::kWings] : nullptr;
        const content::ItemRow* row = wing && !wing->empty() && wing->durability > 0 &&
                                              size_t(wing->item) < tables_.items.size()
                                          ? &tables_.items[size_t(wing->item)]
                                          : nullptr;
        if (!row && !look.worn()) continue;
        Figure& bearer = drawn_[i].figure;
        look.wear(row ? wingBody(*figures_, row->group, row->number) : nullptr, bearer);
        const FigureBody* rig = bearer.body();
        const int beat = bearer.clip();
        const bool beating = rig && beat >= 0 && (beat == rig->flyClip || beat == rig->flyCrossbowClip);
        look.update(seconds, beating, tables_.grid.safe(body.column(), body.row()));
    }
}

int Play::petNumber(const sim::Held& worn) const {
    if (worn.empty() || worn.durability <= 0 || size_t(worn.item) >= tables_.items.size()) return -1;
    const content::ItemRow& row = tables_.items[size_t(worn.item)];
    return row.group == sim::kGroupPets ? row.number : -1;
}

bool Play::onDarkHorse(uint32_t id) const {
    const sim::Body* body = realm_.find(id);
    if (body == nullptr || !body->player || !body->riding) return false;
    const sim::Satchel* bag = id == realm_.hero().id ? &realm_.satchel() : realm_.satchelOf(id);
    return bag != nullptr && petNumber((*bag)[sim::kMount]) == sim::kDarkHorseNumber;
}

int Play::ravenLevel(const sim::Satchel& bag) const {
    const sim::Held& worn = bag[sim::kWeaponLeft];
    if (worn.empty() || size_t(worn.item) >= tables_.items.size()) return 0;
    return sim::darkRaven(tables_.items[size_t(worn.item)]) ? sim::petLevel(worn) : 0;
}

bool Play::bearsRaven(uint32_t id) const {
    const sim::Satchel* bag = id == realm_.hero().id ? &realm_.satchel() : realm_.satchelOf(id);
    return bag != nullptr && ravenLevel(*bag) > 0;
}

void Play::ravened(const sim::Happening& happening) {
    const Drawn* target = drawnOf(happening.whom);
    if (target == nullptr) return;
    Pets* pets = nullptr;
    if (happening.who == realm_.hero().id) {
        pets = &pets_;
    } else {
        for (size_t i = 0; i < otherPets_.size() && i < otherPetsOf_.size(); ++i) {
            if (otherPetsOf_[i] == happening.who) pets = &otherPets_[i];
        }
    }
    if (pets == nullptr) return;
    // At its feet: the dive and the ball each aim 50 over them, as MU's do (CSPetSystem.cpp:518,
    // ZzzEffectJoint.cpp:3854).
    const float at[3] = {target->figure.position()[0], target->figure.position()[1],
                         target->figure.position()[2]};
    pets->ravenStrike(happening.a == 1, at);
    // SetAttack's PlayBuffer, not placed: the dash's rush, the ball's missile (CSPetSystem.cpp:245-286).
    const int sound = happening.a == 1 ? heard_.ravenMissile : heard_.ravenRush;
    if (sound >= 0) sound_.play(sound);}

void Play::petLevelled(const sim::Happening& happening) {
    const bool up = happening.c == 1;
    float at[3];
    if (happening.a == sim::kWeaponLeft) {
        Pets* pets = happening.who == realm_.hero().id ? &pets_ : nullptr;
        for (size_t i = 0; !pets && i < otherPets_.size() && i < otherPetsOf_.size(); ++i) {
            if (otherPetsOf_[i] == happening.who) pets = &otherPets_[i];
        }
        if (pets && pets->ravenAt(at)) ravenShow_.level(at, up);
    } else if (const Drawn* rider = drawnOf(happening.who); rider && up) {
        // The horse's twelve flares about its hooves; nothing on a level down (GOBoid.cpp:469-491).
        for (int k = 0; k < 3; ++k) at[k] = rider->figure.position()[k];
        ravenShow_.horseLevel(at);
    }
    if (happening.who == realm_.hero().id) {
        Gain gain{Gain::Kind::Notice, 0};
        gain.line = std::string(happening.a == sim::kWeaponLeft ? "Your Dark Raven" : "Your Dark Horse") +
                    (up ? " reached level " : " fell to level ") + std::to_string(happening.b);
        gain.calm = up;
        gains_.push_back(gain);
    }
}

void Play::petLight(const Pets& pets, const Figure& owner) {
    float at[3];
    if (pets.angelAt(at)) petLights_.angel(at);
    if (pets.impLight(owner, at)) petLights_.imp(at);
}

void Play::ravenFrame(Pets& pets, float seconds) {
    // Unseen, it gives up what it was doing (Pets::ravenUnseen): TestFrustrum2D's test is of the
    // ground under it, 20 out, so high over him it is still seen.
    if (float bird[3]; ground_ && pets.ravenAt(bird)) {
        bird[1] = ground_->heightAt(bird[0], bird[2]);
        if (!Frustum(shot_).holds(bird, 0.2f)) pets.ravenUnseen();
    }
    // What its steps asked for (fx/raven.h): a spark off a bone 0..65 as it flies, the dash's
    // streaks and flash at the bird, the charge's light on every other bone from 0 or 1, as MU's
    // `i < 66` counts them (CSPetSystem.cpp:425-437, 729-745, 759-775).
    float from[3], to[3], yaw = 0.0f;
    const Pets::RavenCue* cues = nullptr;
    const int count = pets.ravenCues(&cues);
    const int bones = std::min(pets.ravenBones(), 66);
    for (int i = 0; i < count; ++i) {
        switch (cues[i]) {
            case Pets::RavenCue::Spark:
                if (bones > 0 && pets.ravenBone(pets.ravenRoll(bones), from)) ravenShow_.spark(from, pets.ravenYaw());
                break;
            case Pets::RavenCue::Streaks:
                if (pets.ravenAt(from)) ravenShow_.streaks(from);
                break;
            case Pets::RavenCue::Flash:
                if (pets.ravenAt(from)) ravenShow_.flash(from);
                break;
            case Pets::RavenCue::Puff: {
                const float head[3] = {0.5f, 0.0f, 0.0f};
                if (pets.ravenBoneAt(6, head, from)) ravenShow_.puff(from, pets.ravenYaw(), pets.ravenPitch());
                break;
            }
            case Pets::RavenCue::Glow:
                for (int bone = pets.ravenRoll(2); bone < bones; bone += 2) {
                    if (pets.ravenBone(bone, from)) ravenShow_.glow(from);
                }
                break;
        }
    }
    // The ball let go at its charge's end, and the bird's cry now and then (game/pets_raven.cpp).
    if (pets.ravenLetGo(from, to, yaw)) ravenShow_.ball(from, to, yaw);
    if (heard_.ravenShout >= 0 && pets.ravenCry(seconds) && pets.ravenAt(from)) emit(heard_.ravenShout, from[0], from[2]);
}

void Play::horseBreath(Pets& pets, float seconds) {
    float at[3];
    if (!pets.breathe(seconds, at)) return;
    if (underwater_) {
        if (breathBubbles_.size() < 64) breathBubbles_.push_back({at[0], at[1], at[2]});
        return;
    }
    snort_.puff(at);
}

int Play::horseAction(const Drawn& rider, int mount) const {
    // A weapon's ride swing is 3, the run ride 2, anything else 0 -- a spell's PLAYER_RIDE_SKILL
    // included. And the dragon's own 6 under PLAYER_SKILL_RIDER, a knight's skill on it -- Fire
    // Breath's above all (GOBoid.cpp:559-565). Uniria's horse has no 6 and keeps its 3.
    const FigureBody* look = rider.figure.body();
    const int clip = rider.figure.clip();
    // Over Tarkan and Icarus he sits still on the flying dragon (Play::follow), so its flying on is
    // his going, not his clip.
    const sim::Body* body = realm_.find(rider.id);
    const bool galloping = (look && clip >= 0 && (clip == look->rideRunClip || clip == look->rideRunArmedClip ||
                                                   clip == look->horseRunClip)) ||
                           (flying_ && mount == 3 && body && body->walking);
    const sim::SkillRow* swung = rider.swingSkill != 0 ? sim::skillNumbered(rider.swingSkill) : nullptr;
    const bool riderSkill = mount == 3 && rider.swinging > 0.0f && swung != nullptr && !swung->wizardry &&
                            !swung->onSelf() && swung->arrows == 0;
    // And the Dark Horse's stomp, its clip 3, while he plays Earthshake's 87 (GOBoid.cpp:338-342):
    // said here as 7, which Pets::ride turns into the horse's 3. What the swing was thrown with is
    // swingSkill: castSkill is spent the moment the clip starts (Play::update), so it read 0 on
    // every swing -- a spell's horse butted and the stomp never came.
    if (mount == 4 && rider.swinging > 0.0f && rider.swingSkill == sim::skill::kEarthshake) return 7;
    return riderSkill                                        ? 6
           : rider.swinging > 0.0f && rider.swingSkill == 0 ? 3
           : galloping                                      ? 2
                                                            : 0;
}

void Play::mountDust(const Drawn& rider, bool galloping, float& owed, float seconds) {
    // White in Devias, MU's BITMAP_SMOKE, and smoke02's brown everywhere else (fx/dust.h); none
    // over Icarus (WD_10HEAVEN), where only the Dark Horse's hooves strike up anything.
    if (!galloping || !ground_ || realm_.tables()->map == sim::kIcarusMap) {
        owed = 0.0f;
        return;
    }
    const auto dustDice = [this]() {
        dustSeed_ ^= dustSeed_ << 13;
        dustSeed_ ^= dustSeed_ >> 17;
        dustSeed_ ^= dustSeed_ << 5;
        return dustSeed_;
    };
    owed += seconds * 25.0f / 2.0f;
    while (owed >= 1.0f) {
        owed -= 1.0f;
        const float* feet = rider.figure.position();
        float at[3] = {feet[0] + float(int(dustDice() % 64) - 32) * 0.01f, feet[1],
                       feet[2] + float(int(dustDice() % 64) - 32) * 0.01f};
        at[1] = ground_->heightAt(at[0], at[2]);
        dust_.puff(at, rider.figure.yaw(), snowy_);
    }
}

void Play::hoofWaves(Pets& pets, float seconds) {
    // Over Icarus (WD_10HEAVEN) the Dark Horse strikes a ring off each hoof as it comes down, and
    // 5-9 BITMAP_SMOKE about it, 25 either way and 10 under (GOBoid.cpp:346-385).
    float at[3];
    if (realm_.tables()->map != sim::kIcarusMap || !pets.hoof(seconds, at)) return;
    const auto jitter = [this](int span) {
        dustSeed_ ^= dustSeed_ << 13;
        dustSeed_ ^= dustSeed_ >> 17;
        dustSeed_ ^= dustSeed_ << 5;
        return float(int(dustSeed_ % uint32_t(span)) - span / 2) * 0.01f;
    };
    const float ring[3] = {at[0] + jitter(16), at[1], at[2] + jitter(16)};
    earthshake_.hoof(ring);
    const int smoke = 5 + int(dustSeed_ % 5u);
    for (int i = 0; i < smoke; ++i) {
        const float puff[3] = {at[0] + jitter(50), at[1] + jitter(16) - 0.1f, at[2] + jitter(50)};
        dust_.puff(puff, 0.0f, true);
    }
}

void Play::otherHooves(float seconds) {
    // SetPlayerWalk plays the gallop for every Dark Lord on his horse off a safe tile, one roll a
    // frame each, on buffers of one channel the whole world shares (ZzzCharacter.cpp:764-767).
    const std::vector<sim::Body>& bodies = realm_.bodies();
    const uint32_t hero = realm_.hero().id;
    for (size_t i = 0; i < drawn_.size() && i < bodies.size() && i < otherPets_.size(); ++i) {
        const sim::Body& body = bodies[i];
        Pets& pets = otherPets_[i];
        const Drawn& rider = drawn_[i];
        const FigureBody* look = rider.figure.body();
        const bool galloping = body.player && body.id != hero && otherPetsOf_[i] == body.id && body.alive() &&
                               rider.visible && look && rider.figure.clip() == look->horseRunClip &&
                               !tables_.grid.safe(body.column(), body.row());
        if (!galloping || heard_.hoof < 0) {
            pets.hoofOwed = 0.0f;
            continue;
        }
        pets.hoofOwed = std::min(pets.hoofOwed + seconds * 25.0f, 4.0f);
        while (pets.hoofOwed >= 1.0f) {
            pets.hoofOwed -= 1.0f;
            hoofSeed_ ^= hoofSeed_ << 13;
            hoofSeed_ ^= hoofSeed_ >> 17;
            hoofSeed_ ^= hoofSeed_ << 5;
            float& busy = hoofBusy_[hoofSeed_ % 3u];
            if (busy > 0.0f) continue;
            busy = std::max(sound_.seconds(heard_.hoof), 0.1f);
            emit(heard_.hoof, rider.crown[0], rider.crown[2], rider.id);
        }
    }
}

// The other players' pets and mounts, as the hero's in Play::update and their wings above: each
// off his own bag, the horse under him while he rides. Not the hero (pets_) and not a raider.
void Play::otherPets(float seconds) {
    if (!figures_) return;
    const std::vector<sim::Body>& bodies = realm_.bodies();
    if (otherPets_.size() < drawn_.size()) {
        otherPets_.resize(drawn_.size());
        otherPetsOf_.resize(drawn_.size(), 0);
    }
    const uint32_t hero = realm_.hero().id;
    for (size_t i = 0; i < drawn_.size() && i < bodies.size(); ++i) {
        const sim::Body& body = bodies[i];
        const sim::Satchel* bag = body.player && body.id != hero && body.raider < 0 && !body.gone
                                      ? realm_.satchelOf(body.id) : nullptr;
        const int pet = bag ? petNumber((*bag)[sim::kPet]) : -1;
        const int mount = bag ? petNumber((*bag)[sim::kMount]) : -1;
        const int raven = bag ? ravenLevel(*bag) : 0;
        if (otherPetsOf_[i] != body.id) {
            if (otherPetsOf_[i] == 0 && pet < 0 && mount < 0 && raven == 0) continue;  // nothing to start
            otherPets_[i] = Pets{};
            otherPets_[i].open(*figures_);
            otherPets_[i].setFlying(flying_);
            otherPetsOf_[i] = body.id;
        }
        Drawn& rider = drawn_[i];
        otherPets_[i].update(seconds, rider.figure, pet, body.alive());
        petLight(otherPets_[i], rider.figure);
        otherPets_[i].raven(seconds, rider.figure, raven, tables_.grid.safe(body.column(), body.row()),
                            mount == sim::kDarkHorseNumber);
        ravenFrame(otherPets_[i], seconds);
        const int action = horseAction(rider, mount);
        otherPets_[i].ride(seconds, rider.figure, mount, body.riding, action);  // and as he dies (Play::update)
        horseBreath(otherPets_[i], seconds);
        hoofWaves(otherPets_[i], seconds);
        mountDust(rider, action == 2 && body.riding && rider.visible, otherPets_[i].dustOwed, seconds);
    }
}

void Play::gatherOtherWings(gfx::Renderer& renderer, std::vector<gfx::Drawable>& out,
                            std::vector<gfx::Drawable>* casters) {
    // Posed on all the cores, as the bodies are (Play::gather), and drawn in order as they come.
    constexpr size_t kRowsEach = size_t(gfx::Renderer::kMaxBones) * 12;
    posing_.clear();
    for (size_t i = 0; i < otherWings_.size() && i < drawn_.size(); ++i) {
        if (otherWings_[i].worn() && drawn_[i].visible) posing_.push_back(uint32_t(i));
    }
    if (poseRows_.size() < posing_.size() * kRowsEach) poseRows_.resize(posing_.size() * kRowsEach);
    poseBones_.resize(posing_.size());
    core::parallelForInOrder(
        posing_.size(),
        [&](size_t k) {
            const size_t i = posing_[k];
            poseBones_[k] = otherWings_[i].pose(drawn_[i].figure, &poseRows_[k * kRowsEach]);
        },
        [&](size_t k) {
            if (poseBones_[k] < 0) return;
            otherWings_[posing_[k]].gatherPosed(renderer, &poseRows_[k * kRowsEach],
                                                poseBones_[k], out, casters);
        });
}

}  // namespace mu::game
