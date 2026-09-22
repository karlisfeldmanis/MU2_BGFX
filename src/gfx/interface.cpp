#include "gfx/interface.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/files.h"
#include "core/log.h"

namespace mu::gfx {
namespace {

// The windows' own bake of the face. The same file as the overlay's and a different atlas:
// this one is drawn from a 13 px tip to a 20 px title, which is up to four times smaller than
// it is baked, so it carries a mip chain -- and a chain needs air round every glyph, or a
// letter meets its neighbour two levels down. Four texels is two levels of air.
constexpr float kBakePixels = 40.0f;
constexpr int kAtlas = 1024;
constexpr int kPadding = 4;

bgfx::ShaderHandle loadShader(const std::string& dir, const char* name) {
    const std::string path = dir + "/" + name + ".sc.bin";
    std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) return BGFX_INVALID_HANDLE;
    const bgfx::Memory* mem = bgfx::copy(bytes.data(), uint32_t(bytes.size()));
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    if (bgfx::isValid(handle)) bgfx::setName(handle, name);
    return handle;
}

}  // namespace

// ---- the canvas ---------------------------------------------------------------------------

const Face& Canvas::face() const { return owner_->face(); }

void Canvas::clear() {
    vertices_.clear();
    indices_.clear();
    runs_.clear();
}

void Canvas::begin(bgfx::TextureHandle texture) {
    if (runs_.empty() || runs_.back().texture.idx != texture.idx) {
        runs_.push_back({texture, uint32_t(indices_.size()), 0});
    }
}

