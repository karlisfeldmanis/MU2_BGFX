#include "game/world/leaves.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

// Sixty a second, which is the rate MU's leaf numbers are written for. Not the birds'
// twenty-five: `MoveEtcLeaf` is in the render loop and scales by `FPS_ANIMATION_FACTOR`
// against sixty, which is the reading MU2 settled on after its first pass ran them at the
// boids' rate and had the wind at two and a half times the client's.
constexpr float kReference = 60.0f;

// How big one leaf is drawn, in metres, and which part of the sheet it draws.
//
// MU2's quad was 3.2 by 2.4 of MU's units -- 0.032 by 0.024 m -- drawn ALPHA-SCISSORED at a
// threshold of 0.15. That threshold is doing more than it looks: leaf01.png is 16 by 16 and
// its alpha never reaches 208 of 255, mean 32, so the sheet is nearly all soft fringe. What
// survived 0.15 is 84 texels in a bbox 10 wide by 16 tall -- the middle ten columns. The
// empty three either side were thrown away and never drawn.
//
// Here the leaf goes in the sorted transparent pass instead, which has no cutoff, so a quad
// of MU2's size drew the whole 16 columns as a soft halo: 60% wider than MU2's leaf and with
// a fading edge where MU2's had none. At the game's camera that is about eight pixels of pale
// smudge, and the product owner's word for it was "too large" (2026-09-23).
//
// So the UVs are cropped to the columns that survived MU2's threshold, and the quad is sized
// to that crop -- nothing outside MU2's own shape is drawn at all -- and then the whole thing
// is taken to about half MU2's linear size, which at eight metres is four pixels. A speck
// caught in the light, which is what the header says these are for.
constexpr float kU0 = 3.0f / 16.0f;   // the ">= 0.15" bbox, measured off the sheet
constexpr float kU1 = 13.0f / 16.0f;
constexpr float kHalfWidth = 0.008f;
constexpr float kHalfHeight = 0.0095f;

// How fast a landed leaf darkens away, per reference frame -- the client's twentieth a frame,
// so it darkens into the terrain over about a second rather than blinking out.
constexpr float kFadeRate = 0.05f;
// And how fast the ones still in the air go once the player steps inside: five times that,
// which clears the air in about a third of a second.
constexpr float kIndoorFade = 0.25f;

// What the client's speeds actually come out at, pinned. See the header.
constexpr float kWindScale = 0.10f;

// How far a leaf may get from the player before it is taken back, in metres.
constexpr float kStrayDistance = 16.0f;

// How much of its own brightness a leaf is drawn at.
constexpr float kFaintness = 0.62f;

// A drop: `RenderPlane3D(1, 20)`, which is half-extents, so two centimetres by forty -- the
// longest one here. Each drop draws its own length and faintness, so a shower is a scatter of
// unequal streaks rather than a grid of one dash: the user, 2026-09-28, "not heavy visible,
// more random, more realistic".
constexpr float kDropHalfWidth = 0.008f;
constexpr float kDropShortest = 0.08f, kDropLongest = 0.2f;
// Where it starts: CreateHeavenRain's 200 to 399 units over the hero, three to five metres here
// as Devil Square's 300 to 499 has it, over the leaves' own field.
constexpr float kDropLow = 3.0f, kDropHigh = 5.0f;
// How fast, metres a second. MU's 20 to 43 units a frame at its 25 is five to eleven metres a
// second; the bottom of that reads as sleet on this camera, so the fall is the top half.
constexpr float kDropSlow = 8.0f, kDropFast = 11.0f;
// And how far off plumb: MU tilts a drop 20 to 49 degrees about its x, which is towards or away
// from its camera and hardly shows. **Invention:** the tilt here is across the screen, the way
// the leaves blow, and gentle. On top of each drop's own share of it, a gust swings the whole
// shower slowly -- MU's RainAngle, `sin(WorldTime * 0.0005) * 20` -- here a quarter-radian
// swing over about forty seconds.
constexpr float kSlantLow = 0.04f, kSlantHigh = 0.16f;  // radians
constexpr float kGustSwing = 0.1f;
constexpr float kGustSeconds = 40.0f;
// The colour a drop is drawn in, over the whitened sheet, and the most of it any one shows:
// a drop is caught by the light, not painted on the air.
constexpr float kDropColour[3] = {0.8f, 0.86f, 0.94f};
constexpr float kDropAlpha = 0.17f;  // 0.28 until the user asked for it less visible

