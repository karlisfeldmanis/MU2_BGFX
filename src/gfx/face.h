// The typeface, baked once into a coverage atlas: MU2's own Open Sans SemiBold.
//
// Two things draw text and they want the face differently. The viewer's overlay wants a small
// R8 atlas at one size; the windows want the same glyphs at a dozen sizes, from a 13 px tip to
// a 20 px title, which is minification the atlas has to carry a mip chain for. So the bake is
// here and each owner uploads what it bakes the way it needs it.
//
// Sizes come in two kinds and both are here on purpose. `lineHeight` is the overlay's own
// contract -- ascent to descent in pixels. `em` sizes are Godot's: MU2 draws every figure with
// `fontSize: N`, which is the em in pixels, and the windows' tables are written in those
// numbers, so a size copied out of Hud.cs means here what it meant there.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::gfx {

// One glyph of the baked face: where it sits in the atlas and where it sits on the line, in
// the pixels the face was baked at. stb_truetype's packedchar in this project's own terms, so
// that no header of ours drags stb into everything that draws a label.
struct FaceGlyph {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float advance = 0.0f;
};

class Face {
public:
    static constexpr int kFirstCode = 32;
    static constexpr int kLastCode = 126;
    // The one glyph past ASCII: the middle dot, U+00B7, written as its Latin-1 byte "\xB7".
    // The arrival's "TOWN \xB7 SAFE ZONE" is the only thing that says it.
    static constexpr int kMiddleDot = 0xB7;

    // Rasterises the face at `pixels` of line height into a `size`-square R8 buffer, leaving
    // `padding` texels round every glyph. A mip chain wants more than one: a glyph with one
    // texel of air meets its neighbour two levels down. False, with the reason in the log,
    // when the file is missing or will not pack.
    // `oversample` is texels per pixel each way; a title baked larger than it is ever drawn
    // needs none. `margin` keeps the atlas's own edge clear, which a blurred bake spreads into.
    bool bake(const std::string& path, float pixels, int size, int padding, int oversample = 2,
              int margin = 0);
    // Blurs the baked coverage by a Gaussian of `sigma` baked pixels, for a halo: the text-shadow
    // blur a browser draws, in a texture. Every glyph's quad then grows by `spread()` so the blur
    // is drawn whole, which is why the bake wants `margin` and half its `padding` at least that.
    void blur(float sigma);
    // How far past its own box a glyph is drawn, in baked pixels. Nought until blurred.
    float spread() const { return spreadTexels_ / float(oversample_); }
    float spreadUv() const { return size_ > 0 ? spreadTexels_ / float(size_) : 0.0f; }

    bool ready() const { return !glyphs_.empty(); }
    int size() const { return size_; }
    const std::vector<uint8_t>& pixels() const { return pixels_; }
    // Frees the baked coverage once it is uploaded. The glyphs stay.
    void dropPixels() { std::vector<uint8_t>().swap(pixels_); }

    // A glyph, or null for anything outside the printable ASCII (and the middle dot) this bakes.
    const FaceGlyph* glyph(char c) const {
        const int code = int(static_cast<unsigned char>(c));
        if (glyphs_.empty()) return nullptr;
        if (code == kMiddleDot) return &glyphs_.back();
        if (code < kFirstCode || code > kLastCode) return nullptr;
        return &glyphs_[size_t(code - kFirstCode)];
    }

    // Where the solid texel is, in uv: a filled rectangle is a quad sampling it, so a panel and
    // a letter are the same draw against the same texture.
    float solidU() const { return solidU_; }
    float solidV() const { return solidV_; }

    // The baked line box and its ascent, in baked pixels.
    float bakedLine() const { return bakedLine_; }
    float bakedAscent() const { return bakedAscent_; }

    // How much a baked glyph is scaled to draw at a Godot font size -- the em in pixels.
    float emScale(float fontSize) const { return bakedEm_ > 0.0f ? fontSize / bakedEm_ : 0.0f; }
    // Godot's own three at a font size, which MU2's centring arithmetic is written against:
    // `(box - ascent - descent) / 2 + ascent` is where a baseline goes in a box.
    float ascent(float fontSize) const { return bakedAscent_ * emScale(fontSize); }
    float descent(float fontSize) const { return (bakedLine_ - bakedAscent_) * emScale(fontSize); }
    float height(float fontSize) const { return bakedLine_ * emScale(fontSize); }
    // How wide a string is at a font size. Proportional, so it has to be asked.
    float measure(float fontSize, const std::string& s) const;

private:
    std::vector<FaceGlyph> glyphs_;
    std::vector<uint8_t> pixels_;
    int size_ = 0;
    float bakedLine_ = 0.0f;
    float bakedAscent_ = 0.0f;
    float bakedEm_ = 0.0f;
    float solidU_ = 0.0f, solidV_ = 0.0f;
    int oversample_ = 2;
    int spreadTexels_ = 0;
};

// Where the face is on disk: extern/, fetched and pinned by bootstrap.sh.
const char* facePath();
// The arrival's title face, Cinzel Medium, from the same place.
const char* titleFacePath();
// The fight's figures, Barlow Semi Condensed Bold: narrow, because an area skill puts five
// numbers across one tile. Also from extern/, also pinned.
const char* figureFacePath();

}  // namespace mu::gfx
