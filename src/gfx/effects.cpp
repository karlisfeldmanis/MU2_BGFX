#include "gfx/effects.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <bx/sort.h>

#include "core/log.h"
#include "gfx/program.h"

namespace mu::gfx {
namespace {

// Four vertices a sprite against a 16-bit index buffer, so the pass cannot hold more than
// 16384 sprites whatever it is asked for. Clamped rather than silently wrapping, because an
// index that wraps draws a quad made of somebody else's corners and looks like a bug in the
// art. Nothing in MU comes close: a busy fight is tens.
constexpr uint32_t kMaxSprites = 65536 / 4;

// The bound sheets' samplers and stages, fs_effect's: s_albedo is common.sh's stage 0, and
// the other six sit on stages common.sh leaves free.
constexpr const char* kSheetNames[] = {"s_albedo", "s_sheet1", "s_sheet2", "s_sheet3",
                                       "s_sheet4", "s_sheet5", "s_sheet6"};
constexpr uint8_t kSheetStages[] = {0, 9, 10, 11, 13, 14, 15};

// fs_effect's kind, a sprite's look: which of its functions it takes.
float kindOf(Blend blend) {
    switch (blend) {
        case Blend::Additive: return 1.0f;
        case Blend::Flame: return 2.0f;
        case Blend::Smoke: return 3.0f;
        case Blend::Dust: return 4.0f;
        case Blend::Breath: return 5.0f;
        case Blend::Alpha:
        case Blend::Minus: break;
    }
    return 0.0f;
}

uint32_t packAbgr(const float* rgba) {
    const auto byteOf = [](float v) {
        const float clamped = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return uint32_t(clamped * 255.0f + 0.5f);
    };
    return byteOf(rgba[3]) << 24 | byteOf(rgba[2]) << 16 | byteOf(rgba[1]) << 8 | byteOf(rgba[0]);
}

}  // namespace

bool Effects::init(const std::string& shaderDir, uint32_t capacity) {
    if (capacity > kMaxSprites) {
        core::logf("effects: %u sprites asked for and %u is the 16-bit index limit; clamped",
                   capacity, kMaxSprites);
        capacity = kMaxSprites;
    }
    program_ = loadProgramFiles(shaderDir, "vs_effect", "fs_effect");
    if (!bgfx::isValid(program_)) {
        core::logError("effects: the transparent pass has no program; nothing will draw");
        return false;
    }
    for (uint32_t i = 0; i < kSheetSlots; ++i) {
        sSheet_[i] = bgfx::createUniform(kSheetNames[i], bgfx::UniformType::Sampler);
    }
    uFlame_ = bgfx::createUniform("u_flame", bgfx::UniformType::Vec4);
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .end();

    // The whole point of the sprint's proving sentence: both arrays are sized once, here,
    // and a frame only ever writes into what is already reserved.
    sprites_.reserve(capacity);
    order_.reserve(capacity);
    orderSpare_.reserve(capacity);
    keys_.reserve(capacity);
    keysSpare_.reserve(capacity);
    core::logf("effects: transparent pass ready, %u sprites reserved", capacity);
    return true;
}

void Effects::shutdown() {
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    for (bgfx::UniformHandle& u : sSheet_) {
        if (bgfx::isValid(u)) bgfx::destroy(u);
        u = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(uFlame_)) bgfx::destroy(uFlame_);
    uFlame_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    sprites_.clear();
    sprites_.shrink_to_fit();
    order_.clear();
    order_.shrink_to_fit();
    for (auto* v : {&orderSpare_, &keys_, &keysSpare_}) {
        v->clear();
        v->shrink_to_fit();
    }
}

void Effects::begin() {
    // clear() keeps the capacity, which is the difference between this and a pass that
    // allocates. The vector never grows past what init reserved because add() refuses first.
    sprites_.clear();
    order_.clear();
    drawCount_ = 0;
}

bool Effects::add(const Sprite& sprite) {
    if (sprites_.size() >= sprites_.capacity()) {
        ++refused_;
        return false;
    }
    if (!bgfx::isValid(sprite.sheet)) return false;
    sprites_.push_back(sprite);
    if (sprites_.size() > highWater_) highWater_ = uint32_t(sprites_.size());
    return true;
}

void Effects::draw(uint16_t view, const float* viewMtx, const float* projMtx, const float* eye) {
    bgfx::setViewTransform(view, viewMtx, projMtx);
    lastSprites_ = uint32_t(sprites_.size());
    if (sprites_.empty() || !bgfx::isValid(program_)) {
        // A view bgfx sees nothing in is dropped, and an account with no rows reads as free
        // rather than as unbuilt -- the same reason the hud view is touched when empty.
        bgfx::touch(view);
        return;
    }

    // The camera's right and up in world space, out of the view matrix's own rotation. Taken
    // from the matrix rather than from a camera struct so that the billboard faces exactly
    // the frustum being drawn into, which is the same rule the renderer states about culling
    // against the matrices it draws with.
    const float right[3] = {viewMtx[0], viewMtx[4], viewMtx[8]};
    const float up[3] = {viewMtx[1], viewMtx[5], viewMtx[9]};

    // Back to front, because a blended pass has no depth write and the order IS the result.
    // The indices are sorted and not the sprites: 4 bytes moved instead of 64.
    //
    // Farthest first. Ties broken by index so the order is stable frame to frame: two sprites
    // at the same distance swapping places every frame is a visible flicker in exactly the
    // case this pass is for, a cloud of particles thrown from one point.
    //
    // **A radix sort on each distance, worked out once** (2026-10-04): std::sort worked both
    // distances out again in every comparison and was nine tenths of this pass's CPU in a
    // Meteorite shower. A non-negative float's bits order as its value does, so the inverted
    // bits put the farthest first, and the radix sort is stable, so equal distances keep the
    // index order -- the same order as before, checked against std::sort on 200 random sets.
    const uint32_t count = uint32_t(sprites_.size());
    order_.resize(count);
    orderSpare_.resize(count);
    keys_.resize(count);
    keysSpare_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const float dx = sprites_[i].position[0] - eye[0];
        const float dy = sprites_[i].position[1] - eye[1];
        const float dz = sprites_[i].position[2] - eye[2];
        const float away = dx * dx + dy * dy + dz * dz;
        uint32_t bits;
        std::memcpy(&bits, &away, sizeof(bits));
        keys_[i] = ~bits;
        order_[i] = i;
    }
    bx::radixSort(keys_.data(), keysSpare_.data(), order_.data(), orderSpare_.data(), count);