// The ring a drop leaves: twenty frames at 25, a scale of 0.8 to 1.3 that grows 0.03 a frame
// (ZzzEffectParticle.cpp, BITMAP_RAIN_CIRCLE). The sheet is 32 by 16, an ellipse already drawn in
// perspective, and a scale of one is taken as 36 centimetres across -- invention, by eye. The
// painting is a dim blue, (0.3, 0.38, 0.45) at about half alpha, which vanished on Noria's lit
// ground; it is drawn lifted, still its own blue.
constexpr float kRingSeconds = 20.0f / 25.0f;
constexpr float kRingGrowth = 0.03f * 25.0f;  // scale a second
constexpr float kRingHalfWidth = 0.18f;
constexpr float kRingLift = 1.6f;
constexpr float kRingAlpha = 0.3f;  // 0.35 standing, 0.55 flat was "visible too much"

// Devias's snow. MU's sprite scales are the quad's full width in its units, so 5 is five
// centimetres and the glint's 10 is ten. Smaller than that here: the user, 2026-09-29, asked
// for finer flakes than MU's own, so three centimetres and six.
constexpr float kFlakeHalf = 0.015f;
constexpr float kStarHalf = 0.03f;
// CreateDeviasSnow's 200 to 399 units over the hero.
constexpr float kFlakeLow = 2.0f, kFlakeHigh = 3.99f;
// The fall, metres a second. **Invention:** MU's 8 to 23 units a frame is five to fourteen
// metres a second, a blizzard; a tenth of it is a daytime snowfall, which is what this is.
constexpr float kFlakeSlow = 0.55f, kFlakeFast = 1.3f;
// Its slant: the client turns the fall 30 degrees about x, which puts a share of tan 30 of it
// along MU's -y, towards the camera here.
constexpr float kFlakeSlant = 0.577f;
// The sway, OURS: a slow pendulum across the fall, a few centimetres, over two to four seconds.
constexpr float kSwayMetres = 0.18f;
constexpr float kSwayLow = 1.6f, kSwayHigh = 3.1f;  // radians a second
// Its brightness: the client draws them full white under a blend that only adds, which on
// Devias's white ground vanishes. Drawn over the ground with alpha here, most of them faint.
constexpr float kFlakeAlpha = 0.85f;

// Devias's blizzard, all of it OURS (the user, 2026-09-30: "snow leaves are more active like in
// storm", "fly faster and on different angle"). At a full storm the wind carries a flake along
// the leaves' own -x at up to nine metres a second -- MU's face-value fall is five to fourteen,
// the leaves' note calls that a blizzard -- in gusts, and it falls three metres a second faster,
// so the snow goes across the screen, not down it. Spawned upwind by the distance the wind
// carries it while it falls, so the stream crosses the hero instead of leaving him in a lee.
constexpr float kStormWind = 9.0f;
constexpr float kStormFall = 3.0f;
constexpr float kStormUpwind = 7.0f;
// How fast a flake takes up the wind's speed: a second's tenth, so a gust sweeps through the
// field and does not arrive in every flake at once.
constexpr float kStormGrip = 10.0f;
// What lands is blown on: gone this many times faster than MoveEtcLeaf's fade.
constexpr float kStormLift = 8.0f;
// How much of its light a flake keeps at a full storm: "snow flakes too much visible" (the
// user, 2026-09-30, three times) at full strength and 450 of them: half and 300, 0.3 and 220,
// then 0.15 and 180 with the streak half as wide -- and "little bit better visible", 0.25.
constexpr float kStormShown = 0.25f;
// Past this speed a flake is drawn as a streak along its flight, this many seconds of it long.
constexpr float kStreakFrom = 2.5f;
constexpr float kStreakSeconds = 0.035f;

// And the wind wanders ("we need some randomness like wind changes", the user, 2026-09-30):
// every six to fourteen seconds a new heading within this far of the leaves' -x and a new
// strength in this range, each turned to at these rates, so the stream swings and swells.
constexpr float kWindSwing = 0.95f;  // radians, about 55 degrees either way
constexpr float kWindWeakest = 0.55f;
constexpr float kWindChangeLow = 6.0f, kWindChangeHigh = 14.0f;
constexpr float kWindTurnRate = 0.35f;  // radians a second
constexpr float kWindSwellRate = 0.25f; // share a second

