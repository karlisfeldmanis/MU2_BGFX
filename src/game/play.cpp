#include "game/play.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// The sim's own rate. docs/conventions.md, "Time": the tick never reads the frame's delta and
// the frame never decides how many ticks have passed except by this number.
constexpr double kTickSeconds = 1.0 / 20.0;
// The most ticks one frame may run. A window that was away -- dragged, or stalled on a
// screenshot's readback -- comes back owing seconds of simulation, and stepping all of it in
// one frame is a stall that makes the next frame owe more. MU2 called this the mirror clock's
// debt and repaid it a little at a time; here the debt is simply forgiven, because a single
// player game has nobody to be out of step with.
constexpr int kMostTicks = 5;

// How far from the camera a body is drawn at all, in tiles. MU's camera is fixed and close and
// sees about twenty tiles; posing all 290 of Lorencia's bodies every frame would spend the
// crowd's whole account on figures nobody can see. Foundation 7's "ranges per kind", applied to
// the one kind this sprint draws.
constexpr float kDrawRange = 32.0f;

// MU's own action numbers for a swing, by the stance the figure holds its weapon in
// (index.json's `actions` table: 38 "Attack fist", 39 "Attack sword right 1", 43 "Attack two
// hand sword 1", 46 "Attack spear 1", 47 "Attack scythe 1"). A monster's numbers are its OWN
// table and not these -- `monster_actions` 3 is "Attack 1" -- which is the trap figures.h
// names, so the two are looked up separately below and never out of one table.
int attackSlotFor(const std::string& stance) {
    if (stance == "sword") return 39;
    if (stance == "two_hand_sword") return 43;
    if (stance == "spear") return 46;
    if (stance == "scythe") return 47;
    if (stance == "bow") return 50;
    if (stance == "crossbow") return 51;
    return 38;  // bare hands
}

// Which character the window plays. Sprint 9's character select is what replaces this; it is a
// constant here rather than a switch because there is no save to read it out of yet.
const char* kHeroFigure = "DarkKnight";

}  // namespace

