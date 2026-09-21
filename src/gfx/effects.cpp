#include "gfx/effects.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "gfx/program.h"

namespace mu::gfx {
namespace {

// Four vertices a sprite against a 16-bit index buffer, so the pass cannot hold more than
// 16384 sprites whatever it is asked for. Clamped rather than silently wrapping, because an
// index that wraps draws a quad made of somebody else's corners and looks like a bug in the
// art. Nothing in MU comes close: a busy fight is tens.
constexpr uint32_t kMaxSprites = 65536 / 4;

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
    sSheet_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    // Fire and smoke, sprint 8b. Not required: without them a fire draws as plain added sprites.
    flameProgram_ = loadProgramFiles(shaderDir, "vs_effect", "fs_flame");
    smokeProgram_ = loadProgramFiles(shaderDir, "vs_effect", "fs_smoke");
    uFlame_ = bgfx::createUniform("u_flame", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(flameProgram_) || !bgfx::isValid(smokeProgram_)) {
        core::logError("effects: the flame or smoke program did not link; they draw plain");
    }
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    // The whole point of the sprint's proving sentence: both arrays are sized once, here,
    // and a frame only ever writes into what is already reserved.
    sprites_.reserve(capacity);
    order_.reserve(capacity);
    core::logf("effects: transparent pass ready, %u sprites reserved", capacity);
    return true;
}

void Effects::shutdown() {
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(sSheet_)) bgfx::destroy(sSheet_);
    for (bgfx::ProgramHandle* p : {&flameProgram_, &smokeProgram_}) {
        if (bgfx::isValid(*p)) bgfx::destroy(*p);
        *p = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(uFlame_)) bgfx::destroy(uFlame_);
    uFlame_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    sSheet_ = BGFX_INVALID_HANDLE;
    sprites_.clear();
    sprites_.shrink_to_fit();
    order_.clear();
    order_.shrink_to_fit();
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
    order_.resize(sprites_.size());
    for (uint32_t i = 0; i < sprites_.size(); ++i) order_[i] = i;
    const auto distanceSq = [&](uint32_t i) {
        const float dx = sprites_[i].position[0] - eye[0];
        const float dy = sprites_[i].position[1] - eye[1];
        const float dz = sprites_[i].position[2] - eye[2];
        return dx * dx + dy * dy + dz * dz;
    };
    std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) {
        const float da = distanceSq(a), db = distanceSq(b);
        // Farthest first. Ties broken by index so the order is stable frame to frame: two
        // sprites at the same distance swapping places every frame is a visible flicker in
        // exactly the case this pass is for, a cloud of particles thrown from one point.
        if (da != db) return da > db;
        return a < b;
    });

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

    for (uint32_t n = 0; n < quads; ++n) {
        const Sprite& s = sprites_[order_[n]];
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

    // Depth TEST against what the prepass laid down, and no depth WRITE. An effect is
    // occluded by the wall in front of it and never occludes the effect behind it, which is
    // what makes the sort above the thing that decides the picture.
    //
    // No cull: the quads face the camera already, and a spin past a right angle would turn a
    // culled one invisible for half its life.
    const uint64_t common = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS;

    // One draw per run of sprites that share a sheet and a blend. The runs come out of the
    // depth order and are NOT regrouped, because regrouping by sheet is exactly the thing
    // that would put a near sprite behind a far one.
    uint32_t runStart = 0;
    while (runStart < quads) {
        const Sprite& first = sprites_[order_[runStart]];
        uint32_t runEnd = runStart + 1;
        while (runEnd < quads) {
            const Sprite& next = sprites_[order_[runEnd]];
            if (next.sheet.idx != first.sheet.idx || next.blend != first.blend) break;
            ++runEnd;
        }
        const uint32_t runQuads = runEnd - runStart;

        // Both modes are PREMULTIPLIED, which is what fs_effect.sc writes and why neither is
        // bgfx's own BGFX_STATE_BLEND_ALPHA or _ADD. The reason is in that shader's header:
        // _ADD is (ONE, ONE) and never reads alpha, so with straight alpha an additive sprite
        // could not fade, and the tint's alpha would silently do nothing on half of MU's
        // effects. Premultiplied, the fade lives in the rgb and both modes honour it.
        const bool added = first.blend == Blend::Additive || first.blend == Blend::Flame;
        const uint64_t blend = added
                                   ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                                           BGFX_STATE_BLEND_ONE)
                                   : BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                                           BGFX_STATE_BLEND_INV_SRC_ALPHA);
        bgfx::setState(common | blend);
        // The WHOLE vertex buffer, and the run selected by the index range alone.
        //
        // Not `setVertexBuffer(0, &tvb, runStart * 4, runQuads * 4)`, which is the obvious
        // thing to write and is wrong: bgfx ADDS the start vertex to every index, and the
        // indices written above are already absolute. The first run starts at zero so it
        // draws correctly and every run after it reads vertices runStart*4 too far along --
        // so a quad takes its corners from other sprites entirely. On the digits that showed
        // as one glyph stretched across eight, reading "01234567" where the damage was 10,
        // and on the blood as splashes smeared into one cloud. A single-run frame looked
        // perfect throughout, which is what kept it hidden.
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib, runStart * 6, runQuads * 6);
        bgfx::setTexture(0, sSheet_, first.sheet);
        bgfx::ProgramHandle program = program_;
        if (first.blend == Blend::Flame && bgfx::isValid(flameProgram_)) program = flameProgram_;
        if (first.blend == Blend::Smoke && bgfx::isValid(smokeProgram_)) program = smokeProgram_;
        if (first.blend == Blend::Flame) bgfx::setUniform(uFlame_, flame_);
        bgfx::submit(view, program);
        ++drawCount_;
        runStart = runEnd;
    }
}

}  // namespace mu::gfx