    const uint32_t quads = uint32_t(sprites_.size());
    const uint32_t vertexCount = quads * 4;
    const uint32_t indexCount = quads * 6;
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout_) < vertexCount ||
        bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount) {
        // bgfx's own per-frame ring, not ours. Saying so is better than drawing half a fight
        // and leaving somebody to wonder which half.
        core::logf("effects: bgfx's transient buffer would not hold %u sprites this frame",
                   quads);
        bgfx::touch(view);
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, vertexCount, layout_);
    bgfx::allocTransientIndexBuffer(&tib, indexCount);
    auto* vertices = reinterpret_cast<Vertex*>(tvb.data);
    auto* indices = reinterpret_cast<uint16_t*>(tib.data);

    // Depth TEST against what the prepass laid down, and no depth WRITE. An effect is
    // occluded by the wall in front of it and never occludes the effect behind it, which is
    // what makes the sort above the thing that decides the picture.
    //
    // No cull: the quads face the camera already, and a spin past a right angle would turn a
    // culled one invisible for half its life.
    //
    // Every kind is PREMULTIPLIED, which is what fs_effect writes and why neither blend is
    // bgfx's own BGFX_STATE_BLEND_ALPHA or _ADD. The reason is in that shader's header: _ADD is
    // (ONE, ONE) and never reads alpha, so with straight alpha an additive sprite could not
    // fade. Premultiplied, an added kind is (ONE, INV_SRC_ALPHA) with an alpha of 0, so mixed
    // and added sprites share one blend and one draw. `Minus` is MU's own (ZERO,
    // ONE_MINUS_SRC_COLOR) (ZzzOpenglUtil.cpp:423-431): premultiplied, the fade dims the
    // darkening with the rest, and it is the one blend that ends a run.
    const uint64_t common = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS;
    const uint64_t over = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    const uint64_t minus = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_COLOR);
    // The order the draws are submitted in is the order they are drawn, whatever the frame
    // drew in this view before them.
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);

    // One draw per run of sprites in depth order that needs no more than kSheetSlots sheets
    // and one blend. The runs come out of the depth order and are NOT regrouped, because
    // regrouping by sheet is exactly the thing that would put a near sprite behind a far one.
    bgfx::TextureHandle bound[kSheetSlots];
    uint32_t boundCount = 0;
    bool runMinus = false;
    uint32_t runStart = 0;
    const auto submitRun = [&](uint32_t runEnd) {
        bgfx::setState(common | (runMinus ? minus : over));
        // The WHOLE vertex buffer, and the run selected by the index range alone.
        //
        // Not `setVertexBuffer(0, &tvb, runStart * 4, runQuads * 4)`, which is the obvious
        // thing to write and is wrong: bgfx ADDS the start vertex to every index, and the
        // indices written below are already absolute. The first run starts at zero so it
        // draws correctly and every run after it reads vertices runStart*4 too far along --
        // so a quad takes its corners from other sprites entirely. On the digits that showed
        // as one glyph stretched across eight, reading "01234567" where the damage was 10,
        // and on the blood as splashes smeared into one cloud. A single-run frame looked
        // perfect throughout, which is what kept it hidden.
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib, runStart * 6, (runEnd - runStart) * 6);
        // A slot the run did not fill is given its first sheet, so no stage the shader
        // declares is ever left unbound.
        for (uint32_t i = 0; i < kSheetSlots; ++i) {
            bgfx::setTexture(kSheetStages[i], sSheet_[i], bound[i < boundCount ? i : 0]);
        }
        bgfx::setUniform(uFlame_, flame_);
        bgfx::submit(view, program_);
        ++drawCount_;
        runStart = runEnd;
        boundCount = 0;
    };

    for (uint32_t n = 0; n < quads; ++n) {
        const Sprite& s = sprites_[order_[n]];
        const bool isMinus = s.blend == Blend::Minus;
        uint32_t slot = 0;
        while (slot < boundCount && bound[slot].idx != s.sheet.idx) ++slot;
        if (n > runStart && (isMinus != runMinus || slot == kSheetSlots)) {
            submitRun(n);
            slot = 0;
        }
        if (n == runStart) runMinus = isMinus;
        if (slot == boundCount) bound[boundCount++] = s.sheet;

        const float cosine = std::cos(s.spin);
        const float sine = std::sin(s.spin);
        // The quad's own two axes, the camera's basis turned by the spin.
        const float ax[3] = {(right[0] * cosine + up[0] * sine) * s.halfWidth,
                             (right[1] * cosine + up[1] * sine) * s.halfWidth,
                             (right[2] * cosine + up[2] * sine) * s.halfWidth};
        const float ay[3] = {(up[0] * cosine - right[0] * sine) * s.halfHeight,
                             (up[1] * cosine - right[1] * sine) * s.halfHeight,
                             (up[2] * cosine - right[2] * sine) * s.halfHeight};

        const uint32_t abgr = packAbgr(s.colour);
        const float kind = kindOf(s.blend);

        const float corners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        // v runs down the sheet while the quad's y runs up it, so the BOTTOM corners take the
        // rectangle's larger v.
        const float vs[4] = {s.v1, s.v1, s.v0, s.v0};
        const float us[4] = {s.u0, s.u1, s.u1, s.u0};
        for (int c = 0; c < 4; ++c) {
            Vertex& out = vertices[n * 4 + c];
            if (s.placed) {
                out.x = s.corner[c][0];
                out.y = s.corner[c][1];
                out.z = s.corner[c][2];
                out.u = s.cornerUv[c][0];
                out.v = s.cornerUv[c][1];
            } else {
                out.x = s.position[0] + ax[0] * corners[c][0] + ay[0] * corners[c][1];
                out.y = s.position[1] + ax[1] * corners[c][0] + ay[1] * corners[c][1];
                out.z = s.position[2] + ax[2] * corners[c][0] + ay[2] * corners[c][1];
                out.u = us[c];
                out.v = vs[c];
            }
            out.abgr = abgr;
            out.slot = float(slot);
            out.kind = kind;
        }
        const uint16_t base = uint16_t(n * 4);
        uint16_t* tri = &indices[n * 6];
        tri[0] = base;
        tri[1] = uint16_t(base + 1);
        tri[2] = uint16_t(base + 2);
        tri[3] = base;
        tri[4] = uint16_t(base + 2);
        tri[5] = uint16_t(base + 3);
    }
    submitRun(quads);
}

}  // namespace mu::gfx
