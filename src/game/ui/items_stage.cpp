#include "game/ui/items_stage.h"

#include <algorithm>
#include <cmath>

#include <bx/math.h>

#include "core/log.h"

namespace mu::game {
namespace {

// MU2's Panel.Fit: a model fills 82% of its box along whichever side binds first.
constexpr float kFill = 0.82f;
// Panel.Pose: a rest yaw of eight degrees on the vertical, and the hovered one turning at
// RenderObjectScreen's `WorldTime * 0.45`, 0.45 degrees a millisecond.
constexpr float kRestYaw = 8.0f * bx::kPi / 180.0f;
constexpr float kSpinPerSecond = 0.45f * 1000.0f * bx::kPi / 180.0f;

// Row-vector matrices, as bgfx's own: a point is v * M, and mtxMul(out, a, b) applies a first.
void translation(float* m, float x, float y, float z) { bx::mtxTranslate(m, x, y, z); }

}  // namespace

void ItemStage::shutdown() {
    if (bgfx::isValid(target_)) bgfx::destroy(target_);
    target_ = BGFX_INVALID_HANDLE;
    picture_ = gfx::Art{};
}

void ItemStage::stand(const std::vector<Standing>& items, float unitsW, float unitsH) {
    const bool same = items.size() == standing_.size() &&
                      std::equal(items.begin(), items.end(), standing_.begin()) &&
                      unitsW == unitsW_ && unitsH == unitsH_;
    if (same) return;
    standing_ = items;
    unitsW_ = unitsW;
    unitsH_ = unitsH;
    dirty_ = true;
}

void ItemStage::resize(int width, int height) {
    if (width == width_ && height == height_ && bgfx::isValid(target_)) return;
    if (bgfx::isValid(target_)) bgfx::destroy(target_);
    width_ = width;
    height_ = height;
    // Four samples, resolved by bgfx when the interface samples the picture: a model's edge in
    // a 20-unit cell is most of what the eye reads, and a stair-stepped hilt reads as broken.
    const bgfx::TextureHandle colour =
        bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
                              bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_RT_MSAA_X4 | BGFX_SAMPLER_UVW_CLAMP);
    const bgfx::TextureHandle depth = bgfx::createTexture2D(
        uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::D24S8,
        BGFX_TEXTURE_RT_WRITE_ONLY | BGFX_TEXTURE_RT_MSAA_X4);
    const bgfx::TextureHandle both[2] = {colour, depth};
    target_ = bgfx::createFrameBuffer(2, both, true);
    picture_.handle = colour;
    picture_.width = float(width);
    picture_.height = float(height);
    dirty_ = true;
}

void ItemStage::render(gfx::Renderer& renderer, float pixelsPerUnit, double seconds) {
    clock_ += seconds;
    if (unitsW_ <= 0.0f || unitsH_ <= 0.0f) return;
    resize(int(std::lround(unitsW_ * pixelsPerUnit)), int(std::lround(unitsH_ * pixelsPerUnit)));
    bool spinning = false;
    for (const Standing& one : standing_) spinning |= one.spinning;
    // A stage at rest is a picture already taken: nothing on it moved and nothing turns.
    if (!dirty_ && !spinning) return;
    dirty_ = false;
    ++renders_;

    drawables_.clear();
    for (const Standing& one : standing_) {
        const content::Mesh* mesh = models_ ? models_->of(one.item) : nullptr;
        if (!mesh) continue;
        const content::ItemRow& row = models_->tables()->items[size_t(one.item)];
        const content::Bounds& b = mesh->bounds();
        const float size[3] = {b.max[0] - b.min[0], b.max[1] - b.min[1], b.max[2] - b.min[2]};
        const float centre[3] = {(b.max[0] + b.min[0]) * 0.5f, (b.max[1] + b.min[1]) * 0.5f,
                                 (b.max[2] + b.min[2]) * 0.5f};

        // The face-on turn: rows of a row-vector matrix are where the local axes go. The
        // longest local axis to world up, the middle across, the thinnest toward the viewer.
        float basis[16];
        bx::mtxIdentity(basis);
        float extent[3] = {size[0], size[1], size[2]};
        if (!row.armour()) {
            int order[3] = {0, 1, 2};
            std::sort(order, order + 3, [&](int a, int c) { return size[a] > size[c]; });
            const float world[3][3] = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}};
            for (int i = 0; i < 16; ++i) basis[i] = 0.0f;
            basis[15] = 1.0f;
            for (int k = 0; k < 3; ++k) {
                for (int c = 0; c < 3; ++c) basis[order[k] * 4 + c] = world[k][c];
            }
            // A mirror is put right by flipping the axis toward the viewer, which nobody sees.
            const float det = basis[0] * (basis[5] * basis[10] - basis[6] * basis[9]) -
                              basis[1] * (basis[4] * basis[10] - basis[6] * basis[8]) +
                              basis[2] * (basis[4] * basis[9] - basis[5] * basis[8]);
            if (det < 0.0f) {
                for (int r = 0; r < 3; ++r) basis[r * 4 + 2] = -basis[r * 4 + 2];
            }
            extent[1] = size[order[0]];
            extent[0] = size[order[1]];
            extent[2] = size[order[2]];
        }
        const float across = extent[0] > 0.001f ? one.box.w / extent[0] : 1e9f;
        const float upward = extent[1] > 0.001f ? one.box.h / extent[1] : 1e9f;
        float fit = std::min(across, upward) * kFill;
        if (!std::isfinite(fit) || fit <= 0.0f || fit > 1e8f) fit = 1.0f;

        const float yaw = kRestYaw + (one.spinning ? float(clock_) * kSpinPerSecond : 0.0f);
        float toCentre[16], turn[16], scale[16], place[16], a[16], c[16], d[16];
        translation(toCentre, -centre[0], -centre[1], -centre[2]);
        bx::mtxRotateY(turn, yaw);
        bx::mtxScale(scale, fit, fit, fit);
        // The stage's world runs down the window, so a cell's y is negated.
        translation(place, one.box.x + one.box.w * 0.5f, -(one.box.y + one.box.h * 0.5f), 0.0f);
        bx::mtxMul(a, toCentre, basis);
        bx::mtxMul(c, a, turn);
        bx::mtxMul(d, c, scale);
        gfx::Drawable drawable;
        drawable.mesh = mesh;
        bx::mtxMul(drawable.transform, d, place);
        // A worn piece at its bind pose, which is the skin as it was modelled: a drawable with
        // no row of its own draws against the palette's bind row.
        drawable.paletteRow = -1;
        drawables_.push_back(drawable);
    }

    float view[16], proj[16];
    bx::mtxLookAt(view, bx::Vec3(0.0f, 0.0f, 500.0f), bx::Vec3(0.0f, 0.0f, 0.0f),
                  bx::Vec3(0.0f, 1.0f, 0.0f), bx::Handedness::Right);
    bx::mtxOrtho(proj, 0.0f, unitsW_, -unitsH_, 0.0f, 1.0f, 1000.0f, 0.0f,
                 bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    renderer.drawStage(view_, target_, uint16_t(width_), uint16_t(height_), view, proj,
                       drawables_);
}

}  // namespace mu::game
