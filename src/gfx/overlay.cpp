#include "gfx/overlay.h"

#include <cctype>
#include <cstring>

#include "core/files.h"
#include "core/log.h"

namespace mu::gfx {
namespace {

// A 5x7 font, one byte a row, bit 4 leftmost. Written out rather than loaded, because a file
// is a thing that can be missing and this has to work on a machine that has just cloned the
// repo. Upper case only: a model's name is read here, not prose, and folding the lower case
// onto it halves a table that is already the longest thing in this file.
//
// Cell 0 is solid, which is what a panel draws with -- so a filled rectangle and a letter are
// the same quad against the same atlas, and the whole overlay stays one draw.
constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kCellW = 6;   // one column of padding, so neighbours do not bleed at a mip
constexpr int kCellH = 8;
constexpr int kCols = 16;

struct Glyph {
    char code;
    uint8_t rows[kGlyphH];
};

const Glyph kGlyphs[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
};
constexpr int kGlyphCount = int(sizeof(kGlyphs) / sizeof(kGlyphs[0]));
// Cell 0 is the solid one, so the glyphs start at 1.
constexpr int kFirstGlyphCell = 1;
constexpr int kCellCount = kFirstGlyphCell + kGlyphCount;
constexpr int kRows = (kCellCount + kCols - 1) / kCols;

int cellFor(char c) {
    const char upper = char(std::toupper(static_cast<unsigned char>(c)));
    for (int i = 0; i < kGlyphCount; ++i) {
        if (kGlyphs[i].code == upper) return kFirstGlyphCell + i;
    }
    return -1;  // space, and anything this font has no opinion about
}

// The renderer's shader loader again, kept local rather than shared: a header for one
// function that reads a file and makes a handle is machinery this does not need, and gfx is
// the only layer it belongs to either way.
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

bool Overlay::init(const std::string& shaderDir) {
    const int width = kCols * kCellW;
    const int height = kRows * kCellH;
    const bgfx::Memory* mem = bgfx::alloc(uint32_t(width * height));
    std::memset(mem->data, 0, size_t(width * height));

    // Cell 0 solid. Only the glyph box of it is filled, not the padding column, so a panel's
    // quad samples the middle of it and never catches a neighbouring cell's edge.
    for (int y = 0; y < kGlyphH; ++y) {
        for (int x = 0; x < kGlyphW; ++x) mem->data[y * width + x] = 0xFF;
    }
    for (int g = 0; g < kGlyphCount; ++g) {
        const int cell = kFirstGlyphCell + g;
        const int ox = (cell % kCols) * kCellW;
        const int oy = (cell / kCols) * kCellH;
        for (int y = 0; y < kGlyphH; ++y) {
            const uint8_t row = kGlyphs[g].rows[y];
            for (int x = 0; x < kGlyphW; ++x) {
                if (row & (1u << (kGlyphW - 1 - x))) {
                    mem->data[(oy + y) * width + (ox + x)] = 0xFF;
                }
            }
        }
    }

    // R8 and point sampled, with no mip chain: this is a mask at one size, and a filtered
    // 5-pixel letter is a grey smear rather than a letter.
    atlas_ = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
                                   bgfx::TextureFormat::R8,
                                   BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP, mem);
    sampler_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    bgfx::ShaderHandle vs = loadShader(shaderDir, "vs_overlay");
    bgfx::ShaderHandle fs = loadShader(shaderDir, "fs_overlay");
    // Not a ternary: BGFX_INVALID_HANDLE is a braced initialiser, and the right of a `:`
    // cannot be one.
    if (bgfx::isValid(vs) && bgfx::isValid(fs)) program_ = bgfx::createProgram(vs, fs, true);
    layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    if (!bgfx::isValid(program_)) {
        core::logError("the overlay has no program; the viewer will have no list on it");
        return false;
    }
    return bgfx::isValid(atlas_);
}

void Overlay::shutdown() {
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(atlas_)) bgfx::destroy(atlas_);
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    program_ = BGFX_INVALID_HANDLE;
    atlas_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE;
}

void Overlay::begin(int width, int height) {
    width_ = width;
    height_ = height;
    vertices_.clear();
    indices_.clear();
}

void Overlay::quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                   uint32_t abgr) {
    const uint16_t base = uint16_t(vertices_.size());
    vertices_.push_back({x, y, u0, v0, abgr});
    vertices_.push_back({x + w, y, u1, v0, abgr});
    vertices_.push_back({x + w, y + h, u1, v1, abgr});
    vertices_.push_back({x, y + h, u0, v1, abgr});
    const uint16_t order[6] = {base, uint16_t(base + 1), uint16_t(base + 2),
                               base, uint16_t(base + 2), uint16_t(base + 3)};
    for (uint16_t i : order) indices_.push_back(i);
}

void Overlay::panel(float x, float y, float w, float h, uint32_t abgr) {
    // The middle of cell 0, which is solid, so the panel needs no second texture and no
    // branch in the shader.
    const float u = 2.5f / float(kCols * kCellW);
    const float v = 3.5f / float(kRows * kCellH);
    quad(x, y, w, h, u, v, u, v, abgr);
}

float Overlay::measure(float scale, const std::string& s) {
    return float(s.size()) * float(kCellW) * scale;
}

float Overlay::lineHeight(float scale) { return float(kCellH) * scale; }

float Overlay::text(float x, float y, float scale, uint32_t abgr, const std::string& s) {
    const float atlasW = float(kCols * kCellW);
    const float atlasH = float(kRows * kCellH);
    float pen = x;
    for (char c : s) {
        const int cell = cellFor(c);
        if (cell >= 0) {
            const float ox = float((cell % kCols) * kCellW);
            const float oy = float((cell / kCols) * kCellH);
            quad(pen, y, float(kGlyphW) * scale, float(kGlyphH) * scale, ox / atlasW,
                 oy / atlasH, (ox + float(kGlyphW)) / atlasW, (oy + float(kGlyphH)) / atlasH,
                 abgr);
        }
        pen += float(kCellW) * scale;
    }
    return pen - x;
}

void Overlay::submit(bgfx::ViewId view) {
    if (indices_.empty() || !bgfx::isValid(program_)) return;
    const uint32_t vertexCount = uint32_t(vertices_.size());
    const uint32_t indexCount = uint32_t(indices_.size());
    // Transient, because this is rebuilt every frame and is a few kilobytes. A check first:
    // bgfx drops a transient request it cannot meet, and drawing from a half-filled buffer is
    // worse than drawing nothing.
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout_) < vertexCount ||
        bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount) {
        return;
    }
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, vertexCount, layout_);
    bgfx::allocTransientIndexBuffer(&tib, indexCount);
    std::memcpy(tvb.data, vertices_.data(), vertexCount * sizeof(Vertex));
    std::memcpy(tib.data, indices_.data(), indexCount * sizeof(uint16_t));

    bgfx::setViewRect(view, 0, 0, uint16_t(width_), uint16_t(height_));
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, sampler_, atlas_);
    // No depth at all: this is over everything, including the tonemap, and sorts by the order
    // the quads were added.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                         BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(view, program_);
}

}  // namespace mu::gfx
