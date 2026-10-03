// The pointer, and what a click does with it.
//
// The ray is cast against the land's own height field rather than against a flat plane: the
// town is up to two metres of relief and a flat-plane pick is a tile or two out wherever the
// ground is not level. A click becomes a REQUEST and never a decision -- see Realm::accept,
// which takes it at the top of the next tick.
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

void Play::point(const gfx::Camera& camera, const float* view, const float* proj, float pixelX,
                 float pixelY, int width, int height) {
    pointedColumn_ = pointedRow_ = -1;
    pointedAt_ = 0;
    pointedFolk_ = -1;
    pointedLying_ = 0;
    pointedPerch_ = -1;
    // The shot this frame is drawn with, kept for what may be heard: see emit().
    bx::mtxMul(shot_, view, proj);
    shotKnown_ = true;
    if (!isOpen() || !ground_ || width <= 0 || height <= 0) return;

    // The pixel into a direction. bgfx's clip space on this Metal is 0..1 in z and the origin
    // is top left -- both asked for rather than assumed, as docs/conventions.md insists -- so
    // only y is flipped here and the near plane is z = 0.
    const float ndcX = (2.0f * pixelX / float(width)) - 1.0f;
    const float ndcY = 1.0f - (2.0f * pixelY / float(height));
    float viewProj[16];
    bx::mtxMul(viewProj, view, proj);
    float inverse[16];
    bx::mtxInverse(inverse, viewProj);
    const float nearZ = bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f;
    const bx::Vec3 nearPoint = bx::mulH({ndcX, ndcY, nearZ}, inverse);
    const bx::Vec3 farPoint = bx::mulH({ndcX, ndcY, 1.0f}, inverse);
    bx::Vec3 direction = bx::normalize(bx::sub(farPoint, nearPoint));
    if (std::fabs(direction.y) < 1e-5f) return;

    // Down the ray until it is under the land, then a bisection between the last step above and
    // the first below. A flat plane at the camera's own height is the cheap way and it is a tile
    // or two out wherever the town is not level, which is most of Lorencia.
    const float metresPerTile = ground_->metresPerTile();
    const auto below = [&](const bx::Vec3& at) {
        return at.y < ground_->heightAt(at.x, at.z);
    };
    bx::Vec3 above = nearPoint;
    if (below(above)) return;  // the camera is inside the ground: nothing to pick
    float far = 0.0f;
    bool found = false;
    for (float travelled = 0.5f; travelled < 200.0f; travelled += 0.5f) {
        const bx::Vec3 at = bx::add(nearPoint, bx::mul(direction, travelled));
        if (below(at)) {
            far = travelled;
            found = true;
            break;
        }
        above = at;
    }
    if (!found) return;
    float low = far - 0.5f, high = far;
    for (int i = 0; i < 12; ++i) {
        const float middle = (low + high) * 0.5f;
        if (below(bx::add(nearPoint, bx::mul(direction, middle)))) {
            high = middle;
        } else {
            low = middle;
        }
    }
    const bx::Vec3 hit = bx::add(nearPoint, bx::mul(direction, (low + high) * 0.5f));

    // Metres back into tiles: the centre of a tile is its integer coordinate to the sim and
    // (column + 0.5, -(row + 0.5)) in metres to everything else.
    const float column = hit.x / metresPerTile - 0.5f;
    const float row = -hit.z / metresPerTile - 0.5f;
    pointedColumn_ = int(std::lround(column));
    pointedRow_ = int(std::lround(row));
    if (!tables_.grid.inside(pointedColumn_, pointedRow_)) {
        pointedColumn_ = pointedRow_ = -1;
        return;
    }

    // And who is standing there. The nearest body within a tile of the point, which is a scan
    // over a few hundred and is the same scan the sim does: a body is picked by where it IS and
    // not by its drawn mesh, so aiming at a monster's feet and aiming at its head pick the same
    // monster, and what is clicked is what the sim will be asked to fight.
    //
    // And by the ray as well as by where it lands. The ground point alone only ever found the
    // feet: at this camera's 48.5 degrees a pointer on a head meets the ground 1.6 m behind
    // it. So each one is also an upright column its own drawn height and footprint, and the
    // ray's nearest pass to its axis between the ground and its top is what counts. The
    // landing point keeps a little slack of its own for a click at the feet.
    //
    // Sized by the figure and not by a man: a fixed 1.9 m column with 0.6 m round it, beside
    // the old 1.2 tiles round the landing point, rang a spider from empty ground a metre
    // behind it -- hovered with the pointer nowhere on it.
    constexpr float kFeetSlack = 0.6f;  // tiles round the landing point that are "at his feet"
    const auto reach = [&](float bodyColumn, float bodyRow, const Figure& figure) {
        const FigureBody* look = figure.body();
        const float scale = look ? look->scale : 1.0f;
        const float stature = look ? look->height * scale : 1.8f;
        const float across = look ? std::max(look->max[0] - look->min[0],
                                             look->max[2] - look->min[2]) * scale
                                  : 0.8f;
        // Most of the half-width: a bind box is the arms out and the tail straight, wider
        // than the body the eye picks.
        const float round = std::clamp(across * 0.35f, 0.3f, 1.2f);
        const float byGround =
            std::max(std::fabs(bodyColumn - column), std::fabs(bodyRow - row)) / kFeetSlack;
        const float x = (bodyColumn + 0.5f) * metresPerTile;
        const float z = -(bodyRow + 0.5f) * metresPerTile;
        const float floorY = ground_->heightAt(x, z);
        // Where the ray is at the column's top and at its foot, then the nearest point of that
        // stretch to the axis, flat -- the column is upright, so height does not count.
        const float t0 = (floorY + stature - nearPoint.y) / direction.y;
        const float t1 = (floorY - nearPoint.y) / direction.y;
        const float ax = nearPoint.x + direction.x * t0, az = nearPoint.z + direction.z * t0;
        const float dx = direction.x * (t1 - t0), dz = direction.z * (t1 - t0);
        const float along = dx * dx + dz * dz;
        const float u = along > 1e-8f
                            ? std::clamp(((x - ax) * dx + (z - az) * dz) / along, 0.0f, 1.0f)
                            : 0.0f;
        const float byRay = std::hypot(ax + dx * u - x, az + dz * u - z) / round;
        // Under one is on it; the smaller wins between two.
        return std::min(byGround, byRay);
    };
    float closest = 1.0f;
    for (const sim::Body& body : realm_.bodies()) {
        // Nor her own summon: it is never fought, so it gets no attack pointer and no hover
        // bar, and the pointer passes through it to the monster behind -- where it stood
        // in front of her quarry, a click took the summon and she attacked nothing.
        if (body.player || body.summoner != 0) continue;
        const Drawn* drawn = drawnOf(body.id);
        if (drawn == nullptr || !drawn->visible) continue;
        // Standing on screen, not alive in the realm: a monster killed on this tick is still
        // up until the blow that killed it lands, half a swing on, and the pointer resting on
        // it is still on it. Picked by the realm's word, the hover dropped on the tick and its
        // bar lingered out still showing the health before the blow -- then it fell.
        if (!body.alive() && !drawn->fallOwed) continue;
        const float away = reach(body.x, body.y, drawn->figure);
        if (away < closest) {
            closest = away;
            // A guard is pointed at as the townsperson he is: his name, and no bar or attack.
            pointedAt_ = body.warden >= 0 ? 0 : body.id;
            pointedFolk_ = body.warden;
        }
    }
    // What lies on the ground, only where nothing living is closer: a click on a drop next
    // to a monster is a click on the monster, as MU's own picking orders it.
    if (pointedAt_ == 0 && pointedFolk_ < 0) {
        float nearest = 0.8f;
        for (const sim::Lying& one : realm_.lying()) {
            if (std::find(heldIds_.begin(), heldIds_.end(), one.id) != heldIds_.end()) continue;
            const float away =
                std::max(std::fabs(float(one.column) - column), std::fabs(float(one.row) - row));
            if (away < nearest) {
                nearest = away;
                pointedLying_ = one.id;
            }
        }
    }
    // And the townsfolk, by the same reckoning, a body winning a tie: a monster in the town
    // is the more urgent thing under the pointer.
    for (const Standing& standing : folk_) {
        if (standing.folk < 0) continue;
        const content::Townsperson& one = tables_.folk[size_t(standing.folk)];
        const float away = reach(float(one.x), float(one.y), standing.figure);
        if (away < closest) {
            closest = away;
            pointedAt_ = 0;
            pointedFolk_ = standing.folk;
        }
    }
    // And something to sit on or lean against, only where nothing above is under the pointer --
    // MU's Action() tests SelectedOperate after the character and the NPC. MU2's World.Operate:
    // the ray against MU's own pick box, a square post on the placement's origin, axis-aligned,
    // with the model's rotation and scale nowhere in it -- 80 units tall, or 160 for a lean box,
    // since what is clicked there is a box nobody can see. Half a tile across where MU's is 0.8
    // (Poses.PickRadius, MU2's own narrowing: at MU's width the pointer turned into a seat a step
    // and a half before the seat). The nearest entry wins; MU takes the first in memory order.
    // **Only the perches MU's gate lets through** (content::usable). MU picks all of them and
    // shows the sit pointer over a log it will then refuse -- a pointer promising what the rules
    // have said no to. Invention: an unusable one is skipped and the click walks instead.
    if (pointedAt_ == 0 && pointedFolk_ < 0 && pointedLying_ == 0) {
        constexpr float kHalf = 0.25f;  // tiles; MU2's PickRadius, 25 units
        const std::vector<content::Perch>& perches = tables_.perches;
        float nearest = 1e9f;
        for (size_t i = 0; i < perches.size(); ++i) {
            const content::Perch& one = perches[i];
            if (!content::usable(tables_.grid, one)) continue;
            const float x = (one.x + 0.5f) * metresPerTile;
            const float z = -(one.y + 0.5f) * metresPerTile;
            const float floorY = ground_->heightAt(x, z);
            const float low[3] = {x - kHalf * metresPerTile, floorY, z - kHalf * metresPerTile};
            const float high[3] = {x + kHalf * metresPerTile,
                                   floorY + (one.tall ? 1.6f : 0.8f) * metresPerTile,
                                   z + kHalf * metresPerTile};
            const float from[3] = {nearPoint.x, nearPoint.y, nearPoint.z};
            const float along[3] = {direction.x, direction.y, direction.z};
            // The slab test: where the ray enters and leaves each axis's pair of planes.
            float enter = 0.0f, leave = 1e9f;
            bool missed = false;
            for (int axis = 0; axis < 3 && !missed; ++axis) {
                if (std::fabs(along[axis]) < 1e-6f) {
                    missed = from[axis] < low[axis] || from[axis] > high[axis];
                    continue;
                }
                float t0 = (low[axis] - from[axis]) / along[axis];
                float t1 = (high[axis] - from[axis]) / along[axis];
                if (t0 > t1) std::swap(t0, t1);
                enter = std::max(enter, t0);
                leave = std::min(leave, t1);
                missed = enter > leave;
            }
            if (!missed && enter < nearest) {
                nearest = enter;
                pointedPerch_ = int(i);
            }
        }
    }
}

