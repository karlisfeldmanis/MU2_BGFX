#include "gfx/face.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::gfx {
namespace {

// MU2's by inheritance: MU2 sets no theme font in either of its Godot projects, so every label
// it draws is Godot's fallback, Open Sans SemiBold. bootstrap.sh fetches the same file, pinned.
//
// Read from extern/ at run time rather than compiled in. It is 150 kB, it is the one asset gfx
// reads that is not the game's own, and a header holding it as a byte array is a header nobody
// can read. The cost is that it can be missing, and every owner has a fallback for that.
constexpr const char* kFacePath = MU2_ROOT_DIR "/extern/OpenSans-SemiBold.ttf";
// The arrival's map name: Cinzel Medium, Google Fonts' static instance, which is what the
// design page the user chose on 2026-09-22 was drawn in. bootstrap.sh fetches it.
constexpr const char* kTitleFacePath = MU2_ROOT_DIR "/extern/Cinzel-Medium.ttf";
// The damage figures: Barlow Semi Condensed Bold, the face the design page of 2026-09-23 was
// drawn in (concept B2). Chosen for its width and not its style -- a Cyclone puts five figures
// across one tile and a normal-width face has them touching. bootstrap.sh fetches it.
constexpr const char* kFigureFacePath = MU2_ROOT_DIR "/extern/BarlowSemiCondensed-Bold.ttf";

// The rows at the bottom kept out of the packing, for the solid texel a panel draws with. A
// whole band rather than one texel because the face is sampled bilinear, and a solid texel
// beside a packed glyph would bleed that glyph's edge into every panel.
constexpr int kSolidRows = 12;

}  // namespace

const char* facePath() { return kFacePath; }
const char* titleFacePath() { return kTitleFacePath; }
const char* figureFacePath() { return kFigureFacePath; }

