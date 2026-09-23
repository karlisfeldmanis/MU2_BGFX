#include "game/world/grass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/files.h"
#include "core/log.h"
#include "game/frustum.h"

namespace mu::game {
namespace {

// A card: root pair, middle pair, top pair. Four triangles, twelve indices, six vertices.
// Two segments is what a card at MU's camera earns -- see docs/grass.md, where a tuft works
// out at some thirty pixels tall at the nearest the zoom goes. The middle row is not detail,
// it is what lets the card ARCH: with four vertices a leaning card is a flat parallelogram,
// and MU's grass leans hard.
constexpr int kVerticesPerCard = 6;
constexpr int kIndicesPerCard = 12;

// The instance: four vec4s. Not the engine's usual five -- a patch has no model matrix,
// because a card is built in world space out of the patch's own corner. varying.def.sc's
// i_data0..3 are read and the stride is what the buffer is walked by, not what a shader reads.
// The fourth is the paving at the tile's four corners, in the same order as the heights, so a
// card can bilinear it exactly as fs_ground bilinears the blend it draws the road with.
constexpr uint16_t kInstanceStride = 4 * 4 * sizeof(float);
constexpr size_t kFloatsPerInstance = 16;

bgfx::VertexLayout g_layout;
bool g_layoutReady = false;

const bgfx::VertexLayout& cardLayout() {
    if (!g_layoutReady) {
        // Position, and it is not a position: (card index, t, side). It rides in POSITION
        // because every vertex has one and bgfx asks for no new attribute to say so.
        g_layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
        g_layoutReady = true;
    }
    return g_layout;
}

}  // namespace

bool Grass::build(const std::string& assetDir, const std::string& world,
                  const content::Ground& ground, content::Textures& textures) {
    shutdown();

    std::vector<float> vertices;
    std::vector<uint16_t> indices;
    vertices.reserve(size_t(kCardsPerPatch) * kVerticesPerCard * 3);
    indices.reserve(size_t(kCardsPerPatch) * kIndicesPerCard);

    for (int card = 0; card < kCardsPerPatch; ++card) {
        const uint16_t base = uint16_t(card * kVerticesPerCard);
        const float t[kVerticesPerCard] = {0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f};
        const float side[kVerticesPerCard] = {-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f};
        for (int v = 0; v < kVerticesPerCard; ++v) {
            vertices.push_back(float(card));
            vertices.push_back(t[v]);
            vertices.push_back(side[v]);
        }
        // Winding is not chosen, because nothing culls here: a card is a surface with no
        // inside and fs_grass turns the normal to face whoever is looking.
        const uint16_t strip[kIndicesPerCard] = {
            uint16_t(base + 0), uint16_t(base + 1), uint16_t(base + 2),
            uint16_t(base + 1), uint16_t(base + 3), uint16_t(base + 2),
            uint16_t(base + 2), uint16_t(base + 3), uint16_t(base + 4),
            uint16_t(base + 3), uint16_t(base + 5), uint16_t(base + 4),
        };
        for (uint16_t i : strip) indices.push_back(i);
    }

    vbh_ = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(float))), cardLayout());
    ibh_ = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t))));
    if (!bgfx::isValid(vbh_) || !bgfx::isValid(ibh_)) {
        core::logError("the grass card strip did not survive creation");
        return false;
    }

    // MU's painted sheets, one per grass slot the world names, found by that slot's name.
    // The world's own copy first -- MU repaints its grass per map, and Noria's is not
    // Lorencia's -- then the shared one, which is what a map with no copy of its own gets.
    const std::string dir = core::join(assetDir, "effects/grass");
    size_t found = 0;
    for (int slot = 0; slot < 32; ++slot) {
        if (!ground.grassFloor(slot)) continue;
        const std::string& name = ground.floorName(slot);
        if (name.empty()) continue;
        bgfx::TextureHandle sheet =
            textures.load(core::join(dir, world + "_" + name + ".png"), content::TextureRole::Cutout);
        if (!bgfx::isValid(sheet)) {
            sheet = textures.load(core::join(dir, name + ".png"), content::TextureRole::Cutout);
        }
        if (!bgfx::isValid(sheet)) {
            core::logError("no painted grass sheet for slot %d (%s) of %s -- it grows nothing",
                           slot, name.c_str(), world.c_str());
            continue;
        }
        if (size_t(slot) >= sheets_.size()) {
            sheets_.resize(size_t(slot) + 1, BGFX_INVALID_HANDLE);
            sizes_.resize(size_t(slot) + 1, {0.0f, 0.0f});
        }
        sheets_[size_t(slot)] = sheet;
        uint32_t sw = 0, sh = 0;
        textures.sizeOf(sheet, &sw, &sh);
        sizes_[size_t(slot)] = {float(sw > 0 ? sw : 256), float(sh > 0 ? sh : 64)};
        ++found;
    }
    // The blade sheet, shared by every world: its greens are re-hued by the shader's grade, so
    // one painting serves Lorencia's olive and Noria's lush alike.
    sward_ = textures.load(core::join(dir, "sward.png"), content::TextureRole::Cutout);
    if (bgfx::isValid(sward_)) {
        uint32_t w = 0, h = 0;
        textures.sizeOf(sward_, &w, &h);
        swardSize_ = {float(w > 0 ? w : 1024), float(h > 0 ? h : 256)};
    } else {
        core::logf("no sward.png under %s -- the field falls back to MU's painted tuft "
                   "(pipeline/sward.py writes it)", dir.c_str());
    }

    // The meadow's own sheet, shared by every world: MU2 painted one and dims it per world
    // rather than painting two. Not required -- a sward with no flowers in it is what this
    // engine drew until now, and it says so rather than failing the launch.
    meadow_ = textures.load(core::join(dir, "wild.png"), content::TextureRole::Cutout);
    if (bgfx::isValid(meadow_)) {
        uint32_t mw = 0, mh = 0;
        textures.sizeOf(meadow_, &mw, &mh);
        meadowSize_ = {float(mw > 0 ? mw : 512), float(mh > 0 ? mh : 128)};
    } else {
        core::logf("no wild.png under %s -- the sward grows no flowers", dir.c_str());
    }

    if (found == 0) {
        core::logError("%s names no grass slot with a sheet behind it; no field will grow",
                       world.c_str());
        return false;
    }

    core::logf("grass: %d cards a patch on a %dx%d jitter, %zu vertices and %zu indices "
               "(%.1f KB, built once), %zu painted sheet(s)",
               kCardsPerPatch, kStratification, kStratification, vertices.size() / 3,
               indices.size(),
               double(vertices.size() * sizeof(float) + indices.size() * sizeof(uint16_t)) / 1024.0,
               found);
    return true;
}