void Play::pointAtLabel(uint32_t lying) {
    if (lying == 0 || !isOpen()) return;
    // Still held behind a falling monster: not yet a thing to be picked up.
    if (std::find(heldIds_.begin(), heldIds_.end(), lying) != heldIds_.end()) return;
    pointedLying_ = lying;
    pointedAt_ = 0;
    pointedFolk_ = -1;
    pointedPerch_ = -1;
}

void Play::leftClick() {
    if (!isOpen()) return;
    sim::Request request;
    if (pointedFolk_ >= 0) {
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(pointedFolk_);
    } else if (pointedAt_ == 0 && pointedLying_ != 0) {
        request.kind = sim::Request::Kind::Pick;
        request.target = pointedLying_;
    } else if (pointedAt_ != 0 && realm_.find(pointedAt_) && realm_.find(pointedAt_)->alive()) {
        request.kind = sim::Request::Kind::Attack;
        request.target = pointedAt_;
    } else if (pointedPerch_ >= 0) {
        request.kind = sim::Request::Kind::Perch;
        request.target = uint32_t(pointedPerch_);
    } else if (pointedColumn_ >= 0) {
        request.kind = sim::Request::Kind::WalkTo;
        request.column = pointedColumn_;
        request.row = pointedRow_;
    } else {
        return;
    }
    // Not while Teleport holds him: the realm refuses these there (Realm::accept -- every
    // other cast is broken by the click), and a marker planted on ground he is never going to
    // walk to, plus the early tick under it, would be the drawing promising what the rules have
    // already said no to. An Attack is let through, as it is there.
    if (realm_.held() && request.kind != sim::Request::Kind::Attack) return;
    // A flinch does NOT hold the click, and that is ours: MU refuses it in PLAYER_SHOCK
    // (ZzzInterface.cpp:3127), and the user turned that down on 2026-09-29 -- a click to move
    // while he is struck is a click to get out, and eating it read as the game not answering.
    // The walk it starts ends the flinch's clip (play_show.cpp, the flinch hold).
    realm_.ask(request);
    // Only from a stand; see Play::update for why never while walking.
    stepNow_ = !realm_.hero().walking && sinceEarly_ >= kEarlyApart &&
               (request.kind != sim::Request::Kind::WalkTo ||
                request.column != realm_.hero().column() || request.row != realm_.hero().row());
    // A walk, a pickup or a talk puts the marker where the walk ends; a fight takes it away,
    // as MU2's did -- an attack never shows one.
    mark_ = request.kind != sim::Request::Kind::Attack;
    if (!mark_) marker_.dismiss();
}

