#include "game/world/leaves.h"

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

}  // namespace

float Leaves::random01() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Leaves::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table) {
    shutdown();
    const content::EffectSheet* sheet = table.effect("leaf");
    if (sheet == nullptr) {
        core::logError("leaves: no cooked effect named 'leaf'; nothing blows over the town");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    core::logf("leaves: %d on the wind, sheet %s (%s)", kCount, sheet->path.c_str(),
               bgfx::isValid(sheet_) ? "ready" : "missing");
    return bgfx::isValid(sheet_);
}

void Leaves::shutdown() {
    for (Leaf& leaf : leaves_) leaf = Leaf();
    sheet_ = BGFX_INVALID_HANDLE;
    blowing_ = 0;
}

void Leaves::update(float seconds, const float hero[3], const float eye[3], bool indoors,
                    const content::Ground& ground) {
    if (!bgfx::isValid(sheet_)) return;
    const float factor = seconds * kReference;
    uint32_t live = 0;

    for (Leaf& leaf : leaves_) {
        if (!leaf.live) {
            if (!indoors) spawn(leaf, hero, eye, ground);
        } else if (indoors) {
            // Inside: it is on its way out wherever it happens to be. No wind, no walk, no
            // landing -- just gone, quickly enough that the tavern is still.
            leaf.light -= kIndoorFade * factor;
            if (leaf.light <= 0.0f) {
                leaf.live = false;
                leaf.light = 0.0f;
            }
        } else {
            move(leaf, factor, ground);
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

void Leaves::gather(gfx::Effects& effects) const {
    if (!bgfx::isValid(sheet_)) return;
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
