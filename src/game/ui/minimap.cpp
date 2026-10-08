#include "game/ui/minimap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "content/grid.h"
#include "content/ground.h"
#include "core/log.h"
#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/gates.h"
#include "sim/market.h"
#include "sim/quests.h"
#include "sim/vault.h"
#include "sim/wear.h"

namespace mu::game {

namespace {

using gfx::Box;

// ---- measures, in tip::unit() ------------------------------------------------------------------

constexpr float kInset = 12.0f;  // from the screen's top to the circle's box
// Top right, over the quest tracker (the user, 2026-09-29), the drawn disc's right edge on the
// tracker's own: tracker.cpp's kRight, 44 in from the screen's edge.
constexpr float kRight = 44.0f;
constexpr float kSide = 216.0f;  // the circle, across
// The rim fades out over this, from the opaque disc inside it to nothing at the edge: the user's
// "fadeout sides from all sides and it inside circle" (2026-09-29). No frame round it.
constexpr float kFade = 38.0f;
constexpr int kSegments = 72;    // round the circle
constexpr float kChartAlpha = 1.0f;  // the chart carries its own alpha: lines strong, fills faint
// And under it a scrim of the void, fading the same way, so bright ground behind it (Noria's
// grass) is calmed and the chart and the marks still read; the tracker's own "scrim felt rather
// than seen".
constexpr float kScrimAlpha = 0.5f;
constexpr float kReach = 9.0f;   // how near the pointer has to be to name a mark
// The coordinates' size, under the disc: 12 inside the fade was too small to read (the user,
// 2026-09-30, "coordinates has to be little bit bigger and under the map").
constexpr float kFigure = 15.0f;
// A clean rim, the user's "some clean border around map with some transparency" (2026-09-30):
// the chart stops on a ring half way into the old fade, a bone line over a dark hairline, and
// only the scrim outside it keeps fading, as the ring's soft shadow on the world.
constexpr float kRimIn = 0.5f;      // of kFade, where the ring sits inside the drawn radius
constexpr float kRimLine = 2.0f;    // its width
constexpr float kRimAlpha = 0.55f;
constexpr float kRimShadow = 0.5f;  // the dark hairline outside it
// Tiles across the square, near to far; the wheel steps between them.
constexpr float kSpans[] = {40.0f, 64.0f, 100.0f};
constexpr int kZooms = int(sizeof kSpans / sizeof kSpans[0]);
constexpr int kMapTiles = 256;
// How fast the arrow comes round to his facing: most of the way in a tenth of a second.
constexpr float kTurnRate = 14.0f;
// The whole map, M's: the square it is fitted in, as a share of the screen's shorter side, and
// fitted at the worst turn the camera can give it, a diagonal, so it never grows or shrinks as
// the camera turns. The world behind it darkened, the menu's way.
constexpr float kFullShare = 0.7f;
// Its middle a little over the screen's, so its lowest corner clears the action bar.
constexpr float kFullMiddle = 0.47f;
constexpr float kFullFit = 1.41421356f;
constexpr float kFullScrim = 0.5f;
constexpr float kFullTitle = 22.0f;  // the map's name over it

// ---- the chart -----------------------------------------------------------------------------------
//
// The land drawn as a cartographer would, off the map's own attribute grid and its floor: where
// he can walk is one flat ash tone, the town a step lighter, and every edge of it -- a shore, a
// wall, a house -- a hairline of iron. What he cannot walk on is a tone by what it is: water, a
// quiet cool grey, and anything else (cliff, house, thicket) a step below the land. No picture and
// no colour: the user's "flat without colors ... some cartograph", and then "a lot of black areas"
// (2026-09-29), which the first chart had, leaving everything unwalkable as the bare sheet.
//
// Four texels a tile. The grid is softened by a tent over its neighbours before it is read, so a
// lone blocked tile is a notch and not a hole, and the edges come out as lines rather than stairs.
constexpr int kChartTexels = 4;
// Three steps of one warm grey over the sheet, far enough apart to read at a glance: the first
// pass sat within a few levels of the sheet and the town read as the darkest thing on it.
// Drawn in lines, the user's "made from lines without super visible fills" (2026-09-29): every
// edge of where he can walk -- a shore, a wall, a house -- is a bone hairline, and the fills under
// them are breaths, just enough to tell the town from the field and the water from the rest.
// Colour and strength each; the strength is the texel's alpha, so the world shows through.
constexpr float kLine[3] = {0.788f, 0.749f, 0.682f};  // style's kBone2
constexpr float kLineAlpha = 0.8f;
constexpr float kShoreTexels = 0.75f;                  // the hairline's half-width
// A soft dark edge either side of the line, as a printed map haloes its lines, so a pale line
// still reads over Noria's bright grass.
constexpr float kHaloTexels = 2.6f;
constexpr float kHaloAlpha = 0.45f;
constexpr float kFillInk[3] = {0.851f, 0.812f, 0.741f}; // style's kBone, for the land's breath
constexpr float kLandAlpha = 0.05f;
constexpr float kTownAlpha = 0.12f;
constexpr float kWaterInk[3] = {0.020f, 0.024f, 0.030f}; // water is a faint shade, not a light
constexpr float kWaterAlpha = 0.22f;

// ---- the glyphs ----------------------------------------------------------------------------------
//
// One cell each, 20 units square, drawn about (10, 10), baked white at the interface unit -- a
// signed distance a sample, sixteen samples a texel, as Beacon::bake does -- and tinted when
// drawn, so each mark is one flat tone.

constexpr float kCell = 20.0f;
constexpr float kMid = kCell * 0.5f;
constexpr int kGlyphs = 12;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

float circle(float x, float y, float cx, float cy, float r) {
    return std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r;
}

// A box about (cx, cy) with half-sizes (hx, hy) and its corners rounded by `r`.
float box(float x, float y, float cx, float cy, float hx, float hy, float r = 0.0f) {
    const float qx = std::fabs(x - cx) - hx + r, qy = std::fabs(y - cy) - hy + r;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

// A stroke from (ax, ay) to (bx, by), `r` either side.
float capsule(float x, float y, float ax, float ay, float bx, float by, float r) {
    const float px = x - ax, py = y - ay, ex = bx - ax, ey = by - ay;
    const float t = clamp01((px * ex + py * ey) / (ex * ex + ey * ey));
    const float dx = px - ex * t, dy = py - ey * t;
    return std::sqrt(dx * dx + dy * dy) - r;
}

// Inigo Quilez's polygon distance, as the beacon has it: negative inside, any simple polygon.
template <int N>
float polygon(float px, float py, const float (&v)[N][2]) {
    float dx = px - v[0][0], dy = py - v[0][1];
    float d = dx * dx + dy * dy;
    float s = 1.0f;
    for (int i = 0, j = N - 1; i < N; j = i, ++i) {
        const float ex = v[j][0] - v[i][0], ey = v[j][1] - v[i][1];
        const float wx = px - v[i][0], wy = py - v[i][1];
        const float t = clamp01((wx * ex + wy * ey) / (ex * ex + ey * ey));
        const float bx = wx - ex * t, by = wy - ey * t;
        d = std::min(d, bx * bx + by * by);
        const bool c0 = py >= v[i][1], c1 = py < v[j][1], c2 = ex * wy > ey * wx;
        if ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) s = -s;
    }
    return s * std::sqrt(d);
}

// Him: a full arrowhead pointing up the cell, broad at the shoulders and barely notched, so the
// weight is toward the point. The first two, slimmer with a deeper notch, measured the right way
// round and still read backwards at eleven pixels: the wings outweighed the point.
constexpr float kArrow[4][2] = {{kMid, 2.2f}, {kMid + 6.0f, 16.8f}, {kMid, 14.6f}, {kMid - 6.0f, 16.8f}};
// The smith's anvil: the face with its horn to the left, the waist, the foot.
constexpr float kAnvil[12][2] = {
    {5.4f, 7.4f},  {14.6f, 7.4f}, {14.6f, 9.1f},  {12.5f, 9.7f}, {11.7f, 11.4f}, {13.6f, 12.9f},
    {13.6f, 13.9f}, {6.4f, 13.9f}, {6.4f, 12.9f},  {8.3f, 11.4f}, {7.5f, 9.7f},   {6.4f, 9.0f},
};
// The merchant's pouch: its neck, over a round body.
constexpr float kNeck[4][2] = {{8.4f, 6.2f}, {11.6f, 6.2f}, {10.9f, 8.4f}, {9.1f, 8.4f}};

// The hand-in's "?": the beacon's hook, at this size.
float hook(float x, float y) {
    constexpr float cy = 7.6f, r = 2.7f, half = 1.05f, stemEnd = 11.2f;
    constexpr float gapFrom = 1.5708f, gapTo = 3.7525f;  // the opening, 90 to 215 degrees, y down
    const float dx = x - kMid, dy = y - cy;
    float a = std::atan2(dy, dx);
    if (a < 0.0f) a += 6.2831853f;
    float d;
    if (a > gapFrom && a < gapTo) {
        const auto end = [&](float at) {
            return circle(x, y, kMid + r * std::cos(at), cy + r * std::sin(at), 0.0f);
        };
        d = std::min(end(gapFrom), end(gapTo)) - half;
    } else {
        d = std::fabs(std::sqrt(dx * dx + dy * dy) - r) - half;
    }
    return std::min(d, capsule(x, y, kMid, cy + r, kMid, stemEnd, half));
}

// A glyph's distance at (x, y) in the cell's units: negative inside.
float shape(Minimap::Glyph glyph, float x, float y) {
    using Glyph = Minimap::Glyph;
    switch (glyph) {
        case Glyph::Hero: return polygon(x, y, kArrow);
        case Glyph::Quarry: return circle(x, y, kMid, kMid, 2.7f);
        // Her summon: a dot a size up from a quarry's, rimmed as he is.
        case Glyph::Summon: return circle(x, y, kMid, kMid, 3.4f);
        // Another player: the summon's dot, a size up again.
        case Glyph::Player: return circle(x, y, kMid, kMid, 3.8f);
        // Anyone else worth a name: a head over shoulders, the townsperson himself.
        case Glyph::Folk:
            return std::min(circle(x, y, kMid, 6.6f, 2.4f),
                            std::max({circle(x, y, kMid, 15.8f, 5.2f), y - 14.8f,
                                      -circle(x, y, kMid, 6.6f, 3.4f)}));
        // What the potion girl and the barmaids pour: a round flask with its neck and stopper.
        case Glyph::Potion:
            return std::min({circle(x, y, kMid, 12.1f, 3.5f), box(x, y, kMid, 7.6f, 1.1f, 1.6f),
                             box(x, y, kMid, 5.8f, 1.8f, 0.6f, 0.3f)});
        case Glyph::Offer:
            return std::min(capsule(x, y, kMid, 4.6f, kMid, 10.6f, 1.25f), circle(x, y, kMid, 14.4f, 1.4f));
        case Glyph::HandIn: return std::min(hook(x, y), circle(x, y, kMid, 14.4f, 1.35f));
        case Glyph::Vendor: {
            const float pouch = std::min(circle(x, y, kMid, 11.4f, 3.3f), polygon(x, y, kNeck));
            // The cord round the neck, cut out of it.
            return std::max(pouch, -box(x, y, kMid, 8.2f, 2.4f, 0.35f));
        }
        case Glyph::Smith: return polygon(x, y, kAnvil);
        case Glyph::Vault: {
            const float chest = box(x, y, kMid, 10.5f, 4.3f, 3.2f, 0.7f);
            // The lid's line cut across it, broken by the clasp.
            const float lid = std::max(box(x, y, kMid, 9.4f, 4.4f, 0.38f), -box(x, y, kMid, 9.7f, 1.2f, 1.4f));
            const float clasp = box(x, y, kMid, 9.7f, 0.8f, 1.0f, 0.2f);
            return std::min(std::max(chest, -lid), clasp);
        }
        case Glyph::Gate: {
            // An arch outlined: a round head over square jambs, open at the foot.
            const float outer = std::min(box(x, y, kMid, 11.9f, 4.0f, 2.6f), circle(x, y, kMid, 9.3f, 4.0f));
            const float inner = std::min(box(x, y, kMid, 12.4f, 2.6f, 3.0f), circle(x, y, kMid, 9.3f, 2.6f));
            return std::max(outer, -inner);
        }
    }
    return 1e9f;
}

// The townsfolk wear a badge, the user's "better icons for NPCs" (2026-09-30): a dark disc with a
// bone ring, the trade's own sign inside it shrunk to fit. The disc is what lets a sign read on
// the lit town, and the ring is what says "a person to talk to" before the sign is read.
bool badged(Minimap::Glyph glyph) {
    using Glyph = Minimap::Glyph;
    return glyph == Glyph::Vendor || glyph == Glyph::Smith || glyph == Glyph::Vault ||
           glyph == Glyph::Potion || glyph == Glyph::Folk;
}
constexpr float kBadge = 9.4f;       // the disc's radius, in the cell
constexpr float kBadgeRing = 0.6f;   // the ring's half-width, just inside the disc's edge
constexpr float kBadgeSign = 0.72f;  // the sign's scale inside it
constexpr float kBadgeDark = 0.82f;  // the disc's own alpha

constexpr uint32_t kHandInGold = gfx::rgba(0.86f, 0.64f, 0.12f);  // "little bit darker"
// A repeat's offer and hand-in: the beacon's blue (game/ui/beacon.cpp kLitAgain), so the map's
// mark reads as the "!" over his head (the user, 2026-10-02).
constexpr uint32_t kAgainBlue = gfx::rgba(0.56f, 0.80f, 1.00f);
// Him in green (the user, 2026-10-02: "character arrow green"), bright enough over the lit ground.
constexpr uint32_t kHeroGreen = gfx::rgba(0.42f, 0.86f, 0.36f);
// Her summon, his green paled toward bone, so it reads as his and not as him.
constexpr uint32_t kSummonGreen = gfx::rgba(0.66f, 0.90f, 0.60f);
// Another player -- for now the bots -- in orange (the user, 2026-10-08).
constexpr uint32_t kPlayerOrange = gfx::rgba(1.00f, 0.58f, 0.12f);

// The tone each is drawn in: bone for what matters, the quieter inks for the rest.
uint32_t toneOf(Minimap::Glyph glyph) {
    using Glyph = Minimap::Glyph;
    switch (glyph) {
        case Glyph::Hero: return kHeroGreen;
        case Glyph::Summon: return kSummonGreen;
        case Glyph::Player: return kPlayerOrange;
        // A quest ready to hand in is gold, the user's (2026-10-01): "lets color finished quests
        // more vissible gold color". Bone sank into the lit ground. A quest not yet taken wears
        // the same gold (the user, 2026-10-02); the sign, "!" or hook, tells them apart.
        case Glyph::Offer:
        case Glyph::HandIn: return kHandInGold;
        // The one colour on the map, Sanctuary's one accent: what the quest wants killed (the
        // user, 2026-09-29, "quest monsters as red circles").
        case Glyph::Quarry: return style::kBloodHi;
        case Glyph::Gate:
        case Glyph::Vendor:
        case Glyph::Smith:
        case Glyph::Vault:
        case Glyph::Potion: return style::kBone2;
        case Glyph::Folk: return style::kAshInk2;
        default: return style::kAshInk;
    }
}

// The Dungeon's floor an exit gate lets out on (Gates.cs:119-125, docs/dungeon-port.md): its
// three floors are regions of one map, and the stairs between them are named by where they go.
const char* floorName(int32_t exitGate) {
    switch (exitGate) {
        case 2: case 8: return "Dungeon 1";
        case 6: case 12: case 16: return "Dungeon 2";
        case 10: case 14: return "Dungeon 3";
        // The Lost Tower's (Gates.cs:138-145): every stair goes down, so each floor has one.
        case 29: case 42: return "Lost Tower 1";
        case 31: return "Lost Tower 2";
        case 33: return "Lost Tower 3";
        case 35: return "Lost Tower 4";
        case 37: return "Lost Tower 5";
        case 39: return "Lost Tower 6";
        case 41: return "Lost Tower 7";
        default: return "another floor";
    }
}

const char* mapName(uint32_t map) {
    switch (map) {
        case 0: return "Lorencia";
        case 1: return "Dungeon";
        case 2: return "Devias";
        case 3: return "Noria";
        case 4: return "Lost Tower";
        case 7: return "Atlans";
        case 8: return "Tarkan";
        case 10: return "Icarus";
        case 11: return "Blood Castle";
        default: return "another map";
    }
}

}  // namespace

void Minimap::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

void Minimap::close() {
    if (bgfx::isValid(texture_)) bgfx::destroy(texture_);
    texture_ = BGFX_INVALID_HANDLE;
    glyphs_ = {};
    bakedUnit_ = 0.0f;
    if (bgfx::isValid(chartTexture_)) bgfx::destroy(chartTexture_);
    chartTexture_ = BGFX_INVALID_HANDLE;
    chart_ = {};
    chartMap_ = -1;
    built_ = false;
    dismiss();
}

bool Minimap::bake(float unit) {
    if (bgfx::isValid(texture_)) bgfx::destroy(texture_);
    texture_ = BGFX_INVALID_HANDLE;
    glyphs_ = {};
    cell_ = int(std::ceil(kCell * unit));
    const int wide = cell_ * kGlyphs;
    std::vector<uint8_t> rgba(size_t(wide) * size_t(cell_) * 4, 0xFF);
    const float texel = 1.0f / unit;
    constexpr int kSamples = 4;  // a side
    for (int py = 0; py < cell_; ++py) {
        for (int column = 0; column < wide; ++column) {
            const auto glyph = Glyph(column / cell_);
            const int px = column % cell_;
            // White where the glyph is, so the tint is its tone; him with a hairline of ink round
            // him too, which the tint leaves black, so he holds his shape on the lit town.
            const bool rimmed = glyph == Glyph::Hero || glyph == Glyph::Summon || glyph == Glyph::Player;
            float fill = 0.0f, alpha = 0.0f;
            for (int sy = 0; sy < kSamples; ++sy) {
                for (int sx = 0; sx < kSamples; ++sx) {
                    const float x = (float(px) + (float(sx) + 0.5f) / kSamples) * texel;
                    const float y = (float(py) + (float(sy) + 0.5f) / kSamples) * texel;
                    if (badged(glyph)) {
                        // The disc dark (white left out, so the tint does not reach it), the
                        // ring and the sign white over it.
                        const float disc = circle(x, y, kMid, kMid, kBadge);
                        const float ring = std::fabs(disc + kBadgeRing * 1.6f) - kBadgeRing;
                        const float sign = shape(glyph, kMid + (x - kMid) / kBadgeSign,
                                                 kMid + (y - kMid) / kBadgeSign) * kBadgeSign;
                        const float in = clamp01(0.5f - disc / texel);
                        const float lit = std::min(in, clamp01(0.5f - std::min(ring, sign) / texel));
                        fill += lit;
                        alpha += lit + kBadgeDark * (in - lit);
                        continue;
                    }
                    const float d = shape(glyph, x, y);
                    const float inside = clamp01(0.5f - d / texel);
                    const float rim = rimmed ? clamp01(0.5f - (d - 0.9f) / texel) * 0.85f : 0.0f;
                    fill += inside;
                    alpha += inside + rim * (1.0f - inside);
                }
            }
            constexpr float kShare = 1.0f / float(kSamples * kSamples);
            uint8_t* out = &rgba[(size_t(py) * size_t(wide) + size_t(column)) * 4];
            const float white = alpha > 0.0f ? fill / alpha : 1.0f;
            out[0] = out[1] = out[2] = uint8_t(clamp01(white) * 255.0f + 0.5f);
            out[3] = uint8_t(clamp01(alpha * kShare) * 255.0f + 0.5f);
        }
    }
    texture_ = bgfx::createTexture2D(uint16_t(wide), uint16_t(cell_), false, 1,
                                     bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                     bgfx::copy(rgba.data(), uint32_t(rgba.size())));
    if (!bgfx::isValid(texture_)) return false;
    bgfx::setName(texture_, "minimap glyphs");
    glyphs_ = {texture_, float(wide), float(cell_)};
    bakedUnit_ = unit;
    built_ = false;
    return true;
}

bool Minimap::bakeChart(const content::Tables& tables, const content::Ground* ground) {
    if (bgfx::isValid(chartTexture_)) bgfx::destroy(chartTexture_);
    chartTexture_ = BGFX_INVALID_HANDLE;
    chart_ = {};
    chartMap_ = int(tables.map);
    chartGround_ = ground;
    const auto began = std::chrono::steady_clock::now();
    const content::Grid& grid = tables.grid;
    const int tiles = grid.size();
    if (tiles <= 0) return false;
    // Land and town a tile, then the tent over each one's neighbours.
    std::vector<float> land(size_t(tiles) * size_t(tiles)), town(land.size()), water(land.size());
    // Water is the floor MU paints it with, by the slot's name, as the grass finds its own.
    std::vector<int8_t> wet;
    const auto isWater = [&](int column, int row) {
        if (!ground) return false;
        const int slot = ground->floorAt(column, row);
        if (slot < 0) return false;
        if (size_t(slot) >= wet.size()) wet.resize(size_t(slot) + 1, -1);
        if (wet[size_t(slot)] < 0) wet[size_t(slot)] = ground->floorName(slot).find("Water") != std::string::npos;
        return wet[size_t(slot)] == 1;
    };
    for (int row = 0; row < tiles; ++row) {
        for (int column = 0; column < tiles; ++column) {
            const uint16_t word = grid.at(column, row);
            const bool open = (word & (content::kNoMove | content::kNoGround)) == 0;
            land[size_t(row) * size_t(tiles) + size_t(column)] = open ? 1.0f : 0.0f;
            town[size_t(row) * size_t(tiles) + size_t(column)] = open && grid.safe(column, row) ? 1.0f : 0.0f;
            water[size_t(row) * size_t(tiles) + size_t(column)] = !open && isWater(column, row) ? 1.0f : 0.0f;
        }
    }
    const auto soften = [&](const std::vector<float>& in) {
        std::vector<float> out(in.size());
        for (int row = 0; row < tiles; ++row) {
            for (int column = 0; column < tiles; ++column) {
                float sum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int c = std::clamp(column + dx, 0, tiles - 1), r = std::clamp(row + dy, 0, tiles - 1);
                        sum += in[size_t(r) * size_t(tiles) + size_t(c)] * float((2 - std::abs(dx)) * (2 - std::abs(dy)));
                    }
                }
                out[size_t(row) * size_t(tiles) + size_t(column)] = sum / 16.0f;
            }
        }
        return out;
    };
    land = soften(land);
    town = soften(town);
    water = soften(water);
    // Read between tile centres, a tile's centre being its integer coordinate.
    const auto read = [&](const std::vector<float>& field, float fx, float fy) {
        fx = std::clamp(fx, 0.0f, float(tiles - 1));
        fy = std::clamp(fy, 0.0f, float(tiles - 1));
        const int x0 = int(fx), y0 = int(fy);
        const int x1 = std::min(x0 + 1, tiles - 1), y1 = std::min(y0 + 1, tiles - 1);
        const float tx = fx - float(x0), ty = fy - float(y0);
        const auto at = [&](int x, int y) { return field[size_t(y) * size_t(tiles) + size_t(x)]; };
        return (at(x0, y0) * (1.0f - tx) + at(x1, y0) * tx) * (1.0f - ty) +
               (at(x0, y1) * (1.0f - tx) + at(x1, y1) * tx) * ty;
    };
    const int size = tiles * kChartTexels;
    // The softened field climbs 0 to 1 over about two tiles, eight texels: that is what turns
    // its value into a distance from the edge in texels.
    constexpr float kTexelsPerUnit = 2.0f * float(kChartTexels);
    std::vector<uint8_t> rgba(size_t(size) * size_t(size) * 4, 0);
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const float fx = (float(px) + 0.5f) / float(kChartTexels) - 0.5f;
            const float fy = (float(py) + 0.5f) / float(kChartTexels) - 0.5f;
            const float d = (read(land, fx, fy) - 0.5f) * kTexelsPerUnit;
            const float cover = clamp01(d + 0.5f);
            const float inTown = clamp01((read(town, fx, fy) - 0.5f) * kTexelsPerUnit + 0.5f);
            const float shore = clamp01(kShoreTexels + 0.5f - std::fabs(d));
            const float wetness = clamp01(read(water, fx, fy) * 1.6f);
            uint8_t* out = &rgba[(size_t(py) * size_t(size) + size_t(px)) * 4];
            // The fill: the land's breath, more of it in town, or the water's shade; then the line
            // over it. Straight alpha, laid one over the other.
            const float fillAlpha = (kLandAlpha + (kTownAlpha - kLandAlpha) * inTown) * cover +
                                    kWaterAlpha * wetness * (1.0f - cover);
            const float lineAlpha = kLineAlpha * shore;
            const float halo = clamp01(1.0f - std::fabs(d) / kHaloTexels);
            const float haloAlpha = kHaloAlpha * halo * halo;
            // Premultiplied while they stack: the fill, the halo over it, the line over both.
            float pre[3] = {0.0f, 0.0f, 0.0f};
            float alpha = 0.0f;
            const auto over = [&](const float* ink, float a) {
                for (int k = 0; k < 3; ++k) pre[k] = ink[k] * a + pre[k] * (1.0f - a);
                alpha = a + alpha * (1.0f - a);
            };
            static constexpr float kBlack[3] = {0.0f, 0.0f, 0.0f};
            over(cover > 0.5f ? kFillInk : kWaterInk, fillAlpha);
            over(kBlack, haloAlpha);
            over(kLine, lineAlpha);
            for (int k = 0; k < 3; ++k) {
                out[k] = uint8_t(clamp01(alpha > 0.0f ? pre[k] / alpha : 0.0f) * 255.0f + 0.5f);
            }
            out[3] = uint8_t(clamp01(alpha) * 255.0f + 0.5f);
        }
    }
    chartTexture_ = bgfx::createTexture2D(uint16_t(size), uint16_t(size), false, 1,
                                          bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                          bgfx::copy(rgba.data(), uint32_t(rgba.size())));
    if (!bgfx::isValid(chartTexture_)) return false;
    bgfx::setName(chartTexture_, "minimap chart");
    chart_ = {chartTexture_, float(size), float(size)};
    chartTiles_ = float(tiles);
    core::logf("minimap: charted map %u, %d texels square, in %.1f ms", tables.map, size,
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
    return true;
}