// The arena's hand. It raises the SAME request a click on a body raises and decides nothing
// itself -- not whether the blow lands, not whether he is close enough, not whether the target
// is a legal one; the realm refuses all three in press(). What it skips is the pointer.
//
// `stepNow_` is deliberately not set, which a click does set: taking a tick early is a
// presentation trick for the hand at the mouse, and a run whose whole point is that the same
// seed and the same --fixed-dt draw the same frames must not have its tick boundaries moved by
// where a monster happened to wander.
void Play::fight(uint32_t id) {
    if (!isOpen()) return;
    const sim::Body* target = realm_.find(id);
    if (!target || !target->alive() || !target->monster()) return;
    sim::Request request;
    request.kind = sim::Request::Kind::Attack;
    request.target = id;
    // With the right button's skill, so an arena shows what a class fights with: a wizard
    // throws his Energy Ball and a knight with an empty slot swings, as a right-click would.
    // Or with what `--arena-learn` taught him, which is what that option is for.
    request.skill = arenaLeft_ ? 0 : (arena_.learn != 0 ? arena_.learn : quickSkill_);
    realm_.ask(request);
    mark_ = false;
    marker_.dismiss();
}

bool Play::perch(int index) {
    if (!isOpen() || index < 0 || size_t(index) >= tables_.perches.size()) return false;
    sim::Request request;
    request.kind = sim::Request::Kind::Perch;
    request.target = uint32_t(index);
    realm_.ask(request);
    mark_ = true;
    return true;
}