void Canvas::quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                  uint32_t abgr) {
    const uint32_t base = uint32_t(vertices_.size());
    vertices_.push_back({x, y, u0, v0, abgr});
    vertices_.push_back({x + w, y, u1, v0, abgr});
    vertices_.push_back({x + w, y + h, u1, v1, abgr});
    vertices_.push_back({x, y + h, u0, v1, abgr});
    const uint32_t order[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    indices_.insert(indices_.end(), order, order + 6);
    runs_.back().count += 6;
}

void Canvas::image(const Art& art, const Box& to, uint32_t abgr) {
    if (!art.valid() || to.w <= 0.0f || to.h <= 0.0f) return;
    begin(art.handle);
    quad(to.x, to.y, to.w, to.h, 0.0f, 0.0f, 1.0f, 1.0f, abgr);
}

void Canvas::region(const Art& art, const Box& to, const Box& from, uint32_t abgr) {
    if (!art.valid() || to.w <= 0.0f || to.h <= 0.0f) return;
    begin(art.handle);
    quad(to.x, to.y, to.w, to.h, from.x / art.width, from.y / art.height,
         from.right() / art.width, from.bottom() / art.height, abgr);
}

void Canvas::rect(const Box& box, uint32_t abgr) {
    if (box.w <= 0.0f || box.h <= 0.0f) return;
    const Face& f = face();
    begin(owner_->faceTexture());
    quad(box.x, box.y, box.w, box.h, f.solidU(), f.solidV(), f.solidU(), f.solidV(), abgr);
}

void Canvas::shade(const Box& box, uint32_t topLeft, uint32_t topRight, uint32_t bottomRight,
                   uint32_t bottomLeft) {
    if (box.w <= 0.0f || box.h <= 0.0f) return;
    const Face& f = face();
    begin(owner_->faceTexture());
    const float u = f.solidU(), v = f.solidV();
    const uint32_t base = uint32_t(vertices_.size());
    vertices_.push_back({box.x, box.y, u, v, topLeft});
    vertices_.push_back({box.right(), box.y, u, v, topRight});
    vertices_.push_back({box.right(), box.bottom(), u, v, bottomRight});
    vertices_.push_back({box.x, box.bottom(), u, v, bottomLeft});
    const uint32_t order[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    indices_.insert(indices_.end(), order, order + 6);
    runs_.back().count += 6;
}

void Canvas::outline(const Box& box, float t, uint32_t abgr) {
    rect({box.x, box.y, box.w, t}, abgr);
    rect({box.x, box.bottom() - t, box.w, t}, abgr);
    rect({box.x, box.y + t, t, box.h - t * 2.0f}, abgr);
    rect({box.right() - t, box.y + t, t, box.h - t * 2.0f}, abgr);
}

void Canvas::polygon(const Art* art, const float* xy, const float* uv, int count,
                     uint32_t abgr) {
    if (count < 3) return;
    const bool solid = art == nullptr || !art->valid();
    const Face& f = face();
    begin(solid ? owner_->faceTexture() : art->handle);
    const uint32_t base = uint32_t(vertices_.size());
    for (int i = 0; i < count; ++i) {
        vertices_.push_back({xy[i * 2], xy[i * 2 + 1], solid ? f.solidU() : uv[i * 2],
                             solid ? f.solidV() : uv[i * 2 + 1], abgr});
    }
    for (int i = 1; i + 1 < count; ++i) {
        const uint32_t tri[3] = {base, base + uint32_t(i), base + uint32_t(i + 1)};
        indices_.insert(indices_.end(), tri, tri + 3);
        runs_.back().count += 3;
    }
}

void Canvas::polygon(const float* xy, const uint32_t* abgr, int count) {
    if (count < 3) return;
    const Face& f = face();
    begin(owner_->faceTexture());
    const uint32_t base = uint32_t(vertices_.size());
    for (int i = 0; i < count; ++i) {
        vertices_.push_back({xy[i * 2], xy[i * 2 + 1], f.solidU(), f.solidV(), abgr[i]});
    }
    for (int i = 1; i + 1 < count; ++i) {
        const uint32_t tri[3] = {base, base + uint32_t(i), base + uint32_t(i + 1)};
        indices_.insert(indices_.end(), tri, tri + 3);
        runs_.back().count += 3;
    }
}

float Canvas::text(float x, float baseline, float fontSize, uint32_t abgr, const std::string& s,
                   Align align, float width) {
    const Face& f = face();
    if (!f.ready() || s.empty()) return 0.0f;
    const float wide = f.measure(fontSize, s);
    float pen = x;
    if (align == Align::Centre) pen = x + (width - wide) * 0.5f;
    if (align == Align::Right) pen = x + width - wide;
    const float k = f.emScale(fontSize);
    begin(owner_->faceTexture());
    for (char c : s) {
        const FaceGlyph* g = f.glyph(c);
        if (!g) continue;
        if (g->x1 > g->x0 && g->y1 > g->y0) {
            quad(pen + g->x0 * k, baseline + g->y0 * k, (g->x1 - g->x0) * k,
                 (g->y1 - g->y0) * k, g->u0, g->v0, g->u1, g->v1, abgr);
        }
        pen += g->advance * k;
    }
    return wide;
}

float Canvas::lettered(const Face& f, bgfx::TextureHandle texture, float x, float baseline,
                       float fontSize, float tracking, uint32_t abgr, const std::string& s) {
    if (!f.ready() || s.empty() || !bgfx::isValid(texture)) return 0.0f;
    const float k = f.emScale(fontSize);
    const float grow = f.spread(), growUv = f.spreadUv();
    begin(texture);
    float pen = x;
    for (char c : s) {
        const FaceGlyph* g = f.glyph(c);
        if (!g) continue;
        if (g->x1 > g->x0 && g->y1 > g->y0) {
            quad(pen + (g->x0 - grow) * k, baseline + (g->y0 - grow) * k,
                 (g->x1 - g->x0 + grow * 2.0f) * k, (g->y1 - g->y0 + grow * 2.0f) * k,
                 g->u0 - growUv, g->v0 - growUv, g->u1 + growUv, g->v1 + growUv, abgr);
        }
        pen += g->advance * k + tracking;
    }
    return pen - x;
}

float Canvas::shadowed(float x, float baseline, float fontSize, uint32_t abgr, uint32_t shadow,
                       float drop, const std::string& s, Align align, float width) {
    text(x + drop, baseline + drop, fontSize, shadow, s, align, width);
    return text(x, baseline, fontSize, abgr, s, align, width);
}

// ---- the interface ------------------------------------------------------------------------

bgfx::TextureHandle uploadFace(const Face& face, const char* name) {
    // White everywhere and the coverage in alpha, so the one shader that multiplies the art by
    // the vertex colour draws a letter too, and a letter and a plate share a draw whenever they
    // are next to each other in the list.
    const int size = face.size();
    if (!face.ready() || size <= 0 || face.pixels().size() != size_t(size) * size_t(size)) {
        return BGFX_INVALID_HANDLE;
    }
    std::vector<uint8_t> levels;
    uint32_t w = uint32_t(size), h = uint32_t(size);
    std::vector<uint8_t> alpha = face.pixels();
    while (true) {
        const size_t at = levels.size();
        levels.resize(at + size_t(w) * h * 4);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            uint8_t* p = &levels[at + i * 4];
            p[0] = p[1] = p[2] = 0xFF;
            p[3] = alpha[i];
        }
        if (w == 1 && h == 1) break;
        const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        std::vector<uint8_t> next(size_t(nw) * nh);
        for (uint32_t y = 0; y < nh; ++y) {
            for (uint32_t x = 0; x < nw; ++x) {
                const uint32_t x0 = x * 2, y0 = y * 2;
                const uint32_t x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
                const uint32_t sum = alpha[size_t(y0) * w + x0] + alpha[size_t(y0) * w + x1] +
                                     alpha[size_t(y1) * w + x0] + alpha[size_t(y1) * w + x1];
                next[size_t(y) * nw + x] = uint8_t((sum + 2) / 4);
            }
        }
        alpha.swap(next);
        w = nw;
        h = nh;
    }
    bgfx::TextureHandle handle =
        bgfx::createTexture2D(uint16_t(size), uint16_t(size), true, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_SAMPLER_UVW_CLAMP,
                              bgfx::copy(levels.data(), uint32_t(levels.size())));
    if (bgfx::isValid(handle)) bgfx::setName(handle, name);
    return handle;
}

bool Interface::init(const std::string& shaderDir) {
    if (face_.bake(facePath(), kBakePixels, kAtlas, kPadding)) {
        faceTexture_ = uploadFace(face_, "interface face");
        face_.dropPixels();
        core::logf("interface: face atlas %dx%d", kAtlas, kAtlas);
    } else {
        core::logError("the interface has no face; the windows will draw without their words");
        const uint32_t white = 0xFFFFFFFFu;
        faceTexture_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                             bgfx::copy(&white, 4));
    }

    sampler_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    bgfx::ShaderHandle vs = loadShader(shaderDir, "vs_overlay");
    bgfx::ShaderHandle fs = loadShader(shaderDir, "fs_interface");
    if (bgfx::isValid(vs) && bgfx::isValid(fs)) program_ = bgfx::createProgram(vs, fs, true);
    layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    if (!bgfx::isValid(program_)) {
        core::logError("the interface has no program; there will be no HUD");
        return false;
    }
    return true;
}