bool Play::open(const std::string& assetDir, const std::string& world,
                const content::Ground* ground, const Figures* figures, uint64_t seed, int kin,
                int level, int column, int row, const std::string& weapon,
                const std::string& shield) {
    ground_ = ground;
    figures_ = figures;
    const std::string path = core::join(assetDir, "cooked/" + world + "/" + world + ".mur");
    std::string error;
    if (!content::loadTables(path, tables_, error)) {
        core::logError("no rules for %s: %s (tools/cook.py --world %s --only tables)",
                       world.c_str(), error.c_str(), world.c_str());
        return false;
    }

    // The two copies of the attribute grid, compared -- the one the cook wrote into the tables
    // and the one the ground read out of attributes.png. This is the only run in which both
    // are in memory at once, and docs/conventions.md promised the check the day there was one.
    if (ground_ && !ground_->grid().empty()) {
        if (ground_->grid().size() != tables_.grid.size()) {
            core::logError("the ground's grid is %d tiles a side and the tables' is %d",
                           ground_->grid().size(), tables_.grid.size());
        } else {
            size_t differ = 0;
            for (int r = 0; r < tables_.grid.size(); ++r) {
                for (int c = 0; c < tables_.grid.size(); ++c) {
                    if (ground_->grid().at(c, r) != tables_.grid.at(c, r)) ++differ;
                }
            }
            if (differ > 0) {
                core::logError("the ground's attribute grid and the cooked one differ on %zu "
                               "tiles -- one of them is stale", differ);
            }
        }
    }

    if (!realm_.raise(&tables_, seed, column, row, sim::Kin(kin), level)) return false;

    // A character made above level 1 arrives with his points in hand and nobody to spend them:
    // there is no character screen until sprint 9, and unspent he is a level-1 man with more
    // health who cannot lift the weapon he was asked to carry. So the same courtesy the
    // headless hand does itself (game/headless.cpp) -- pay for what he is about to hold, the
    // rest into strength -- and both go when the stat window arrives.
    if (realm_.hero().pointsInHand > 0) {
        int points = realm_.hero().pointsInHand;
        int wantsStrength = 0, wantsAgility = 0;
        for (const std::string& name : {weapon, shield}) {
            const int32_t index = name.empty() ? -1 : tables_.armNamed(name);
            if (index < 0) continue;
            const content::Arm& arm = tables_.arms[size_t(index)];
            wantsStrength = std::max(wantsStrength, arm.wantsStrength);
            wantsAgility = std::max(wantsAgility, arm.wantsAgility);
        }
        const int intoStrength =
            std::min(points, std::max(0, wantsStrength - realm_.hero().points.strength));
        points -= intoStrength;
        const int intoAgility =
            std::min(points, std::max(0, wantsAgility - realm_.hero().points.agility));
        points -= intoAgility;
        realm_.spend(intoStrength + points, intoAgility, 0, 0);
    }

    // What he holds. The character that is DRAWN carries whatever the cook put in his hands --
    // `figures.json` gives the Dark Knight a Kris and a Plate Shield -- and the sim knows only
    // what it was told here, so a mismatch is said out loud rather than left to be noticed as
    // a fight that does not match the picture.
    if (!weapon.empty() || !shield.empty()) {
        const int32_t held = weapon.empty() ? -1 : tables_.armNamed(weapon);
        const int32_t worn = shield.empty() ? -1 : tables_.armNamed(shield);
        if ((!weapon.empty() && held < 0) || (!shield.empty() && worn < 0)) {
            core::logError("no arm called %s%s%s", weapon.c_str(), shield.empty() ? "" : " or ",
                           shield.c_str());
        } else if (!realm_.equip(held, worn)) {
            core::logError("he cannot hold that: %s", realm_.refusal().c_str());
        } else {
            core::logf("play: holding %s%s%s -- damage %d to %d, defence %d, a swing every "
                       "%d ms (%d ticks)", weapon.c_str(), shield.empty() ? "" : " and ",
                       shield.c_str(), realm_.hero().stats.minimumDamage,
                       realm_.hero().stats.maximumDamage, realm_.hero().stats.defense,
                       realm_.hero().swingMs, realm_.hero().swingTicks);
        }
    }

    // A figure for every body, made once. Bodies are never added or removed after the realm is
    // raised -- a dead monster is a body waiting for its respawn -- so this list is as fixed as
    // the realm's own, and a frame walks it without allocating.
    drawn_.clear();
    drawn_.reserve(realm_.bodies().size());
    size_t bones = 0, dressed = 0, bare = 0;
    for (const sim::Body& body : realm_.bodies()) {
        Drawn one;
        one.id = body.id;
        one.wasX = one.nowX = body.x;
        one.wasY = one.nowY = body.y;
        const FigureBody* look = nullptr;
        if (figures_) {
            look = body.player ? figures_->body(kHeroFigure)
                               : figures_->body(tables_.kinds[size_t(body.kind)].figure);
        }
        if (look) {
            const float at[3] = {0, 0, 0};
            one.figure.stand(look, at, 0.0f, look->scale);
            bones = std::max(bones, look->boneCount());
            ++dressed;
            // The swing, found once. A player's stance decides which of MU's attack clips it
            // is; a monster has its own two and takes the first it has.
            if (look->library) {
                if (body.player) {
                    one.attackClip = look->library->find(attackSlotFor(look->stance));
                    if (one.attackClip < 0) one.attackClip = look->library->find(38);
                } else {
                    one.attackClip = look->library->find(3);
                    if (one.attackClip < 0) one.attackClip = look->library->find(4);
                }
            }
        } else {
            ++bare;
        }
        drawn_.push_back(std::move(one));
    }
    scratch_.assign(std::max<size_t>(128, bones) * 12, 0.0f);
    remember();

    core::logf("play: %zu bodies, %zu of them with a figure to wear (%zu have none cooked), "
               "hero at tile %d,%d", drawn_.size(), dressed, bare, realm_.hero().column(),
               realm_.hero().row());
    return true;
}

void Play::shutdown() {
    drawn_.clear();
    scratch_.clear();
    accumulator_ = 0.0;
}

void Play::remember() {
    for (Drawn& one : drawn_) {
        one.wasX = one.nowX;
        one.wasY = one.nowY;
        if (const sim::Body* body = realm_.find(one.id)) {
            one.nowX = body->x;
            one.nowY = body->y;
        }
    }
}