void Play::rightClick() {
    if (!isOpen()) return;
    sim::Request request;
    // **The quick slot.** A monster under the pointer is fought with the right button's skill --
    // Energy Ball for a new wizard -- and the realm swings the weapon whenever the skill cannot be
    // thrown (Realm::press). The left button's attack is the weapon alone. The user, 2026-09-28,
    // the same for every class; with nothing in the slot it is the left button's attack.
    const sim::Body* at = pointedAt_ != 0 ? realm_.find(pointedAt_) : nullptr;
    const sim::SkillRow* quick = quickSkill_ != 0 ? sim::skillNumbered(quickSkill_) : nullptr;
    if (at != nullptr && at->alive() && at->monster()) {
        request.kind = sim::Request::Kind::Attack;
        request.target = pointedAt_;
        request.skill = quickSkill_;
    } else if (quick != nullptr && quick->aimsAtPointer() && pointedColumn_ >= 0) {
        // **A skill with a direction on the right button goes the way the mouse is** over bare
        // ground too (SkillRow::aimsAtPointer; the user, 2026-10-02): a press, as a key's is,
        // and no order changes -- he casts where he stands.
        realm_.invokeAt(quickSkill_, pointedColumn_, pointedRow_);
        mark_ = false;
        marker_.dismiss();
        return;
    } else {
        request.kind = sim::Request::Kind::Stop;
    }
    realm_.ask(request);
    mark_ = false;
    marker_.dismiss();
}

