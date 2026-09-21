#include "gfx/overlay.h"

#include <cctype>
#include <cstring>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "core/files.h"
#include "core/log.h"

namespace mu::gfx {
namespace {

// The face the overlay wears, and it is MU2's by inheritance: MU2 sets no theme font in
// either of its Godot projects, so every label it draws is Godot's fallback, Open Sans
// SemiBold. bootstrap.sh fetches the same file, pinned.
//
// Read from extern/ at run time rather than compiled in. It is 150 kB, it is the one asset
// gfx reads that is not the game's own, and a header holding it as a byte array is a header
// nobody can read. The cost is that it can be missing, which is what the bitmap fallback
// below is for.
constexpr const char* kFacePath = MU2_ROOT_DIR "/extern/OpenSans-SemiBold.ttf";
// A 1024 square, which is 1 MB of R8 and holds the ninety-five printable glyphs at the size
// below with the 2x2 oversampling they are packed with. 512 was tried first and the packing
// refused it -- an oversampled 40 px glyph occupies an 80 px box, and ninety-five of those
// do not fit in a quarter of a megapixel. The refusal was visible, in the log and in the
// picture, only because the bitmap fallback caught it.
constexpr int kAtlas = 1024;
// The rows at the bottom kept out of the packing, for the solid texel a panel draws with.
// A whole band rather than one texel because the face is sampled BILINEAR -- unlike the
// bitmap, which was point sampled -- and a solid texel with a packed glyph next to it would
// bleed that glyph's edge into every panel in the list.
constexpr int kSolidRows = 12;

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
// What one character advances by, in glyph pixels. Wider than the glyph: at 5 wide and no
// gap the letters of a name ran into each other and ARMORCLASS01 read as one long word.
constexpr float kAdvance = 6.5f;
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

bool Overlay::bakeFace(const std::string& path, uint8_t* pixels, int width, int height) {
    const std::vector<uint8_t> ttf = core::readFile(path);
    if (ttf.empty()) {
        core::logError("no face at %s, so the overlay falls back to its own 5x7 letters. "
                       "./bootstrap.sh fetches it", path.c_str());
        return false;
    }
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        core::logError("%s is not a font this can read", path.c_str());
        return false;
    }

    stbtt_pack_context pack;
    // The packing is given everything but the solid band at the bottom, and is told the full
    // stride, so the glyph coordinates that come back are already in the whole atlas's frame.
    if (!stbtt_PackBegin(&pack, pixels, width, height - kSolidRows, width, 1, nullptr)) {
        core::logError("the face would not pack into %dx%d", width, height - kSolidRows);
        return false;
    }
    // Two by two. The overlay draws this face at a fifth of the size it is baked at and
    // nothing here is pixel-aligned, so the horizontal oversample is what keeps a stem from
    // thinning to nothing between one label and the next.
    stbtt_PackSetOversampling(&pack, 2, 2);
    const int count = kLastCode - kFirstCode + 1;
    // Resized rather than constructed with a size: `vector<T> packed(size_t(count))` is a
    // function declaration, and the compiler then says the packing does not take a vector.
    std::vector<stbtt_packedchar> packed;
    packed.resize(size_t(count));
    const int ok = stbtt_PackFontRange(&pack, ttf.data(), 0, kBakePixels, kFirstCode, count,
                                       packed.data());
    stbtt_PackEnd(&pack);
    if (!ok) {
        core::logError("the face packed short; the atlas is too small for %d glyphs", count);
        return false;
    }

    // The face's own line box, which is what `lineHeight` is scaled against. Asked of the
    // font rather than derived from the glyphs: a line of "aces" and a line of "Qgjy" must
    // sit on the same baseline and take the same height.
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    const float toPixels = stbtt_ScaleForPixelHeight(&info, kBakePixels);
    bakedAscent_ = float(ascent) * toPixels;
    bakedLine_ = float(ascent - descent) * toPixels;

    glyphs_.assign(size_t(count), FaceGlyph{});
    for (int i = 0; i < count; ++i) {
        float penX = 0.0f, penY = 0.0f;
        stbtt_aligned_quad q;
        // align_to_integer 0: the quad is scaled down by the caller, so rounding it to whole
        // pixels HERE rounds at the wrong size and the spacing comes out uneven.
        stbtt_GetPackedQuad(packed.data(), width, height, i, &penX, &penY, &q, 0);
        FaceGlyph& g = glyphs_[size_t(i)];
        g.u0 = q.s0; g.v0 = q.t0; g.u1 = q.s1; g.v1 = q.t1;
        g.x0 = q.x0; g.y0 = q.y0; g.x1 = q.x1; g.y1 = q.y1;
        g.advance = packed[size_t(i)].xadvance;
    }