void Minimap::setView(const float* view) {
    // bx's view matrix keeps the camera's axes in its columns: right is (0, 4, 8) and up is
    // (1, 5, 9). On the ground a column is +x and a row is -z (docs/conventions.md), so each
    // becomes a tile vector with its z negated. Up on the ground is where the camera looks,
    // since it looks down at a slant; flattened, the two are square to one another.
    const auto flat = [](float x, float z, float* out) {
        const float length = std::sqrt(x * x + z * z);
        if (length < 1e-4f) return;
        out[0] = x / length;
        out[1] = -z / length;
    };
    flat(view[0], view[8], right_);
    flat(view[1], view[9], up_);
}

void Minimap::place(float dc, float dr, float* sx, float* sy) const {
    *sx = (dc * right_[0] + dr * right_[1]) * scale_;
    *sy = -(dc * up_[0] + dr * up_[1]) * scale_;
}

void Minimap::update(float seconds, const Play& play, const Pointer& pointer, float scroll,
                     int width, int height) {
    if (!play.isOpen()) {
        if (showing_) dismiss();
        return;
    }
    const float u = tip::unit();
    if (u != bakedUnit_ && !bake(u)) return;
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    if (int(tables.map) != chartMap_ || ground_ != chartGround_) {
        bakeChart(tables, ground_);
        built_ = false;
    }

    play.focus(&heroX_, &heroY_);
    if (full_) {
        // The whole map in the middle of the screen, its own middle at the screen's.
        const float side = std::round(float(std::min(width, height)) * kFullShare);
        map_ = {std::round((float(width) - side) * 0.5f), std::round(float(height) * kFullMiddle - side * 0.5f),
                side, side};
        outer_ = map_;
        radius_ = side * 0.5f;
        scale_ = side / (chartTiles_ * kFullFit);
        focusX_ = focusY_ = chartTiles_ * 0.5f - 0.5f;
    } else {
        const float side = std::round(kSide * u), at = std::round(kInset * u);
        // The fade stands past the disc that reads, so it is the disc inside it that lines up.
        const float right = float(width) - kRight * u + kFade * u;
        map_ = {std::round(right - side), at, side, side};
        outer_ = map_;
        radius_ = side * 0.5f;

        if (scroll != 0.0f && covers(pointer.x, pointer.y)) {
            zoom_ = std::clamp(zoom_ + (scroll > 0.0f ? -1 : 1), 0, kZooms - 1);
        }
        scale_ = side / kSpans[zoom_];
        focusX_ = heroX_;
        focusY_ = heroY_;
    }

    // His facing, eased round the short way.
    const sim::Body& hero = realm.hero();
    if (!faced_) facing_ = hero.facing;
    faced_ = true;
    facing_ += std::remainder(hero.facing - facing_, 6.2831853f) * std::min(1.0f, seconds * kTurnRate);
    facing_ = std::remainder(facing_, 6.2831853f);

    std::vector<Mark>& marks = now_.marks;
    marks.clear();
    const float cx = map_.midX(), cy = map_.midY();
    const float fade = kFade * u;
    // Where a pinned mark is held: just inside the rim.
    const float rim = radius_ - fade * kRimIn;
    const float hold = rim - 7.0f * u;
    // Puts a mark down at a tile. Out of reach, a pinning mark is held on the line to it at
    // `hold`, and any other fades with the rim it is under and is left off past it.
    const auto put = [&](Glyph glyph, float column, float row, bool pins, int name, int32_t kind) {
        float sx = 0.0f, sy = 0.0f;
        place(column - focusX_, row - focusY_, &sx, &sy);
        bool pinned = false;
        const float out = std::sqrt(sx * sx + sy * sy);
        int shown = 16;
        // The whole map holds everything on it: nothing is out of reach.
        if (full_) {
        } else if (pins && out > hold) {
            sx *= hold / out;
            sy *= hold / out;
            pinned = true;
        } else if (!pins) {
            if (out > rim - 3.0f * u) return;
            shown = int(std::lround(clamp01((rim - out) / (fade * 0.4f)) * 16.0f));
            if (shown <= 0) return;
        }
        Mark mark;
        mark.fade = shown;
        mark.glyph = glyph;
        mark.x = int(std::lround((cx + sx) * 16.0f));
        mark.y = int(std::lround((cy + sy) * 16.0f));
        mark.pinned = pinned;
        mark.name = name;
        mark.kind = kind;
        marks.push_back(mark);
    };

    // The quest's monsters: every live one of a breed a taken quest still has a count open on.
    int32_t wanted[sim::kQuests * sim::kQuestSteps];
    int wants = 0;
    for (int q = 0; q < sim::kQuests; ++q) {
        const sim::QuestProgress& progress = realm.quest(q);
        if (progress.state != sim::QuestState::Active) continue;
        const sim::QuestRow& row = sim::questAt(q);
        for (int s = 0; s < row.stepCount; ++s) {
            if (row.steps[s].kind != sim::QuestStepKind::Clear) continue;
            if (int(progress.counts[s]) >= realm.questGoal(q, s)) continue;
            wanted[wants++] = row.steps[s].target;
        }
    }
    if (wants > 0) {
        for (const sim::Body& body : realm.bodies()) {
            if (body.player || body.kind < 0 || !body.alive()) continue;
            const int32_t number = tables.kinds[size_t(body.kind)].number;
            if (std::find(wanted, wanted + wants, number) == wanted + wants) continue;
            float column = body.x, row = body.y;
            play.shownAt(body.id, &column, &row);
            put(Glyph::Quarry, column, row, false, -1, body.kind);
        }
    }

    // The townsfolk who do something, the giver last so he is drawn over them.
    int giver = -1;
    Glyph giverGlyph = Glyph::Folk;
    bool giverAgain = false;
    float giverColumn = 0.0f, giverRow = 0.0f;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        const content::Townsperson& one = tables.folk[i];
        // The guards, MU's 247 and 249: six of them, and nothing to say.
        if (one.number == 247 || one.number == 249) continue;
        // Where he is: his body's tile when he stands as one, else where the table stands him.
        float column = float(one.x), row = float(one.y);
        // Read as he is drawn, between ticks, as the chart is slid by where the hero is drawn.
        if (const uint32_t id = play.wardenBody(int(i)); id != 0) play.shownAt(id, &column, &row);
        Glyph glyph = Glyph::Folk;
        if (const int quest = realm.questHere(one.number); quest >= 0) {
            const bool ready = realm.quest(quest).state == sim::QuestState::Ready;
            if (ready || realm.questOffered(quest)) {
                giver = int(i);
                giverGlyph = ready ? Glyph::HandIn : Glyph::Offer;
                giverAgain = realm.quest(quest).completions > 0;
                giverColumn = column;
                giverRow = row;
                continue;
            }
        } else if (one.number == sim::kVaultKeeper) {
            glyph = Glyph::Vault;
        } else if (sim::repairsAt(one.number)) {
            glyph = Glyph::Smith;
        } else if (one.number == 253 || one.number == 255 || one.number == 244) {
            glyph = Glyph::Potion;  // the potion girl and the two barmaids (sim/market.cpp)
        } else if (sim::sells(one.number)) {
            glyph = Glyph::Vendor;
        }
        put(glyph, column, row, false, int(i), -1);
    }

    // The ways out, found once a map by asking the gates' own test of every tile: sim/gates.h
    // lists them by number and not by map, and the test is the one the realm walks him by.
    if (int(tables.map) != gatesMap_) {
        gatesMap_ = int(tables.map);
        gates_.clear();
        for (int row = 0; row < kMapTiles; ++row) {
            for (int column = 0; column < kMapTiles; ++column) {
                const sim::EnterGate* gate = sim::enterGateAt(tables.map, column, row);
                if (gate && std::find(gates_.begin(), gates_.end(), gate) == gates_.end()) {
                    gates_.push_back(gate);
                }
            }
        }
    }
    for (const sim::EnterGate* gate : gates_) {
        const sim::GateBox& b = gate->box;
        put(Glyph::Gate, float(b.x1 + b.x2) * 0.5f, float(b.y1 + b.y2) * 0.5f, true, -1,
            gate->number);
    }
    if (giver >= 0) {
        const size_t before = marks.size();
        put(giverGlyph, giverColumn, giverRow, true, giver, -1);
        if (marks.size() > before) marks.back().again = giverAgain;
    }

    // Her summon, held to the edge when it is out of reach, so one left behind is found.
    if (const sim::Body* summon = realm.summoned(); summon != nullptr && summon->alive()) {
        float column = summon->x, row = summon->y;
        play.shownAt(summon->id, &column, &row);
        put(Glyph::Summon, column, row, true, -1, summon->kind);
    }

    // Every other player in the world, held to the edge so one far off is found: for now the bots.
    // Named by his body's index (`kind`), read back below.
    for (int p = 0; p < realm.playerCount(); ++p) {
        const sim::Body& other = realm.playerAt(p);
        if (other.id == realm.hero().id || !other.alive()) continue;
        float column = other.x, row = other.y;
        play.shownAt(other.id, &column, &row);
        put(Glyph::Player, column, row, true, -1, int32_t(&other - realm.bodies().data()));
    }

    // Him, last and on top: his facing as a screen direction, clockwise from up.
    {
        float fx = 0.0f, fy = 0.0f;
        place(std::cos(facing_), std::sin(facing_), &fx, &fy);
        float hx = 0.0f, hy = 0.0f;
        place(heroX_ - focusX_, heroY_ - focusY_, &hx, &hy);
        Mark mark;
        mark.glyph = Glyph::Hero;
        mark.x = int(std::lround((cx + hx) * 16.0f));
        mark.y = int(std::lround((cy + hy) * 16.0f));
        mark.angle = int(std::lround(std::atan2(fx, -fy) * 1800.0f / 3.14159265f));
        marks.push_back(mark);
    }

    // The mark under the pointer, nearest first, him excepted.
    now_.hovered = -1;
    if (full_ || covers(pointer.x, pointer.y)) {
        float best = kReach * u * kReach * u;
        for (size_t i = 0; i + 1 < marks.size(); ++i) {
            const float dx = float(marks[i].x) / 16.0f - pointer.x, dy = float(marks[i].y) / 16.0f - pointer.y;
            const float d = dx * dx + dy * dy;
            if (d < best) {
                best = d;
                now_.hovered = int(i);
            }
        }
    }

    now_.width = width;
    now_.height = height;
    now_.heroX = int(std::lround(heroX_ * 16.0f));
    now_.heroY = int(std::lround(heroY_ * 16.0f));
    now_.turn = int(std::lround(std::atan2(right_[1], right_[0]) * 1800.0f / 3.14159265f));
    now_.zoom = zoom_;
    now_.full = full_;
    now_.column = int(std::lround(hero.x));
    now_.row = int(std::lround(hero.y));
    showing_ = true;
    if (built_ && now_ == drawn_) return;
    rebuild(play);
    drawn_ = now_;
    built_ = true;
}