// Where every spell leaves a caster: the middle of his chest, a little toward what it is
// thrown at. The user's, 2026-09-28 ("spells come from same position somewhere in center of
// body"), after the throwing hand was tried: the two cast clips put that hand in two very different
// places (1.35 m up and in front, 1.90 m up overhead), so the ball jumped between throws. MU's own
// is a fixed height per spell -- 100 units for the bolt, 120 for the fireball -- and this is one
// height for both, as a share of the figure so a tall and a short caster are alike.
constexpr float kCastHeight = 0.6f;    // of his drawn height, from his feet
// Where a figureless summon's bar stands: a Stone Golem's height and a little, in metres.
constexpr float kSummonStandsTall = 2.0f;
// Metres toward the target: just past his outstretched arms, which reach 0.6 m on clip 147 --
// at 0.25 the ball was born inside them ("little bit front of arms").
constexpr float kCastForward = 0.7f;

bool Play::castFrom(const Drawn& caster, const float to[3], float out[3]) const {
    out[0] = caster.crown[0];
    out[1] = ground_ ? ground_->heightAt(caster.crown[0], caster.crown[2]) : 0.0f;
    out[2] = caster.crown[2];
    const FigureBody* look = caster.figure.body();
    if (look == nullptr) return false;
    out[1] += look->height * look->scale * kCastHeight;
    const float wayX = to[0] - caster.crown[0], wayZ = to[2] - caster.crown[2];
    const float flat = std::sqrt(wayX * wayX + wayZ * wayZ);
    if (flat > 1e-4f) {
        out[0] += wayX / flat * kCastForward;
        out[2] += wayZ / flat * kCastForward;
    }
    return true;
}

// From the weapon: the point its rig marks on it, carried by her hand (Figure::muzzle, MU2's
// Crowd.Rail). MU's own muzzle -- her feet plus (-10, -60, 135) units turned by her facing,
// 1.35 m up, 0.6 m toward the target, a hand's width to the side (fx/arrow.h) -- for a weapon
// that marks none. The model is her weapon's. The arrow on the string goes with it.
void Play::shootArrow(const Drawn& shooter, const float to[3], uint32_t whom, float seconds) {
    const float feet = ground_ ? ground_->heightAt(shooter.crown[0], shooter.crown[2]) : 0.0f;
    const float wayX = to[0] - shooter.crown[0], wayZ = to[2] - shooter.crown[2];
    const float flat = std::max(1e-4f, std::sqrt(wayX * wayX + wayZ * wayZ));
    const float fx = wayX / flat, fz = wayZ / flat;
    float muzzle[3] = {shooter.crown[0] + fx * 0.6f + fz * 0.1f, feet + 1.35f,
                       shooter.crown[2] + fz * 0.6f - fx * 0.1f};
    float rail[3];
    const bool fromWeapon = shooter.figure.muzzle(muzzle, rail);
    if (Drawn* own = drawnOf(shooter.id)) own->figure.nock(false);
    if (whom != 0 || shooter.id == realm_.hero().id) {
        // Off her feet, so a muzzle a metre wide of the weapon shows in the log.
        core::logf("arrow: from the %s %.2f across, %.2f up, %.2f on from her feet, her clip "
                   "%d at key %.2f",
                   fromWeapon ? "weapon" : "chest",
                   double((muzzle[0] - shooter.crown[0]) * fz - (muzzle[2] - shooter.crown[2]) * fx),
                   double(muzzle[1] - feet),
                   double((muzzle[0] - shooter.crown[0]) * fx + (muzzle[2] - shooter.crown[2]) * fz),
                   shooter.figure.clip(), double(keyOf(shooter.figure)));
    }
    Arrows::Model model = Arrows::Wood;
    const float* tint = nullptr;
    if (const sim::Body* body = realm_.find(shooter.id);
        body && body->weapon >= 0 && size_t(body->weapon) < tables_.arms.size()) {
        const content::Arm& arm = tables_.arms[size_t(body->weapon)];
        model = Arrows::modelFor(arm.group, arm.number);
        tint = Arrows::tintFor(arm.group, arm.number);
    }
    arrows_.loose(muzzle, to, whom, model, 0, tint, seconds);
}