bool Face::bake(const std::string& path, float pixels, int size, int padding, int oversample,
                int margin) {
    const std::vector<uint8_t> ttf = core::readFile(path);
    if (ttf.empty()) {
        core::logError("no face at %s. ./bootstrap.sh fetches it", path.c_str());
        return false;
    }
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        core::logError("%s is not a font this can read", path.c_str());
        return false;
    }

    size_ = size;
    oversample_ = oversample;
    spreadTexels_ = 0;
    pixels_.assign(size_t(size) * size_t(size), 0);
    stbtt_pack_context pack;
    // Everything but the solid band at the bottom and the margin, told the full stride. The
    // coordinates that come back are the packed area's, and the margin is added to them below.
    uint8_t* origin = pixels_.data() + size_t(margin) * size_t(size) + size_t(margin);
    if (!stbtt_PackBegin(&pack, origin, size - margin, size - kSolidRows - margin, size, padding,
                         nullptr)) {
        core::logError("the face would not pack into %dx%d", size, size - kSolidRows);
        return false;
    }
    // Two by two for the windows. Nothing drawn with this face is pixel-aligned, and the
    // horizontal oversample is what keeps a stem from thinning to nothing between one label
    // and the next.
    stbtt_PackSetOversampling(&pack, unsigned(oversample), unsigned(oversample));
    const int ascii = kLastCode - kFirstCode + 1;
    const int count = ascii + 1;  // and the middle dot, last
    // Resized rather than constructed with a size: `vector<T> packed(size_t(count))` is a
    // function declaration, and the compiler then says the packing does not take a vector.
    std::vector<stbtt_packedchar> packed;
    packed.resize(size_t(count));
    stbtt_pack_range ranges[2] = {};
    ranges[0].font_size = pixels;
    ranges[0].first_unicode_codepoint_in_range = kFirstCode;
    ranges[0].num_chars = ascii;
    ranges[0].chardata_for_range = packed.data();
    ranges[1].font_size = pixels;
    ranges[1].first_unicode_codepoint_in_range = kMiddleDot;
    ranges[1].num_chars = 1;
    ranges[1].chardata_for_range = packed.data() + ascii;
    const int ok = stbtt_PackFontRanges(&pack, ttf.data(), 0, ranges, 2);
    stbtt_PackEnd(&pack);
    if (!ok) {
        core::logError("the face packed short; the atlas is too small for %d glyphs", count);
        return false;
    }

    // The face's own line box, asked of the font rather than derived from the glyphs: a line
    // of "aces" and a line of "Qgjy" must sit on the same baseline and take the same height.
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    const float toPixels = stbtt_ScaleForPixelHeight(&info, pixels);
    bakedAscent_ = float(ascent) * toPixels;
    bakedLine_ = float(ascent - descent) * toPixels;
    // The em at this bake, which is what a Godot font size is: ScaleForMappingEmToPixels(1) is
    // one over the units per em.
    bakedEm_ = toPixels / stbtt_ScaleForMappingEmToPixels(&info, 1.0f);

    glyphs_.assign(size_t(count), FaceGlyph{});
    for (int i = 0; i < count; ++i) {
        float penX = 0.0f, penY = 0.0f;
        stbtt_aligned_quad q;
        // align_to_integer 0: the quad is scaled by the caller, so rounding it to whole pixels
        // HERE rounds at the wrong size and the spacing comes out uneven.
        stbtt_GetPackedQuad(packed.data(), size, size, i, &penX, &penY, &q, 0);
        FaceGlyph& g = glyphs_[size_t(i)];
        const float shift = float(margin) / float(size);
        g.u0 = q.s0 + shift; g.v0 = q.t0 + shift; g.u1 = q.s1 + shift; g.v1 = q.t1 + shift;
        g.x0 = q.x0; g.y0 = q.y0; g.x1 = q.x1; g.y1 = q.y1;
        g.advance = packed[size_t(i)].xadvance;
    }

    // The solid band, inset from the packed rows and from the edge so that bilinear sampling of
    // its middle can only ever find more of itself -- down two mip levels, for the windows.
    for (int y = size - kSolidRows + 2; y < size - 2; ++y) {
        for (int x = 2; x < 14; ++x) pixels_[size_t(y) * size_t(size) + size_t(x)] = 0xFF;
    }
    solidU_ = 8.0f / float(size);
    solidV_ = (float(size) - float(kSolidRows) * 0.5f) / float(size);
    core::logf("face: %s, %d glyphs baked at %.0f px, line %.1f px, em %.1f px", path.c_str(),
               count, double(pixels), double(bakedLine_), double(bakedEm_));
    return true;
}

void Face::blur(float sigma) {
    if (pixels_.empty() || sigma <= 0.0f) return;
    const float s = sigma * float(oversample_);
    const int radius = int(std::ceil(s * 3.0f));
    std::vector<float> kernel(size_t(radius) * 2 + 1);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        kernel[size_t(i + radius)] = std::exp(-0.5f * float(i * i) / (s * s));
        sum += kernel[size_t(i + radius)];
    }
    for (float& k : kernel) k /= sum;
    // Separable, in floats, rows then columns. The atlas is at most 512 square for a halo and
    // this runs once at load.
    const int n = size_;
    std::vector<float> a(pixels_.begin(), pixels_.end()), b(a.size());
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            float v = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                const int at = x + i;
                if (at >= 0 && at < n) v += a[size_t(y) * n + at] * kernel[size_t(i + radius)];
            }
            b[size_t(y) * n + x] = v;
        }
    }
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            float v = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                const int at = y + i;
                if (at >= 0 && at < n) v += b[size_t(at) * n + x] * kernel[size_t(i + radius)];
            }
            pixels_[size_t(y) * n + x] = uint8_t(std::min(255.0f, v + 0.5f));
        }
    }
    spreadTexels_ = radius;
}

float Face::measure(float fontSize, const std::string& s) const {
    const float f = emScale(fontSize);
    float width = 0.0f;
    for (char c : s) {
        if (const FaceGlyph* g = glyph(c)) width += g->advance * f;
    }
    return width;
}

}  // namespace mu::gfx