void Play::update(double seconds) {
    if (!isOpen()) return;
    accumulator_ += seconds;
    int stepped = 0;
    const int64_t started = bx::getHPCounter();
    while (accumulator_ >= kTickSeconds && stepped < kMostTicks) {
        realm_.step();
        // AFTER the step, not before it. Before, `now` held the state at the START of the tick
        // and `was` the start of the one before, so the picture trailed the sim by one whole
        // tick on top of the interpolation's own -- a figure at `through_ = 0` was 100 ms
        // behind what the sim had already decided. Now the two ends really are the ticks
        // either side of where the clock stands, which is what the comment below claims.
        remember();
        sim::audit(realm_, findings_);
        for (const sim::Happening& happening : realm_.happenings()) {
            // A swing is drawn because it is a POSE and not an effect: the blood, the number,
            // the fall and the health plate that hang off the landing are sprint 6's, and none
            // of them is here. What a blow does to the picture today is put the attacker into
            // its attack clip, which then blends back to idle or walk when it ends.
            if (happening.what == sim::What::Hit || happening.what == sim::What::Missed) {
                if (Drawn* swinger = drawnOf(happening.who)) {
                    if (swinger->attackClip >= 0 && swinger->figure.body()) {
                        swinger->figure.play(swinger->attackClip, true);
                        // The clip has to fit between two blows, and MU's own reason is that
                        // the attack speed makes the CLIP run faster -- the swing rate follows
                        // from that, so anything that plays the animation has to apply the same
                        // scaling or the man swings at one speed and connects at another
                        // (Beast.cs:820-826). Only ever faster: a monster whose row gives it
                        // 1.4 s between blows plays its half-second swing at its own pace and
                        // waits, as MU does, rather than being smeared out to fill the gap.
                        const sim::Body* body = realm_.find(happening.who);
                        const float between =
                            body ? float(body->swingTicks) * float(kTickSeconds) : 0.0f;
                        const float clip = swinger->figure.length();
                        swinger->swingPace =
                            (between > 0.01f && clip > between) ? clip / between : 1.0f;
                        swinger->swinging = clip / swinger->swingPace;
                    }
                }
            }
            // Everything but the tile crossings, which are most of the log and none of the
            // news. The line is what the run is read by until sprint 6 draws any of it.
            if (happening.what == sim::What::Stepped) continue;
            lastLine_ = sim::describe(happening, realm_);
        }
        accumulator_ -= kTickSeconds;
        ++stepped;
    }
    if (stepped > 0) {
        tickMs_ = double(bx::getHPCounter() - started) * 1000.0 /
                  double(bx::getHPFrequency()) / double(stepped);
        // Whatever is left over after the cap is forgiven rather than owed. See kMostTicks.
        if (accumulator_ >= kTickSeconds * kMostTicks) accumulator_ = 0.0;
    }
    through_ = float(std::min(1.0, accumulator_ / kTickSeconds));
    follow();
    for (Drawn& one : drawn_) {
        // A swing runs at its own pace and everything else at the clip's own.
        const float pace = one.swinging > 0.0f ? one.swingPace : 1.0f;
        one.figure.update(float(seconds) * pace);
        if (one.swinging > 0.0f) one.swinging -= float(seconds);
    }
}