// The gust, 0.45 to 1.0 of the storm's wind: a five-second swell with a two-second flurry on it,
// both on the leaves' forty-second clock so it never jumps where the clock wraps.
float stormGust(float clock) {
    constexpr float kTau = 6.2831853f;
    return 0.72f + 0.2f * std::sin(clock * kTau / 5.0f) + 0.08f * std::sin(clock * kTau / 2.0f);
}

}  // namespace

float Leaves::random01() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Leaves::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table, bool snow) {
    shutdown();
    snow_ = snow;
    if (snow_) {
        const content::EffectSheet* flake = table.effect("snow");
        const content::EffectSheet* star = table.effect("snow_star");
        if (flake == nullptr || star == nullptr) {
            core::logError("leaves: no cooked effect named 'snow' or 'snow_star'; nothing falls "
                           "on Devias (tools/cook.py --only showing)");
            return false;
        }
        sheet_ = textures.load(assetDir + "/" + flake->path, content::TextureRole::Albedo);
        starSheet_ = textures.load(assetDir + "/" + star->path, content::TextureRole::Albedo);
        core::logf("leaves: %d flakes of snow, sheets %s and %s (%s)", kFlakes,
                   flake->path.c_str(), star->path.c_str(),
                   bgfx::isValid(sheet_) && bgfx::isValid(starSheet_) ? "ready" : "missing");
        return bgfx::isValid(sheet_) && bgfx::isValid(starSheet_);
    }
    const content::EffectSheet* sheet = table.effect("leaf");
    if (sheet == nullptr) {
        core::logError("leaves: no cooked effect named 'leaf'; nothing blows over the town");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    core::logf("leaves: %d on the wind, sheet %s (%s)", kCount, sheet->path.c_str(),
               bgfx::isValid(sheet_) ? "ready" : "missing");
    const content::EffectSheet* rain = table.effect("rain");
    const content::EffectSheet* ring = table.effect("rain_ring");
    if (rain && ring) {
        rainSheet_ = textures.load(assetDir + "/" + rain->path, content::TextureRole::Albedo);
        ringSheet_ = textures.load(assetDir + "/" + ring->path, content::TextureRole::Albedo);
    }
    return bgfx::isValid(sheet_);
}

void Leaves::shutdown() {
    for (Leaf& leaf : leaves_) leaf = Leaf();
    for (Drop& drop : drops_) drop = Drop();
    for (Ring& ring : rings_) ring = Ring();
    sheet_ = rainSheet_ = ringSheet_ = starSheet_ = BGFX_INVALID_HANDLE;
    snow_ = false;
    blowing_ = falling_ = 0;
}

void Leaves::spawnDrop(Drop& drop, const float hero[3], const content::Ground& ground) {
    // The leaves' own field round the player, and higher.
    drop.position[0] = hero[0] + between(-8.0f, 7.99f);
    drop.position[2] = hero[2] - between(-5.0f, 8.99f);
    drop.position[1] = ground.heightAt(hero[0], hero[2]) + between(kDropLow, kDropHigh);
    const float speed = between(kDropSlow, kDropFast);
    const float gust = kGustSwing * std::sin(gust_ * 6.2831853f / kGustSeconds);
    const float slant = between(kSlantLow, kSlantHigh) + gust;
    // A little of it along the other axis too, so no two streaks are quite parallel.
    const float drift = between(-0.05f, 0.05f);
    drop.velocity[0] = -std::sin(slant) * speed;
    drop.velocity[1] = -std::cos(slant) * speed;
    drop.velocity[2] = drift * speed;
    // The faint far outnumber the bright: squared, so most drops are barely there.
    const float catchLight = random01();
    drop.faint = 0.15f + 0.85f * catchLight * catchLight;
    drop.length = between(kDropShortest, kDropLongest);
    drop.live = true;
}

void Leaves::landRing(const float at[3], float faint) {
    Ring& ring = rings_[nextRing_];
    nextRing_ = (nextRing_ + 1) % kRings;
    ring.position[0] = at[0];
    ring.position[1] = at[1] + 0.03f;
    ring.position[2] = at[2];
    ring.scale = between(0.8f, 1.3f);
    ring.life = kRingSeconds;
    ring.faint = faint;
    ring.live = true;
}

void Leaves::update(float seconds, const float hero[3], const float eye[3], bool indoors,
                    const content::Ground& ground, float rain) {
    if (!bgfx::isValid(sheet_)) return;
    const float factor = seconds * kReference;
    uint32_t live = 0;

    // The rain's slots, first: CreateHeavenRain's `index < Rainly`. Under a roof nothing falls
    // on him, and what was falling is taken back at once -- a drop is too quick to fade.
    const bool wet = bgfx::isValid(rainSheet_) && bgfx::isValid(ringSheet_);
    const int drops = wet ? int(rain * float(kDrops)) : 0;
    gust_ = std::fmod(gust_ + seconds, kGustSeconds);
    // The blizzard's wind wandering: a new heading and strength every few seconds, turned to.
    if (snow_ && storm_ > 0.0f) {
        windChangeIn_ -= seconds;
        if (windChangeIn_ <= 0.0f) {
            windChangeIn_ = between(kWindChangeLow, kWindChangeHigh);
            windHeadingTo_ = between(-kWindSwing, kWindSwing);
            windStrengthTo_ = between(kWindWeakest, 1.0f);
        }
        const float turn = kWindTurnRate * seconds, swell = kWindSwellRate * seconds;
        windHeading_ += std::clamp(windHeadingTo_ - windHeading_, -turn, turn);
        windStrength_ += std::clamp(windStrengthTo_ - windStrength_, -swell, swell);
    }
    uint32_t falling = 0;
    for (int i = 0; i < kDrops; ++i) {
        Drop& drop = drops_[i];
        if (!drop.live) {
            if (i < drops && !indoors) spawnDrop(drop, hero, ground);
            else continue;
        } else if (indoors) {
            drop.live = false;
            continue;
        }
        for (int axis = 0; axis < 3; ++axis) drop.position[axis] += drop.velocity[axis] * seconds;
        const float land = ground.heightAt(drop.position[0], drop.position[2]);
        if (drop.position[1] <= land) {
            drop.position[1] = land;
            drop.live = false;
            // Not every drop shows where it lands: MU rings every one, which on this lit ground
            // reads as a carpet of dots. One in three at a full downpour, and more of a thinner
            // rain's -- half of Lorencia's drizzle -- so the ground shows the rain whatever its
            // weight (the user, 2026-09-29: "we can see drops on ground", then "rings visible
            // too much" at every one).
            if (random01() < std::min(0.5f, 0.33f / std::max(rain, 0.33f))) {
                landRing(drop.position, drop.faint);
            }
            continue;
        }
        const float dx = drop.position[0] - hero[0];
        const float dz = drop.position[2] - hero[2];
        if (dx * dx + dz * dz > kStrayDistance * kStrayDistance) {
            drop.live = false;
            continue;
        }
        ++falling;
    }
    for (Ring& ring : rings_) {
        if (!ring.live) continue;
        ring.life -= seconds;
        ring.scale += kRingGrowth * seconds;
        if (ring.life <= 0.0f || indoors) ring.live = false;
    }
    falling_ = falling;

    // And the leaves: none while it rains at all (the user, 2026-09-29: "when there is rain
    // there has to be NOT leaves") -- no new one grows, and one still on the wind goes out as
    // it does indoors. MU's pool shares its slots between the two instead.
    const bool raining = rain > 0.0f;
    // A blizzard fills a larger pool; every slot still moves, so what the storm raised above
    // the calm's count finishes its flight as the storm goes, and is not refilled.
    const int count = snow_ ? kFlakes + int(storm_ * float(kStormFlakes - kFlakes)) : kCount;
    const int slots = snow_ ? kStormFlakes : kCount;
    for (int i = 0; i < slots; ++i) {
        Leaf& leaf = leaves_[i];
        if (!leaf.live) {
            if (i < count && !indoors && !raining) {
                if (snow_) spawnFlake(leaf, hero, ground);
                else spawn(leaf, hero, eye, ground);
            }
        } else if (indoors || raining) {
            // Inside: it is on its way out wherever it happens to be. No wind, no walk, no
            // landing -- just gone, quickly enough that the tavern is still.
            leaf.light -= kIndoorFade * factor;
            if (leaf.light <= 0.0f) {
                leaf.live = false;
                leaf.light = 0.0f;
            }
        } else {
            if (snow_) moveFlake(leaf, seconds, factor, ground);
            else move(leaf, factor, ground);
            // Blown out of the neighbourhood: the slot is worth more back around the player
            // than the leaf is where it has got to.
            const float dx = leaf.position[0] - hero[0];
            const float dy = leaf.position[1] - hero[1];
            const float dz = leaf.position[2] - hero[2];
            if (dx * dx + dy * dy + dz * dz > kStrayDistance * kStrayDistance) leaf.live = false;
        }
        if (leaf.live) ++live;
    }
    blowing_ = live;

}

void Leaves::spawn(Leaf& leaf, const float hero[3], const float eye[3],
                   const content::Ground& ground) {
    // Eight tiles across, five behind the player and nine in front, and between half a tile and
    // three and a half tiles up. Engine z runs against MU's y, so the asymmetric span flips
    // with it.
    leaf.position[0] = hero[0] + between(-8.0f, 7.99f);
    leaf.position[2] = hero[2] - between(-5.0f, 8.99f);
    leaf.position[1] = ground.heightAt(hero[0], hero[2]) + between(0.5f, 3.49f);

    float wind = -between(0.064f, 0.127f);
    // Nearer the camera than the player: the wind turns round and drops to a crawl. This is
    // what makes the square look breezy rather than draughty.
    if (leaf.position[2] > eye[2] - 4.0f) wind = -wind + 0.032f;

    leaf.velocity[0] = wind * kWindScale;
    leaf.velocity[1] = between(-0.016f, 0.015f) * kWindScale;
    leaf.velocity[2] = -between(-0.016f, 0.015f) * kWindScale;
    leaf.light = 1.0f;
    leaf.live = true;
}

void Leaves::spawnFlake(Leaf& flake, const float hero[3], const content::Ground& ground) {
    // The leaves' own field, CreateDeviasSnow's -800..799 by -500..899 units.
    flake.position[0] = hero[0] + between(-8.0f, 7.99f);
    flake.position[2] = hero[2] - between(-5.0f, 8.99f);
    flake.position[1] = ground.heightAt(hero[0], hero[2]) + between(kFlakeLow, kFlakeHigh);
    // In a blizzard, upwind of where the wind now blows from, falling faster, already on it.
    const float windX = -std::cos(windHeading_), windZ = std::sin(windHeading_);
    flake.position[0] -= windX * storm_ * kStormUpwind;
    flake.position[2] -= windZ * storm_ * kStormUpwind;
    const float fall = between(kFlakeSlow, kFlakeFast) + storm_ * between(0.5f, 1.0f) * kStormFall;
    const float blow = storm_ * kStormWind * windStrength_ * stormGust(gust_);
    flake.velocity[0] = windX * blow;
    flake.velocity[1] = -fall;
    flake.velocity[2] = fall * kFlakeSlant + windZ * blow;
    // rand_fps_check(10): one in ten a glint.
    flake.star = random01() < 0.1f;
    // Squared, as the rain's is: most flakes are barely there, a few catch the light.
    const float catchLight = random01();
    flake.faint = 0.3f + 0.7f * catchLight * catchLight;
    flake.phase = between(0.0f, 6.2831853f);
    flake.light = 1.0f;
    flake.live = true;
}

void Leaves::moveFlake(Leaf& flake, float seconds, float factor, const content::Ground& ground) {
    const float land = ground.heightAt(flake.position[0], flake.position[2]);
    if (flake.position[1] <= land) {
        // Lying where it fell and going out, MoveEtcLeaf's twentieth a frame.
        flake.position[1] = land;
        flake.light -= kFadeRate * factor * (1.0f + storm_ * kStormLift);
        if (flake.light <= 0.0f) {
            flake.live = false;
            flake.light = 0.0f;
        }
        return;
    }
    // Its sway: each flake its own rate, off its phase, so no two keep time.
    const float rate = kSwayLow + (kSwayHigh - kSwayLow) * std::fmod(flake.phase * 0.61803f, 1.0f);
    const float before = std::sin(flake.phase);
    flake.phase += rate * seconds;
    const float across = (std::sin(flake.phase) - before) * kSwayMetres;
    // And MoveEtcLeaf's random walk on top, at the leaves' tenth, on the level only: a flake
    // that wanders up as well is a flake that never lands.
    for (int axis : {0, 2}) {
        flake.velocity[axis] += between(-0.008f, 0.007f) * factor * kWindScale * 6.0f;
    }
    // The blizzard's wind, taken up over a tenth of a second, each flake at its own share of the
    // gust (off its phase) so the stream shears instead of moving as one sheet.
    // Along the wind's heading as it wanders, at its strength of the moment.
    const float blow = storm_ * kStormWind * windStrength_ * stormGust(gust_ + flake.phase * 0.3f);
    const float windX = -std::cos(windHeading_) * blow;
    const float windZ = std::sin(windHeading_) * blow - flake.velocity[1] * kFlakeSlant;
    // Scaled by the storm, so the calm's random walk is left exactly as it was.
    const float grip = std::min(1.0f, seconds * kStormGrip * storm_);
    flake.velocity[0] += (windX - flake.velocity[0]) * grip;
    flake.velocity[2] += (windZ - flake.velocity[2]) * grip;
    flake.velocity[0] = std::clamp(flake.velocity[0], std::min(windX, 0.0f) - 0.35f,
                                   std::max(windX, 0.0f) + 0.35f);
    flake.position[0] += flake.velocity[0] * seconds + across;
    flake.position[1] += flake.velocity[1] * seconds;
    flake.position[2] += flake.velocity[2] * seconds + across * 0.4f;
}

void Leaves::move(Leaf& leaf, float factor, const content::Ground& ground) {
    const float land = ground.heightAt(leaf.position[0], leaf.position[2]);
    if (leaf.position[1] <= land) {
        // Down, and staying down. The client keeps it exactly on the ground and takes its light
        // away a twentieth at a time.
        leaf.position[1] = land;
        leaf.light -= kFadeRate * factor;
        if (leaf.light <= 0.0f) {
            leaf.live = false;
            leaf.light = 0.0f;
        }
        return;
    }
    // A random walk on every axis, which is the whole of the turbulence: no gravity, no drag,
    // just a nudge each frame that never quite averages out.
    for (int axis = 0; axis < 3; ++axis) {
        leaf.velocity[axis] += between(-0.008f, 0.007f) * factor * kWindScale;
        leaf.position[axis] += leaf.velocity[axis] * factor;
    }
}

void Leaves::gather(gfx::Effects& effects, const float eye[3]) const {
    if (!bgfx::isValid(sheet_)) return;
    for (const Drop& drop : drops_) {
        if (!drop.live) continue;
        // Along the fall and across the line to the eye, so the streak is never seen edge on.
        float along[3] = {drop.velocity[0], drop.velocity[1], drop.velocity[2]};
        const float speed = std::sqrt(along[0] * along[0] + along[1] * along[1] +
                                      along[2] * along[2]);
        if (speed <= 0.0f) continue;
        for (float& a : along) a /= speed;
        const float look[3] = {eye[0] - drop.position[0], eye[1] - drop.position[1],
                               eye[2] - drop.position[2]};
        float side[3] = {along[1] * look[2] - along[2] * look[1],
                         along[2] * look[0] - along[0] * look[2],
                         along[0] * look[1] - along[1] * look[0]};
        const float across = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (across <= 0.0f) continue;
        for (float& s : side) s *= kDropHalfWidth / across;
        gfx::Sprite sprite;
        sprite.placed = true;
        sprite.sheet = rainSheet_;
        sprite.blend = gfx::Blend::Alpha;
        for (int a = 0; a < 3; ++a) {
            sprite.position[a] = drop.position[a];
            const float low = drop.position[a] + along[a] * drop.length;   // ahead: lower
            const float high = drop.position[a] - along[a] * drop.length;
            sprite.corner[0][a] = low - side[a];
            sprite.corner[1][a] = low + side[a];
            sprite.corner[2][a] = high + side[a];
            sprite.corner[3][a] = high - side[a];
        }
        const float uv[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
        for (int c = 0; c < 4; ++c) {
            sprite.cornerUv[c][0] = uv[c][0];
            sprite.cornerUv[c][1] = uv[c][1];
        }
        for (int c = 0; c < 3; ++c) sprite.colour[c] = kDropColour[c];
        sprite.colour[3] = kDropAlpha * drop.faint;
        effects.add(sprite);
    }
    for (const Ring& ring : rings_) {
        if (!ring.live) continue;
        // Laid flat on the ground, not stood up to the eye: the sheet's ellipse stretched to a
        // square is a circle again, and the camera's own pitch squashes it as a puddle's ring.
        gfx::Sprite sprite;
        sprite.placed = true;
        for (int a = 0; a < 3; ++a) sprite.position[a] = ring.position[a];
        const float half = kRingHalfWidth * ring.scale;
        const float offset[4][2] = {{-half, half}, {half, half}, {half, -half}, {-half, -half}};
        const float uv[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
        for (int c = 0; c < 4; ++c) {
            sprite.corner[c][0] = ring.position[0] + offset[c][0];
            sprite.corner[c][1] = ring.position[1];
            sprite.corner[c][2] = ring.position[2] + offset[c][1];
            sprite.cornerUv[c][0] = uv[c][0];
            sprite.cornerUv[c][1] = uv[c][1];
        }
        sprite.sheet = ringSheet_;
        sprite.blend = gfx::Blend::Alpha;
        sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = kRingLift;
        sprite.colour[3] =
            kRingAlpha * ring.faint * std::clamp(ring.life / kRingSeconds, 0.0f, 1.0f);
        effects.add(sprite);
    }
    if (snow_) {
        for (int i = 0; i < kStormFlakes; ++i) {
            const Leaf& flake = leaves_[i];
            if (!flake.live || flake.light <= 0.0f) continue;
            gfx::Sprite sprite;
            for (int a = 0; a < 3; ++a) sprite.position[a] = flake.position[a];
            sprite.halfWidth = sprite.halfHeight = flake.star ? kStarHalf : kFlakeHalf;
            sprite.sheet = flake.star ? starSheet_ : sheet_;
            sprite.blend = gfx::Blend::Alpha;
            sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = 1.0f;
            sprite.colour[3] = kFlakeAlpha * flake.faint * flake.light *
                               (1.0f - (1.0f - kStormShown) * storm_);
            // A blizzard's flake is a streak along its flight, as a drop is, facing the eye.
            const float* v = flake.velocity;
            const float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            const float look[3] = {eye[0] - flake.position[0], eye[1] - flake.position[1],
                                   eye[2] - flake.position[2]};
            float side[3] = {v[1] * look[2] - v[2] * look[1], v[2] * look[0] - v[0] * look[2],
                             v[0] * look[1] - v[1] * look[0]};
            const float across = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
            if (!flake.star && speed > kStreakFrom && across > 0.0f) {
                const float half = speed * kStreakSeconds * 0.5f;
                for (float& s : side) s *= kFlakeHalf * 0.5f / across;
                sprite.placed = true;
                for (int a = 0; a < 3; ++a) {
                    const float ahead = flake.position[a] + v[a] / speed * half;
                    const float behind = flake.position[a] - v[a] / speed * half;
                    sprite.corner[0][a] = ahead - side[a];
                    sprite.corner[1][a] = ahead + side[a];
                    sprite.corner[2][a] = behind + side[a];
                    sprite.corner[3][a] = behind - side[a];
                }
                const float uv[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
                for (int c = 0; c < 4; ++c) {
                    sprite.cornerUv[c][0] = uv[c][0];
                    sprite.cornerUv[c][1] = uv[c][1];
                }
            }
            effects.add(sprite);
        }
        return;
    }
    for (const Leaf& leaf : leaves_) {
        if (!leaf.live || leaf.light <= 0.0f) continue;
        gfx::Sprite sprite;
        sprite.position[0] = leaf.position[0];
        sprite.position[1] = leaf.position[1];
        sprite.position[2] = leaf.position[2];
        sprite.halfWidth = kHalfWidth;
        sprite.halfHeight = kHalfHeight;
        sprite.u0 = kU0;
        sprite.u1 = kU1;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Alpha;
        // Dimmed as well as faded: the landing fade runs from two thirds down to black, and the
        // alpha carries the same number so a leaf going out thins as well as darkens. MU2 drew
        // these alpha-scissored and unlit against a hard 0.15 threshold, because a MultiMesh
        // had no sorted pass to go in; here they belong in the one this engine already sorts.
        const float shown = leaf.light * kFaintness;
        sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = shown;
        sprite.colour[3] = leaf.light;
        effects.add(sprite);
    }
}

}  // namespace mu::game