void Interface::shutdown() {
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(faceTexture_)) bgfx::destroy(faceTexture_);
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    program_ = BGFX_INVALID_HANDLE;
    faceTexture_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE;
}

void Interface::begin(int width, int height) {
    width_ = width;
    height_ = height;
    queued_.clear();
}

void Interface::add(const Canvas& canvas) {
    if (!canvas.empty()) queued_.push_back(&canvas);
}

void Interface::submit(bgfx::ViewId view) {
    draws_ = 0;
    vertexCount_ = 0;
    if (!ready() || queued_.empty()) return;
    uint32_t vertexCount = 0, indexCount = 0;
    for (const Canvas* c : queued_) {
        vertexCount += uint32_t(c->vertices_.size());
        indexCount += uint32_t(c->indices_.size());
    }
    // Checked first: bgfx drops a transient request it cannot meet, and drawing from a
    // half-filled buffer is worse than drawing nothing.
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout_) < vertexCount ||
        bgfx::getAvailTransientIndexBuffer(indexCount, true) < indexCount) {
        core::logError("the interface wanted %u vertices and the frame had no room",
                       vertexCount);
        return;
    }
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, vertexCount, layout_);
    bgfx::allocTransientIndexBuffer(&tib, indexCount, true);

    auto* vertices = reinterpret_cast<Canvas::Vertex*>(tvb.data);
    auto* indices = reinterpret_cast<uint32_t*>(tib.data);
    uint32_t vAt = 0, iAt = 0;
    bgfx::setViewRect(view, 0, 0, uint16_t(width_), uint16_t(height_));
    // No depth at all: this is over everything, including the tonemap, and sorts by the order
    // it was queued. Sequential, so bgfx keeps that order across the draws.
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    for (const Canvas* c : queued_) {
        std::memcpy(vertices + vAt, c->vertices_.data(),
                    c->vertices_.size() * sizeof(Canvas::Vertex));
        for (size_t i = 0; i < c->indices_.size(); ++i) indices[iAt + i] = c->indices_[i] + vAt;
        for (const Canvas::Run& run : c->runs_) {
            if (run.count == 0) continue;
            bgfx::setVertexBuffer(0, &tvb);
            bgfx::setIndexBuffer(&tib, iAt + run.firstIndex, run.count);
            bgfx::setTexture(0, sampler_, run.texture);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                                 BGFX_STATE_BLEND_INV_SRC_ALPHA));
            bgfx::submit(view, program_);
            ++draws_;
        }
        vAt += uint32_t(c->vertices_.size());
        iAt += uint32_t(c->indices_.size());
    }
    vertexCount_ = vAt;
}

}  // namespace mu::gfx