    // The solid band, inset from the packed rows and from the edge so that bilinear sampling
    // of its middle can only ever find more of itself.
    for (int y = height - kSolidRows + 3; y < height - 3; ++y) {
        for (int x = 3; x < 12; ++x) pixels[y * width + x] = 0xFF;
    }
    solidU_ = 7.5f / float(width);
    solidV_ = (float(height) - float(kSolidRows) * 0.5f) / float(height);
    core::logf("overlay: %s, %d glyphs baked at %.0f px, line %.1f px", path.c_str(), count,
               double(kBakePixels), double(bakedLine_));
    return true;
}

bool Overlay::init(const std::string& shaderDir) {
    // The face first; the 5x7 letters only if it is not there.
    std::vector<uint8_t> pixels(size_t(kAtlas) * size_t(kAtlas), 0);
    haveFace_ = bakeFace(kFacePath, pixels.data(), kAtlas, kAtlas);
    if (haveFace_) {
        const bgfx::Memory* face = bgfx::copy(pixels.data(), uint32_t(pixels.size()));
        // Bilinear, where the bitmap below is point sampled: this is a 64 px face drawn at
        // 16, and point sampling a minified glyph is what makes small text crawl and break.
        atlas_ = bgfx::createTexture2D(uint16_t(kAtlas), uint16_t(kAtlas), false, 1,
                                       bgfx::TextureFormat::R8, BGFX_SAMPLER_UVW_CLAMP, face);
    } else {
        const int width = kCols * kCellW;
        const int height = kRows * kCellH;
        const bgfx::Memory* mem = bgfx::alloc(uint32_t(width * height));
        std::memset(mem->data, 0, size_t(width * height));

        // Cell 0 solid. Only the glyph box of it is filled, not the padding column, so a
        // panel's quad samples the middle of it and never catches a neighbouring cell's edge.
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
        solidU_ = 2.5f / float(width);
        solidV_ = 3.5f / float(height);

        // R8 and point sampled, with no mip chain: this is a mask at one size, and a
        // filtered 5-pixel letter is a grey smear rather than a letter.
        atlas_ = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
                                       bgfx::TextureFormat::R8,
                                       BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP, mem);
    }

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
    // A solid texel out of whichever atlas was built, so a filled rectangle and a letter are
    // the same quad against the same texture and the whole overlay stays one draw.
    quad(x, y, w, h, solidU_, solidV_, solidU_, solidV_, abgr);
}

// How much a baked glyph is shrunk for a caller asking for `scale`. The line box is the
// contract -- `lineHeight` is eight pixels a unit of scale, as the bitmap face was -- so a
// 64 px face drawn at scale 2 comes down by 16/bakedLine_.
float Overlay::faceScale(float scale) const {
    return bakedLine_ > 0.0f ? lineHeight(scale) / bakedLine_ : 0.0f;
}

float Overlay::measure(float scale, const std::string& s) const {
    if (!haveFace_) return float(s.size()) * kAdvance * scale;
    const float f = faceScale(scale);
    float width = 0.0f;
    for (char c : s) {
        const int code = int(static_cast<unsigned char>(c));
        if (code < kFirstCode || code > kLastCode) continue;
        width += glyphs_[size_t(code - kFirstCode)].advance * f;
    }
    return width * 1.0f;
}

float Overlay::text(float x, float y, float scale, uint32_t abgr, const std::string& s) {
    // The shadow first, so the ink lands on top of it. Both passes walk the whole string
    // rather than interleaving per glyph: a glyph's own shadow must not sit over its
    // neighbour's ink, which is what one pass of (shadow, ink) per character gives.
    constexpr uint32_t kShadow = 0xC0000000u;  // abgr: three quarters opaque, black
    if (haveFace_) {
        const float f = faceScale(scale);
        // A pixel, whatever the size: the shadow is there to separate the ink from grass,
        // and a shadow that grows with the text reads as a second, blurred copy of it.
        const float offset = 1.0f;
        for (int pass = 0; pass < 2; ++pass) {
            const uint32_t colour = pass == 0 ? kShadow : abgr;
            const float d = pass == 0 ? offset : 0.0f;
            float pen = x + d;
            // `y` is the top of the line box for every caller of this, as it was with the
            // bitmap; the face draws from a BASELINE, which is the ascent below that.
            const float baseline = y + bakedAscent_ * f + d;
            for (char c : s) {
                const int code = int(static_cast<unsigned char>(c));
                if (code < kFirstCode || code > kLastCode) continue;
                const FaceGlyph& g = glyphs_[size_t(code - kFirstCode)];
                if (g.x1 > g.x0 && g.y1 > g.y0) {
                    quad(pen + g.x0 * f, baseline + g.y0 * f, (g.x1 - g.x0) * f,
                         (g.y1 - g.y0) * f, g.u0, g.v0, g.u1, g.v1, colour);
                }
                pen += g.advance * f;
            }
        }
        return measure(scale, s);
    }

    const float atlasW = float(kCols * kCellW);
    const float atlasH = float(kRows * kCellH);
    const float w = float(kGlyphW) * scale;
    const float h = float(kGlyphH) * scale;
    const float offset = scale;
    for (int pass = 0; pass < 2; ++pass) {
        const uint32_t colour = pass == 0 ? kShadow : abgr;
        const float dx = pass == 0 ? offset : 0.0f;
        float pen = x + dx;
        for (char c : s) {
            const int cell = cellFor(c);
            if (cell >= 0) {
                const float ox = float((cell % kCols) * kCellW);
                const float oy = float((cell / kCols) * kCellH);
                quad(pen, y + dx, w, h, ox / atlasW, oy / atlasH,
                     (ox + float(kGlyphW)) / atlasW, (oy + float(kGlyphH)) / atlasH, colour);
            }
            pen += kAdvance * scale;
        }
    }
    return float(s.size()) * kAdvance * scale;
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