bool Play::shoots(uint32_t id, Arrows::Model* model) {
    const Drawn* one = drawnOf(id);
    const FigureBody* look = one ? one->figure.body() : nullptr;
    if (!look) return false;
    if (look->name == kHunterFigure) {
        *model = Arrows::Saw;
        return true;
    }
    // A guard: an arrow off a bow, and off a crossbow the Light Crossbow's own bolt, which is
    // the one every crossbow guard holds (MU's CreateArrow gives MODEL_LIGHT_CROSSBOW Laser).
    // And a monster on the player rig holding a bow -- the Dungeon's Skeleton Archer, whose blow
    // is an arrow at its AttackRange 5 -- as a guard does.
    const sim::Body* body = realm_.find(id);
    if (!body || (body->warden < 0 && !body->monster())) return false;
    if (look->stance == "bow") {
        *model = Arrows::Wood;
        return true;
    }
    if (look->stance == "crossbow") {
        *model = Arrows::Laser;
        return true;
    }
    return false;
}

void Play::volleyShot(uint32_t shooter, uint32_t target) {
    const Drawn* from = drawnOf(shooter);
    const Drawn* to = drawnOf(target);
    if (!from || !to || !from->placed || !to->placed || !ground_) return;
    Arrows::Model model = Arrows::Saw;
    if (!shoots(shooter, &model)) return;
    // At the middle of the one it is shot at, as every arrow is aimed.
    const FigureBody* aim = to->figure.body();
    const float tall = aim ? aim->height * aim->scale : 1.0f;
    const float at[3] = {to->crown[0], to->crown[1] - tall * 0.5f, to->crown[2]};
    const float feet = ground_->heightAt(from->crown[0], from->crown[2]);
    const float wayX = at[0] - from->crown[0], wayZ = at[2] - from->crown[2];
    const float flat = std::max(1e-4f, std::sqrt(wayX * wayX + wayZ * wayZ));
    const float fx = wayX / flat, fz = wayZ / flat;
    // MU's muzzle, (-10, -60, 135) turned by its facing, as shootArrow's.
    const float muzzle[3] = {from->crown[0] + fx * 0.6f + fz * 0.1f, feet + 1.35f,
                             from->crown[2] + fz * 0.6f - fx * 0.1f};
    arrows_.loose(muzzle, at, target, model, shooter);
    // Read afterwards, as the meteor's line is: an arrow at two tiles is in the air for a tenth
    // of a second, and no shot schedule proves it flew.
    core::logf("arrow: tick %lld, #%u looses at #%u from %.1f m", (long long)realm_.tick(),
               shooter, target, double(flat));
}