// The whole map: the world darkened behind it, the chart as one quad from the map's four corners,
// turned by the camera as the disc is, and its name and where he stands over it. Flat: no plate
// and no line round its edge, only the land's own lines on the darkened world -- the user's "remove
// outline so its flat", "container outlines" (2026-10-01).
void Minimap::fullChart(const Play& play) {
    const float u = tip::unit();
    const float cx = map_.midX(), cy = map_.midY();
    {
        const float w = float(now_.width), h = float(now_.height);
        const float xy[8] = {0.0f, 0.0f, w, 0.0f, w, h, 0.0f, h};
        const uint32_t ink = (style::kVoid & 0x00FFFFFFu) | (uint32_t(kFullScrim * 255.0f + 0.5f) << 24);
        const uint32_t tones[4] = {ink, ink, ink, ink};
        canvas_.polygon(xy, tones, 4);
    }
    if (!chart_.valid()) return;
    const float lo = -0.5f, hi = chartTiles_ - 0.5f;
    const float corners[4][2] = {{lo, lo}, {hi, lo}, {hi, hi}, {lo, hi}};
    float xy[8];
    for (int i = 0; i < 4; ++i) {
        place(corners[i][0] - focusX_, corners[i][1] - focusY_, &xy[i * 2], &xy[i * 2 + 1]);
        xy[i * 2] += cx;
        xy[i * 2 + 1] += cy;
    }
    const float uv[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    canvas_.polygon(&chart_, xy, uv, 4, gfx::rgba(1, 1, 1, kChartAlpha));
    // The map's name over it, and where he stands beside it.
    const std::string title = std::string(mapName(play.realm().tables()->map)) + "  \xC2\xB7  " +
                              std::to_string(now_.column) + ", " + std::to_string(now_.row);
    float top = xy[1];
    for (int i = 1; i < 4; ++i) top = std::min(top, xy[i * 2 + 1]);
    const float size = kFullTitle * u;
    const float wide = controls::labelWidth(size, title);
    controls::label(canvas_, std::round(cx - wide * 0.5f), std::round(std::max(top - 10.0f * u, size * 1.2f)),
                    size, style::kBone, title);
}

void Minimap::rebuild(const Play& play) {
    ++rebuilds_;
    canvas_.clear();
    const float u = tip::unit();
    const float cx = map_.midX(), cy = map_.midY();
    const float fade = kFade * u;

    // The chart: an opaque disc and, round it, two bands fading to nothing at the rim -- 1, then
    // under half, then none, which is near enough a smoothstep that no ring shows. Each vertex's
    // texel is found by turning the screen back into tiles, the inverse of place(). Off the map
    // the clamp holds the map's own edge, which is the blocked tone.
    if (full_) {
        fullChart(play);
    } else if (chart_.valid()) {
        const float a = right_[0], b = right_[1], c = -up_[0], d = -up_[1];
        const float det = a * d - b * c;
        const auto texel = [&](float x, float y, float* uv) {
            const float sx = (x - cx) / scale_, sy = (y - cy) / scale_;
            const float dc = (d * sx - b * sy) / det, dr = (-c * sx + a * sy) / det;
            uv[0] = (focusX_ + dc + 0.5f) / chartTiles_;
            uv[1] = (focusY_ + dr + 0.5f) / chartTiles_;
        };
        // The scrim is solid to the rim and fades past it; the chart eases a little inside it and
        // stops on it, where the ring is drawn.
        const float rim = radius_ - fade * kRimIn;
        const float shade[3] = {rim, rim + (radius_ - rim) * 0.45f, radius_};
        const float radii[3] = {rim - fade * 0.35f, rim - fade * 0.12f, rim};
        const auto ringAt = [&](int ring, int i, float* xy) {
            const float t = 6.2831853f * float(i % kSegments) / float(kSegments);
            xy[0] = cx + std::cos(t) * shade[ring];
            xy[1] = cy + std::sin(t) * shade[ring];
        };
        // The scrim: the same disc and bands, solid.
        {
            const uint32_t voidInk = style::kVoid & 0x00FFFFFFu;
            const auto ink = [&](float alpha) { return voidInk | (uint32_t(alpha * 255.0f + 0.5f) << 24); };
            float xy[2 * (kSegments + 2)];
            uint32_t tones[kSegments + 2];
            xy[0] = cx;
            xy[1] = cy;
            tones[0] = ink(kScrimAlpha);
            for (int i = 0; i <= kSegments; ++i) {
                ringAt(0, i, &xy[(i + 1) * 2]);
                tones[i + 1] = ink(kScrimAlpha);
            }
            canvas_.polygon(xy, tones, kSegments + 2);
            const float steps[3] = {kScrimAlpha, kScrimAlpha * 0.42f, 0.0f};
            for (int ring = 0; ring < 2; ++ring) {
                for (int i = 0; i < kSegments; ++i) {
                    float quad[8];
                    ringAt(ring, i, &quad[0]);
                    ringAt(ring, i + 1, &quad[2]);
                    ringAt(ring + 1, i + 1, &quad[4]);
                    ringAt(ring + 1, i, &quad[6]);
                    const uint32_t band[4] = {ink(steps[ring]), ink(steps[ring]), ink(steps[ring + 1]), ink(steps[ring + 1])};
                    canvas_.polygon(quad, band, 4);
                }
            }
        }
        // Most of the way see-through, so it lies over the world rather than on it: the user's
        // "more transparent so it's not too much active but we still can read it" (2026-09-29).
        const float alphas[3] = {kChartAlpha, kChartAlpha * 0.8f, kChartAlpha * 0.6f};
        const auto at = [&](int ring, int i, float* xy) {
            const float t = 6.2831853f * float(i % kSegments) / float(kSegments);
            xy[0] = cx + std::cos(t) * radii[ring];
            xy[1] = cy + std::sin(t) * radii[ring];
        };
        {
            float xy[2 * (kSegments + 2)], uv[2 * (kSegments + 2)];
            xy[0] = cx;
            xy[1] = cy;
            texel(cx, cy, uv);
            for (int i = 0; i <= kSegments; ++i) {
                at(0, i, &xy[(i + 1) * 2]);
                texel(xy[(i + 1) * 2], xy[(i + 1) * 2 + 1], &uv[(i + 1) * 2]);
            }
            canvas_.polygon(&chart_, xy, uv, kSegments + 2, gfx::rgba(1, 1, 1, kChartAlpha));
        }
        for (int ring = 0; ring < 2; ++ring) {
            const uint32_t inner = gfx::rgba(1, 1, 1, alphas[ring]), outer = gfx::rgba(1, 1, 1, alphas[ring + 1]);
            for (int i = 0; i < kSegments; ++i) {
                float xy[8], uv[8];
                at(ring, i, &xy[0]);
                at(ring, i + 1, &xy[2]);
                at(ring + 1, i + 1, &xy[4]);
                at(ring + 1, i, &xy[6]);
                for (int k = 0; k < 4; ++k) texel(xy[k * 2], xy[k * 2 + 1], &uv[k * 2]);
                const uint32_t tones[4] = {inner, inner, outer, outer};
                canvas_.polygon(chart_, xy, uv, tones, 4);
            }
        }
        // The rim: a dark hairline outside, then the bone line over the chart's edge.
        const auto band = [&](float from, float to, uint32_t tone) {
            for (int i = 0; i < kSegments; ++i) {
                const float t0 = 6.2831853f * float(i) / float(kSegments);
                const float t1 = 6.2831853f * float(i + 1) / float(kSegments);
                const float quad[8] = {cx + std::cos(t0) * from, cy + std::sin(t0) * from,
                                       cx + std::cos(t1) * from, cy + std::sin(t1) * from,
                                       cx + std::cos(t1) * to,   cy + std::sin(t1) * to,
                                       cx + std::cos(t0) * to,   cy + std::sin(t0) * to};
                const uint32_t tones[4] = {tone, tone, tone, tone};
                canvas_.polygon(quad, tones, 4);
            }
        };
        const float line = std::max(1.0f, kRimLine * u);
        const float hair = std::max(1.0f, u);
        band(rim, rim + hair, (style::kVoid & 0x00FFFFFFu) | (uint32_t(kRimShadow * 255.0f) << 24));
        band(rim - line, rim, gfx::rgba(kLine[0], kLine[1], kLine[2], kRimAlpha));
    }

    const float cellPx = float(cell_);
    for (const Mark& mark : now_.marks) {
        const float from = float(int(mark.glyph)) * cellPx;
        const float x = float(mark.x) / 16.0f, y = float(mark.y) / 16.0f;
        uint32_t tone = mark.again ? kAgainBlue : toneOf(mark.glyph);
        // Held at the rim, it is the way there rather than the place: quieter. Under the rim's
        // fade, it fades with it.
        // A quest to hand in is never quieted: it is the one mark worth walking to.
        const float shown = mark.glyph == Glyph::HandIn ? 1.0f
                            : mark.pinned               ? 0.7f
                                                        : float(mark.fade) / 16.0f;
        tone = (tone & 0x00FFFFFFu) | (uint32_t(shown * 255.0f + 0.5f) << 24);
        if (mark.glyph == Glyph::Hero) {
            const float a = float(mark.angle) * 3.14159265f / 1800.0f;
            const float cs = std::cos(a), sn = std::sin(a), h = cellPx * 0.5f;
            const float corners[4][2] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
            float xy[8], uv[8];
            for (int i = 0; i < 4; ++i) {
                xy[i * 2] = x + corners[i][0] * cs - corners[i][1] * sn;
                xy[i * 2 + 1] = y + corners[i][0] * sn + corners[i][1] * cs;
                uv[i * 2] = (from + (i == 1 || i == 2 ? cellPx : 0.0f)) / glyphs_.width;
                uv[i * 2 + 1] = i >= 2 ? 1.0f : 0.0f;
            }
            canvas_.polygon(&glyphs_, xy, uv, 4, tone);
            continue;
        }
        // Not rounded to the pixel: the chart under it slides by fractions, and a glyph snapped
        // to whole pixels swims on it as he walks.
        canvas_.region(glyphs_, {x - cellPx * 0.5f, y - cellPx * 0.5f, cellPx, cellPx},
                       {from, 0.0f, cellPx, cellPx}, tone);
    }

    // Where he stands, MU's own "(130, 127)" (CNewUIHeroPositionInfo), under the disc, on the
    // scrim's fading shadow just past the rim, with the map's name over it (the user,
    // 2026-10-07: 'above the coordinates show also map name'). The whole map has it beside its
    // name instead.
    char where[32];
    std::snprintf(where, sizeof where, "%d, %d", now_.column, now_.row);
    if (!full_) {
        const float figure = kFigure * u;
        const float line = std::round(cy + radius_ - fade * kRimIn + 5.0f * u + figure * 0.8f);
        const char* name = mapName(play.realm().tables()->map);
        const float named = controls::labelWidth(figure, name);
        controls::label(canvas_, std::round(cx - named * 0.5f), line, figure, style::kBone, name);
        const float wide = controls::labelWidth(figure, where);
        controls::label(canvas_, std::round(cx - wide * 0.5f), std::round(line + figure * 1.2f),
                        figure, style::kBone2, where);
    }

    // The name of the mark under the pointer, over it and inside the screen.
    if (now_.hovered >= 0) {
        const Mark& mark = now_.marks[size_t(now_.hovered)];
        const content::Tables& tables = *play.realm().tables();
        std::string words;
        if (mark.name >= 0) {
            words = tables.folk[size_t(mark.name)].name;
        } else if ((mark.glyph == Glyph::Quarry || mark.glyph == Glyph::Summon) && mark.kind >= 0) {
            words = tables.kinds[size_t(mark.kind)].label;
        } else if (mark.glyph == Glyph::Player && mark.kind >= 0 &&
                   size_t(mark.kind) < play.realm().bodies().size()) {
            const sim::Body& other = play.realm().bodies()[size_t(mark.kind)];
            words = "Bot, level " + std::to_string(other.level) + " " +
                    sim::className(int(other.kin), other.second);
        } else if (mark.glyph == Glyph::Gate) {
            const sim::EnterGate* gate = sim::enterGateNumbered(mark.kind);
            const sim::ExitGate* to = gate ? sim::exitGate(gate->target) : nullptr;
            words = std::string("Gate to ") + (to ? mapName(to->map) : "another map");
            // A stair to another floor of the map he is on: named by the floor it comes out on.
            if (to && to->map == tables.map) words = std::string("Stairs to ") + floorName(to->number);
            // A sealed gate, to a map not built (sim/gates.cpp).
            if (gate && gate->target < 0) {
                words = std::string(gate->sealed ? gate->sealed : "Another map") + " (sealed)";
            } else if (const int asked = gate ? sim::moveLevel(gate->level, play.realm().hero().kin)
                                              : 0;
                       gate && play.realm().hero().level < asked) {
                words += " (level " + std::to_string(asked) + ")";
            }
        }
        if (!words.empty()) {
            const float size = 13.0f * u;
            const float wide = (controls::labelWidth(size, words) + 16.0f * u) * 0.5f;
            const float x = std::min(float(mark.x) / 16.0f, float(now_.width) - wide - 4.0f * u);
            // Over the mark, or under it where over would leave the screen.
            float bottom = float(mark.y) / 16.0f - cellPx * 0.4f;
            if (bottom - 24.0f * u < 4.0f * u) bottom = float(mark.y) / 16.0f + cellPx * 0.4f + 24.0f * u;
            controls::hint(canvas_, x, bottom, words, u);
        }
    }
}

}  // namespace mu::game