void Play::follow() {
    if (!ground_) return;
    const float metresPerTile = ground_->metresPerTile();
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        if (!body || !one.figure.body()) {
            one.visible = false;
            continue;
        }
        // A dead body is not drawn at all this sprint. The fall, the corpse and how long it
        // lies there are the landing cue's business and belong to sprint 6; a monster that
        // vanishes the tick it dies is honest about that, and a monster left standing would
        // not be.
        one.visible = body->alive();
        if (!one.visible) continue;
        // Out of range is not drawn and not posed. Measured in the log beside the drawn count,
        // because foundation 7 says a culling change that is not visible in those numbers did
        // not happen.
        const sim::Body& hero = realm_.hero();
        if (std::max(std::fabs(body->x - hero.x), std::fabs(body->y - hero.y)) > kDrawRange) {
            one.visible = false;
            continue;
        }

        // Between the two ticks either side of where the clock stands. Presentation only:
        // every reach, aim and hit in the sim used the sim's own position, and this one is
        // never read back into it. docs/conventions.md, "Time".
        const float tileX = one.wasX + (one.nowX - one.wasX) * through_;
        const float tileY = one.wasY + (one.nowY - one.wasY) * through_;
        const float x = (tileX + 0.5f) * metresPerTile;
        const float z = -(tileY + 0.5f) * metresPerTile;
        // A model looks down +z, and placementTransform negates the angle it is given, so +z
        // ends up along (sin yaw, cos yaw). The sim's facing is an angle in TILE space, where
        // +x is the column and +y is the row -- and the row runs south, which is -z. So the
        // direction of travel in the world is (cos facing, -sin facing) and the yaw that
        // points a model down it is atan2 of those two, in that order.
        // docs/conventions.md, "Space" and "Matrices".
        const float dx = std::cos(body->facing);
        const float dz = -std::sin(body->facing);
        one.yaw = std::atan2(dx, dz);

        const float position[3] = {x, ground_->heightAt(x, z), z};
        // The safe zone is a stance and not only a place: inside one MU carries the weapon on
        // the back and stands in the unarmed idle, and steps out of it with the weapon drawn.
        // `place` moves the weapon; the clip below is the other half of the same rule, and the
        // 0.18 s crossfade in Figure::play is what makes the change a blend rather than a cut.
        const bool safe = tables_.grid.safe(body->column(), body->row());
        one.figure.place(position, one.yaw, safe);
        // A swing holds until it has played out, and then walk or idle take it back. `play`
        // ignores a request for the clip already running, so the two below are comparisons
        // rather than restarts, and the blend between them is the crossfade's.
        if (one.swinging > 0.0f) continue;
        const FigureBody* look = one.figure.body();
        int clip = look->idleClip;
        if (body->walking) {
            clip = look->walkClip;
        } else if (safe && look->idleSafeClip >= 0) {
            clip = look->idleSafeClip;
        }
        if (clip >= 0) one.figure.play(clip);
    }
}

Play::Drawn* Play::drawnOf(uint32_t id) {
    // The bodies are made once and never reordered, and ids are handed out from 1 in that same
    // order, so this is an index and not a search.
    const size_t at = size_t(id) - 1;
    return at < drawn_.size() && drawn_[at].id == id ? &drawn_[at] : nullptr;
}

void Play::focus(float* column, float* row) const {
    if (drawn_.empty()) return;
    const Drawn& hero = drawn_[0];
    *column = hero.wasX + (hero.nowX - hero.wasX) * through_;
    *row = hero.wasY + (hero.nowY - hero.wasY) * through_;
}

void Play::point(const gfx::Camera& camera, const float* view, const float* proj, float pixelX,
                 float pixelY, int width, int height) {
    pointedColumn_ = pointedRow_ = -1;
    pointedAt_ = 0;
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
    float closest = 1.2f;
    for (const sim::Body& body : realm_.bodies()) {
        if (body.player || !body.alive()) continue;
        const float away = std::max(std::fabs(body.x - column), std::fabs(body.y - row));
        if (away < closest) {
            closest = away;
            pointedAt_ = body.id;
        }
    }
}

void Play::leftClick() {
    if (!isOpen()) return;
    sim::Request request;
    if (pointedAt_ != 0) {
        request.kind = sim::Request::Kind::Attack;
        request.target = pointedAt_;
    } else if (pointedColumn_ >= 0) {
        request.kind = sim::Request::Kind::WalkTo;
        request.column = pointedColumn_;
        request.row = pointedRow_;
    } else {
        return;
    }
    realm_.ask(request);
}

void Play::rightClick() {
    if (!isOpen()) return;
    sim::Request request;
    request.kind = sim::Request::Kind::Stop;
    realm_.ask(request);
}

void Play::gather(gfx::Renderer& renderer, const float* viewProj, std::vector<gfx::Drawable>& out,
                  std::vector<gfx::Drawable>* casters) {
    for (Drawn& one : drawn_) {
        if (!one.visible || !one.figure.body()) continue;
        const int bones = one.figure.pose(scratch_.data());
        const int palette = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        // The sun's list is every figure, as the town's is: a body behind the camera still
        // casts into the frame, and culling the shadow pass with the camera's frustum is the
        // bug foundation 7 names. The camera's own cull is Crowd's frustum test and is owed
        // here; at Lorencia's 290 bodies, of which a handful are ever near the camera, it is
        // the next thing to do and not this sprint's.
        if (casters) one.figure.gather(palette, *casters);
        one.figure.gather(palette, out);
    }
}

}  // namespace mu::game