void Play::benchBolt(float tiles, float acrossX, float acrossZ, int32_t skill) {
    if (!isOpen() || drawn_.empty() || !drawn_[0].placed || !ground_) return;
    const Drawn& hero = drawn_[0];
    const float feet[3] = {hero.crown[0], ground_->heightAt(hero.crown[0], hero.crown[2]),
                           hero.crown[2]};
    const float flat = std::max(1e-4f, std::sqrt(acrossX * acrossX + acrossZ * acrossZ));
    const float far = tiles * ground_->metresPerTile() / flat;
    // A man's middle, as if one stood there.
    const float to[3] = {feet[0] + acrossX * far, feet[1] + 1.0f, feet[2] + acrossZ * far};
    float from[3];
    const bool atHand = castFrom(hero, to, from);
    if (skill == sim::skill::kNone) {
        // Her own weapon's arrow, from the muzzle a shot leaves.
        shootArrow(hero, to, 0);
        return;
    }
    if (skill == sim::skill::kFireBall) {
        meteor_.hurl(from, to, 0, atHand);
    } else if (skill == sim::skill::kMeteorite) {
        meteor_.cast(to[0], to[2], 0);
    } else if (skill == sim::skill::kPoison) {
        const float feet[3] = {to[0], ground_ ? ground_->heightAt(to[0], to[2]) : to[1], to[2]};
        poison_.cast(feet, hero.yaw);
    } else if (skill == sim::skill::kIce) {
        const float feet[3] = {to[0], ground_ ? ground_->heightAt(to[0], to[2]) : to[1], to[2]};
        ice_.freeze(feet, hero.yaw);
    } else if (skill == sim::skill::kLightning) {
        thunder_.strike(from, to, 0);
    } else if (skill == sim::skill::kPowerWave) {
        const float ground[3] = {from[0], feet[1], from[2]};
        wave_.cast(ground, to);
    } else if (skill == sim::skill::kFlame) {
        const float at[3] = {to[0], ground_ ? ground_->heightAt(to[0], to[2]) : to[1], to[2]};
        flame_.light(at, hero.yaw);
    } else {
        bolt_.cast(from, to, 0, atHand);
    }
    const int index = sim::skillIndexOf(skill);
    if (index >= 0 && heard_.skill[index] >= 0) emit(heard_.skill[index], from[0], from[2], hero.id);
}

void Play::benchFace(float acrossX, float acrossZ) {
    if (!isOpen()) return;
    const float flat = std::max(1e-4f, std::sqrt(acrossX * acrossX + acrossZ * acrossZ));
    sim::Request request;
    request.kind = sim::Request::Kind::WalkTo;
    // World x is the column and world z the negated row (docs/conventions.md).
    request.column = realm_.hero().column() + int(std::lround(acrossX / flat));
    request.row = realm_.hero().row() - int(std::lround(acrossZ / flat));
    realm_.ask(request);
}

void Play::walkTo(int column, int row) {
    if (!isOpen()) return;
    sim::Request request;
    request.kind = sim::Request::Kind::WalkTo;
    request.column = column;
    request.row = row;
    realm_.ask(request);
}

bool Play::crownOf(uint32_t id, const float* viewProj, int width, int height, float* x,
                   float* y) const {
    const size_t at = size_t(id) - 1;
    if (!ground_ || at >= drawn_.size() || drawn_[at].id != id) return false;
    const Drawn& one = drawn_[at];
    float world[4] = {one.crown[0], 0.0f, one.crown[2], 1.0f};
    if (one.placed) {
        // MU2's CrownClearance: a third of a tile between the top of the body and the bar.
        world[1] = one.crown[1] + 0.33f * ground_->metresPerTile();
    } else {
        // **Her summon, on a map whose figure table does not carry its breed**: fighting, but
        // undrawn (Play::update, What::Spawned). Its bar still stands where it is -- the user's,
        // 2026-09-28, "on any map where she casts it" -- over its own tile at a standing height.
        const sim::Body* summon = realm_.find(id);
        if (summon == nullptr || summon->summoner == 0 || !summon->alive()) return false;
        const float metres = ground_->metresPerTile();
        world[0] = (summon->x + 0.5f) * metres;
        world[2] = -(summon->y + 0.5f) * metres;
        world[1] = ground_->heightAt(world[0], world[2]) + kSummonStandsTall;
    }
    float clip[4];
    bx::vec4MulMtx(clip, world, viewProj);
    if (clip[3] <= 0.0f) return false;
    *x = (clip[0] / clip[3] * 0.5f + 0.5f) * float(width);
    *y = (0.5f - clip[1] / clip[3] * 0.5f) * float(height);
    return true;
}