bool Grass::gather(const content::Ground& ground, const gfx::Lighting& look, const float* viewProj,
                   const float* eye, const float* focus, float seconds, gfx::GrassField& field) {
    counts_ = Counts();
    field.batchCount = 0;
    if (!bgfx::isValid(vbh_) || sheets_.empty() || look.grass <= 0.0f) return false;
    if (ground.size() <= 0 || ground.floorAt(0, 0) < 0) return false;

    const float radius = std::max(1.0f, look.grassRadius);
    const float fadeBand = std::max(0.25f, std::min(look.grassFade, radius * 0.9f));
    const float metres = ground.metresPerTile();

    // The reach is measured from the EYE, and the shader does the measuring per card. What the
    // CPU does is coarser: it walks the square of tiles the reach could touch and hands the
    // frustum every patch whose nearest corner is inside it. The square is centred on where
    // the eye stands over the ground -- column is +x and row is -z, docs/conventions.md.
    // The focus is not needed for that; it stays in the call because it is the one point the
    // camera is defined by, and the next thing to measure from it is the walker's own wake.
    (void)focus;
    const float eyeColumn = eye[0] / metres;
    const float eyeRow = -eye[2] / metres;
    const int reach = int(std::ceil(radius / metres)) + 1;
    const int firstColumn = std::max(0, int(std::floor(eyeColumn)) - reach);
    const int lastColumn = std::min(ground.size() - 2, int(std::floor(eyeColumn)) + reach);
    const int firstRow = std::max(0, int(std::floor(eyeRow)) - reach);
    const int lastRow = std::min(ground.size() - 2, int(std::floor(eyeRow)) + reach);

    const Frustum frustum(viewProj);
    // One bucket a sheet, so what comes out is already sorted into contiguous draws. The
    // buckets are members and are only cleared, so a gather allocates nothing once the disc
    // has been walked once.
    int slotOfBucket[gfx::GrassField::kMaxSheets];
    int buckets = 0;
    for (int i = 0; i < gfx::GrassField::kMaxSheets; ++i) {
        packed_[i].clear();
        slotOfBucket[i] = -1;
    }

    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            ++counts_.considered;
            // Where grass grows: MU's own rule is the base mapping layer, and the engine asks
            // it by the slot's NAME rather than by its number. See Ground::grassFloor.
            const int slot = ground.floorAt(column, row);
            if (!ground.grassFloor(slot)) continue;
            if (size_t(slot) >= sheets_.size() || !bgfx::isValid(sheets_[size_t(slot)])) continue;
            ++counts_.grassy;

            // The tile's own square, in metres. x spans [column, column+1] and z spans
            // -(row+1) to -row, so the corner the shader grows from is (column, -(row + 1)).
            const float x0 = float(column) * metres;
            const float z0 = -float(row + 1) * metres;
            const float centreX = x0 + metres * 0.5f;
            const float centreZ = z0 + metres * 0.5f;

            // Where MU painted something over the lawn. tiles.png's blue is how far the overlay
            // has been taken across the base, per CORNER -- MU's terrain blends its alpha at
            // the cell's four vertices, and fs_ground draws the road off the same four -- and
            // where that is the paving the grass gives way to it, which is Turf's own rule in
            // MU2 and the reason MU's grass stops at the edge of the square without anything
            // saying so. An overlay that is itself grass paves nothing.
            //
            // The four corners go to the shader, which bilinears them at the card's own foot,
            // so grass stops where the cobbles start and not a tile away. Read at one corner
            // -- which is what this did first -- a tile whose corner was clear but whose other
            // three were road grew a full sward across the main road.
            //
            // The thinning with DISTANCE is not here either. It is the shader's, per card and
            // off the eye, so that it is a place on the screen rather than a ring round the
            // player; a ring moves with him, and the cards on it grow as it passes. The far
            // edge of the field is the same story, and it is the shader's for the same reason.
            auto paved = [&](int c, int r) {
                return ground.grassFloor(ground.overlayAt(c, r)) ? 0.0f : ground.blendAt(c, r);
            };
            // Same order as the heights: the v = 0 edge is the row+1 grid line.
            const float p00 = paved(column, row + 1);
            const float p10 = paved(column + 1, row + 1);
            const float p01 = paved(column, row);
            const float p11 = paved(column + 1, row);
            const float density = look.grassDensity;
            if (density <= 0.01f) continue;
            // A tile paved at every corner grows nothing, and is not sent.
            if (std::min(std::min(p00, p10), std::min(p01, p11)) > 0.85f) continue;

            // The four corner heights, in the order the shader bilinears them: the v = 0 edge
            // of the tile is the row+1 grid line, because v runs along +z and z runs -row.
            const float h00 = ground.heightAt(x0, z0);
            const float h10 = ground.heightAt(x0 + metres, z0);
            const float h01 = ground.heightAt(x0, z0 + metres);
            const float h11 = ground.heightAt(x0 + metres, z0 + metres);
            const float lowest = std::min(std::min(h00, h10), std::min(h01, h11));
            const float highest = std::max(std::max(h00, h10), std::max(h01, h11));

            // Past the reach, by the eye's own measure and in three dimensions: the shader
            // shrinks a card to nothing at `radius`, so a patch whose nearest point is beyond
            // it draws nothing and is not sent. Generous by half a diagonal, as the bounds
            // below are, because a patch is a square and not a point.
            const float dx = centreX - eye[0];
            const float dy = (lowest + highest) * 0.5f - eye[1];
            const float dz = centreZ - eye[2];
            const float nearest = std::sqrt(dx * dx + dy * dy + dz * dz) - 0.7071f * metres;
            if (nearest > radius) continue;

            // The patch's bounds, and the cap on what the shader may do to them. A card stands
            // at most its sward height times its own draw (1.28), its bunch's (1.16), its
            // patch's vigour (1.20) and, if it is one of the rank ones, 1.85 on top of that;
            // and it is up to that wide as well, widened again at distance. The box has to
            // hold the worst of all of it, or the bounds are a lie and a tuft bends into frame
            // out of a patch that was culled. Turf paid for this once in MU2 already.
            const float tallest = look.grassHeight * 1.28f * 1.16f * 1.20f * 1.85f;
            const float widest = tallest * look.grassAspect * (1.0f + look.grassWiden);
            const float centre[3] = {centreX, (lowest + highest) * 0.5f + tallest * 0.5f, centreZ};
            const float sphere = 0.7071f * metres + tallest + widest * 0.5f;
            if (!frustum.holds(centre, sphere)) continue;

            // Which bucket this patch's sheet is in, opening a new one if it has not been seen.
            int bucket = -1;
            for (int i = 0; i < buckets; ++i) {
                if (slotOfBucket[i] == slot) bucket = i;
            }
            if (bucket < 0) {
                if (buckets >= gfx::GrassField::kMaxSheets) continue;
                bucket = buckets++;
                slotOfBucket[bucket] = slot;
            }

            ++counts_.drawn;
            counts_.cards += uint32_t(float(kCardsPerPatch) * density);

            float light[3];
            ground.lightAt(column, row, light);

            // No seed is sent. The shader hashes the patch's own tile, which it already has in
            // the corner it grows from -- small numbers straight into a hash that behaves at
            // small numbers. The seed this used to fuse (column * 37 + row * 131) was both huge
            // and collision-prone, and grass.sh's note on the hash says what that looked like.

            const float instance[kFloatsPerInstance] = {
                x0, z0, h00, h10,
                h01, h11, density, 0.0f,
                light[0], light[1], light[2], 0.0f,
                p00, p10, p01, p11,
            };
            packed_[bucket].insert(packed_[bucket].end(), instance, instance + kFloatsPerInstance);
        }
    }

    if (counts_.drawn == 0) return false;
    if (bgfx::getAvailInstanceDataBuffer(counts_.drawn, kInstanceStride) < counts_.drawn) {
        core::logError("grass: bgfx has room for fewer than %u patches this frame", counts_.drawn);
        return false;
    }
    bgfx::allocInstanceDataBuffer(&field.instances, counts_.drawn, kInstanceStride);

    // One contiguous run a sheet, copied in bucket order, so each batch is one draw.
    uint32_t at = 0;
    uint8_t* out = field.instances.data;
    for (int i = 0; i < buckets; ++i) {
        const uint32_t count = uint32_t(packed_[i].size() / kFloatsPerInstance);
        if (count == 0) continue;
        std::memcpy(out, packed_[i].data(), packed_[i].size() * sizeof(float));
        out += size_t(count) * kInstanceStride;
        gfx::GrassField::Batch& batch = field.batches[field.batchCount++];
        batch.sheet = sheets_[size_t(slotOfBucket[i])];
        batch.width = sizes_[size_t(slotOfBucket[i])].first;
        batch.height = sizes_[size_t(slotOfBucket[i])].second;
        batch.first = at;
        batch.count = count;
        at += count;
    }

    // One sheet for the whole sward, or MU's one per slot. The blade sheet is not per world --
    // the colour grade is what makes it Lorencia's or Noria's -- so it collapses the batching
    // to a single draw, and the buckets above are left as they are: their instance data is the
    // same either way, and a batch that spans all of it is the same run.
    const bool painted = look.grassPainted > 0.5f || !bgfx::isValid(sward_);
    if (!painted) {
        field.batchCount = 1;
        field.batches[0].sheet = sward_;
        field.batches[0].first = 0;
        field.batches[0].count = counts_.drawn;
        field.batches[0].width = swardSize_.first;
        field.batches[0].height = swardSize_.second;
    }

    field.vertices = vbh_;
    field.indices = ibh_;

    field.card[0] = look.grassHeight;
    field.card[1] = look.grassAspect;
    field.card[2] = look.grassLean;
    field.card[3] = look.grassWiden;

    // The reach, from the eye: where the field ends, the band it shrinks away over before
    // that, and the band the thinning runs over -- from `grass_thin` out to where the fade
    // begins, so a card at the far edge has been thinned all it will be before it starts to
    // shrink. gfx::GrassField says why it is the eye and not the focus.
    field.reach[0] = radius;
    field.reach[1] = fadeBand;
    field.reach[2] = std::min(look.grassThin, radius - fadeBand - 1.0f);
    field.reach[3] = radius - fadeBand;

    // The wind turns from +x towards -z, which is the way the sun's azimuth turns and the way
    // a row runs on MU's grid. One convention for every angle in the sheet.
    const float windRadians = look.grassWindDegrees * 3.14159265f / 180.0f;
    field.wind[0] = std::cos(windRadians);
    field.wind[1] = -std::sin(windRadians);
    field.wind[2] = look.grassWindStrength;
    field.wind[3] = seconds;

    for (int i = 0; i < 3; ++i) field.root[i] = look.grassRootColour[i];
    field.root[3] = look.grassRootAo;
    for (int i = 0; i < 3; ++i) field.tip[i] = look.grassTipColour[i];
    field.tip[3] = look.grassRoughness;

    field.vary[0] = float(kCardsPerPatch);
    field.vary[1] = float(kStratification);
    field.vary[2] = look.grassRank;
    field.vary[3] = look.grassDry;

    field.sheet[0] = float(painted ? kSheetColumns : kSwardColumns);
    field.sheet[1] = look.grassCutout;
    field.sheet[2] = look.grassMipBias;
    field.sheet[3] = 0.0f;  // the sward, not the meadow
    field.colour = look.grassColour;

    // The meadow, over the same patches and the same instance buffer.
    field.meadow.sheet = meadow_;
    field.meadow.first = 0;
    field.meadow.count = bgfx::isValid(meadow_) ? counts_.drawn : 0;
    field.meadow.width = meadowSize_.first;
    field.meadow.height = meadowSize_.second;
    field.meadowVary[0] = float(kMeadowCards);
    field.meadowVary[1] = float(kMeadowStratification);
    field.meadowVary[2] = 0.0f;   // no rank plants: the sheet paints its own tall ones
    field.meadowVary[3] = 0.0f;   // and no straw: a daisy does not go over
    field.meadowSheet[0] = float(kMeadowColumns);
    field.meadowSheet[1] = kCutout;  // the meadow keeps the painted threshold
    field.meadowSheet[2] = look.grassMipBias;
    field.meadowSheet[3] = 1.0f;
    field.meadowCard[0] = look.grassMeadowHeight;
    field.meadowCard[1] = meadowSize_.second > 0.0f
                              ? (meadowSize_.first / float(kMeadowColumns)) / meadowSize_.second
                              : 0.5f;
    field.meadowDensity = look.grassMeadow;
    // Only the first few cards of each patch. The index buffer holds every card's triangles in
    // order, so a short range IS a smaller plant count -- no degenerate quads rasterised for
    // the ones that were never wanted.
    field.meadowIndices = uint32_t(kMeadowCards * kIndicesPerCard);
    // Nothing else writes field.sheet after this point. The first field wrote the deepest mip
    // and the sheet's width into .z and .w here, from a time when grassSheet() took them from
    // the uniform; it works them out from the sheet's own size now, and the two writes had
    // outlived that. Left in, they landed AFTER the meadow flag above was cleared: .w came out
    // 256, so fs_grass took every card of the sward for a flower and skipped the colour grade
    // and the straw, and .z came out +3, a bias of three whole mip levels of blur on a sheet
    // that was tuned with a bias of -0.4. That was most of why the blades read as plates.
    return true;
}

void Grass::shutdown() {
    if (bgfx::isValid(vbh_)) bgfx::destroy(vbh_);
    if (bgfx::isValid(ibh_)) bgfx::destroy(ibh_);
    vbh_ = BGFX_INVALID_HANDLE;
    ibh_ = BGFX_INVALID_HANDLE;
    // The sheets belong to Textures, which owns and frees them; this only forgets them.
    sheets_.clear();
    sizes_.clear();
    sward_ = BGFX_INVALID_HANDLE;
    meadow_ = BGFX_INVALID_HANDLE;
    for (std::vector<float>& bucket : packed_) bucket.clear();
    counts_ = Counts();
}

}  // namespace mu::game
