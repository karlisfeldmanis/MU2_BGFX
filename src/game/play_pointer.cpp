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
        if (body.player) continue;
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
            pointedAt_ = body.id;
        }
    }
    // What lies on the ground, only where nothing living is closer: a click on a drop next
    // to a monster is a click on the monster, as MU's own picking orders it.
    if (pointedAt_ == 0) {
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
    } else if (pointedColumn_ >= 0) {
        request.kind = sim::Request::Kind::WalkTo;
        request.column = pointedColumn_;
        request.row = pointedRow_;
    } else {
        return;
    }
    // Not while a skill's clip is running: the realm refuses these three (Realm::accept -- only
    // the auto-attack is cancelled by a click to move), and a marker planted on ground he is
    // never going to walk to, plus the early tick under it, would be the drawing promising what
    // the rules have already said no to. An Attack is let through, as it is there.
    if (realm_.casting() && request.kind != sim::Request::Kind::Attack) return;
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
    if (!target || !target->alive() || target->player) return;
    sim::Request request;
    request.kind = sim::Request::Kind::Attack;
    request.target = id;
    realm_.ask(request);
    mark_ = false;
    marker_.dismiss();
}

void Play::rightClick() {
    if (!isOpen()) return;
    sim::Request request;
    request.kind = sim::Request::Kind::Stop;
    realm_.ask(request);
    mark_ = false;
    marker_.dismiss();
}

bool Play::crownOf(uint32_t id, const float* viewProj, int width, int height, float* x,
                   float* y) const {
    const size_t at = size_t(id) - 1;
    if (!ground_ || at >= drawn_.size() || drawn_[at].id != id || !drawn_[at].placed) {
        return false;
    }
    const Drawn& one = drawn_[at];
    // MU2's CrownClearance: a third of a tile between the top of the body and the bar.
    const float world[4] = {one.crown[0], one.crown[1] + 0.33f * ground_->metresPerTile(),
                            one.crown[2], 1.0f};
    float clip[4];
    bx::vec4MulMtx(clip, world, viewProj);
    if (clip[3] <= 0.0f) return false;
    *x = (clip[0] / clip[3] * 0.5f + 0.5f) * float(width);
    *y = (0.5f - clip[1] / clip[3] * 0.5f) * float(height);
    return true;
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