bool Play::folkCrownOf(int folk, const float* viewProj, int width, int height, float* x,
                       float* y) const {
    if (!ground_ || folk < 0) return false;
    if (const uint32_t guard = wardenBody(folk)) return crownOf(guard, viewProj, width, height, x, y);
    for (const Standing& one : folk_) {
        if (one.folk != folk || !one.figure.body()) continue;
        const float* at = one.figure.position();
        const float top = at[1] + one.figure.body()->height * one.figure.scale();
        const float world[4] = {at[0], top + 0.33f * ground_->metresPerTile(), at[2], 1.0f};
        float clip[4];
        bx::vec4MulMtx(clip, world, viewProj);
        if (clip[3] <= 0.0f) return false;
        // Across, from the face: a point raised over the head leans away from the screen's middle
        // in the perspective, as the whole figure does, and the quest mark hung off to the side
        // of anyone standing off-centre -- Sevina, at the left of the frame, by eleven pixels
        // (the user, 2026-09-30). So the across is the head joint's, where the pose holds it;
        // the height is still the raised point's.
        float head[4] = {at[0], top, at[2], 1.0f};
        if (const content::Mesh* skeleton = one.figure.body()->skeletonMesh) {
            const std::vector<content::Bone>& bones = skeleton->bones();
            for (size_t b = 0; b < bones.size(); ++b) {
                if (bones[b].name != "Bip01 Head") continue;
                // The joint's place in the model is its inverse bind's inverse.
                float bind[16];
                bx::mtxInverse(bind, bones[b].inverseBind);
                const float joint[3] = {bind[12], bind[13], bind[14]};
                float onPose[3];
                if (one.figure.pointOnBind(int(b), joint, onPose)) {
                    head[0] = onPose[0];
                    head[1] = onPose[1];
                    head[2] = onPose[2];
                }
                break;
            }
        }
        float onHead[4];
        bx::vec4MulMtx(onHead, head, viewProj);
        const float across = onHead[3] > 0.0f ? onHead[0] / onHead[3] : clip[0] / clip[3];
        *x = (across * 0.5f + 0.5f) * float(width);
        *y = (0.5f - clip[1] / clip[3] * 0.5f) * float(height);
        return true;
    }
    return false;
}

void Play::dropsOnScreen(const float* viewProj, int width, int height,
                         std::vector<OnScreen>& out) const {
    out.clear();
    if (!ground_) return;
    const float metresPerTile = ground_->metresPerTile();
    for (const sim::Lying& one : realm_.lying()) {
        // Not while it is held behind a falling monster or still tossing and bouncing: the
        // name goes up once the thing lies still.
        if (std::find(settled_.begin(), settled_.end(), one.id) == settled_.end()) continue;
        const float x = (float(one.column) + 0.5f) * metresPerTile;
        const float z = -(float(one.row) + 0.5f) * metresPerTile;
        // MU2's LabelLift, thirty units over the thing, and the thing a hand's height up.
        const float world[4] = {x, ground_->heightAt(x, z) + 0.4f, z, 1.0f};
        float clip[4];
        bx::vec4MulMtx(clip, world, viewProj);
        if (clip[3] <= 0.0f) continue;
        const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
        if (nx < -1.1f || nx > 1.1f || ny < -1.1f || ny > 1.1f) continue;
        out.push_back({one.id, (nx * 0.5f + 0.5f) * float(width), (0.5f - ny * 0.5f) * float(height)});
    }
}

}  // namespace mu::game
