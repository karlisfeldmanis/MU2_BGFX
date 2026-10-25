#include "gfx/glyphs.h"

#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "core/files.h"
#include "core/log.h"

namespace mu::gfx::glyphs {
namespace {

// 2048 square in 64-texel cells: 1024 letters, more than a session's chat in any one script
// but Chinese, where a long evening may fill it; then a new letter is left out, and logged.
constexpr int kSize = 2048;
constexpr int kCell = 64;
constexpr int kAcross = kSize / kCell;
// The air round a letter in its cell, which its mips (a 64 cell is 4 texels two levels down)
// sample into rather than the next cell. A letter taller than the rest is drawn smaller.
constexpr int kPad = 4;
// Levels written per cell: down to one texel. Below that a letter is not readable anyway.
constexpr int kLevels = 7;

struct Font {
    std::vector<uint8_t> bytes;
    stbtt_fontinfo info{};
    bool ok = false;
};

struct Cell {
    int x = 0, y = 0;
    std::vector<uint8_t> alpha;  // kCell square
};

std::mutex gLock;
std::map<std::string, std::unique_ptr<Font>> gFonts;
std::vector<std::unique_ptr<Font>> gFallbacks;
bool gFallbacksLoaded = false;
// What each face's file was asked for, found or not, so a miss is not looked for again.
std::map<std::pair<std::string, uint32_t>, std::pair<bool, FaceGlyph>> gFound;
std::vector<Cell> gWaiting;
int gUsed = 0;
bool gToldFull = false;
bgfx::TextureHandle gTexture = BGFX_INVALID_HANDLE;

bool load(Font& font, const std::string& path, int index) {
    if (!core::fileExists(path)) return false;
    font.bytes = core::readFile(path);
    if (font.bytes.empty()) return false;
    const int offset = stbtt_GetFontOffsetForIndex(font.bytes.data(), index);
    font.ok = offset >= 0 && stbtt_InitFont(&font.info, font.bytes.data(), offset) != 0;
    if (!font.ok) font.bytes.clear();
    return font.ok;
}

// The system's fonts for what the game's faces have not: the CJK scripts above all. Read only
// when a letter misses its own face, and kept.
void loadFallbacks() {
    gFallbacksLoaded = true;
    std::vector<std::pair<std::string, int>> files;
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::string dir = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    for (const char* f : {"msyh.ttc", "YuGothM.ttc", "meiryo.ttc", "malgun.ttf", "simsun.ttc",
                          "tahoma.ttf", "seguisym.ttf"}) {
        files.push_back({dir + f, 0});
    }
#elif defined(__APPLE__)
    files = {{"/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 0},
             {"/System/Library/Fonts/Hiragino Sans GB.ttc", 0},
             {"/System/Library/Fonts/AppleSDGothicNeo.ttc", 0},
             {"/System/Library/Fonts/Supplemental/Tahoma.ttf", 0}};
#else
    files = {{"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0},
             {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0},
             {"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 0},
             {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", 0},
             {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0}};
#endif
    for (const auto& [path, index] : files) {
        auto font = std::make_unique<Font>();
        if (!load(*font, path, index)) continue;
        core::logf("glyphs: fallback %s", path.c_str());
        gFallbacks.push_back(std::move(font));
    }
}

Font* fontOf(const std::string& path) {
    std::unique_ptr<Font>& font = gFonts[path];
    if (!font) {
        font = std::make_unique<Font>();
        load(*font, path, 0);
    }
    return font->ok ? font.get() : nullptr;
}

// The letter rasterised into the next cell, its quad in kEm pixels.
bool draw(const Font& font, int index, FaceGlyph* out) {
    if (gUsed >= kAcross * kAcross) {
        if (!gToldFull) core::logError("glyphs: the cache's %d letters are used; new ones are left out", gUsed);
        gToldFull = true;
        return false;
    }
    float scale = stbtt_ScaleForMappingEmToPixels(&font.info, kEm);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&font.info, index, scale, scale, &x0, &y0, &x1, &y1);
    const int room = kCell - 2 * kPad;
    const int most = std::max(x1 - x0, y1 - y0);
    int advance = 0, bearing = 0;
    stbtt_GetGlyphHMetrics(&font.info, index, &advance, &bearing);
    out->advance = float(advance) * scale;
    if (most > room) {
        scale *= float(room) / float(most);
        stbtt_GetGlyphBitmapBox(&font.info, index, scale, scale, &x0, &y0, &x1, &y1);
    }
    const int w = std::min(room, x1 - x0), h = std::min(room, y1 - y0);
    out->x0 = float(x0), out->y0 = float(y0), out->x1 = float(x0 + w), out->y1 = float(y0 + h);
    if (w <= 0 || h <= 0) {  // a space: an advance and no cell
        out->u0 = out->v0 = out->u1 = out->v1 = 0.0f;
        return true;
    }
    Cell cell;
    cell.x = (gUsed % kAcross) * kCell;
    cell.y = (gUsed / kAcross) * kCell;
    ++gUsed;
    cell.alpha.assign(size_t(kCell) * kCell, 0);
    stbtt_MakeGlyphBitmap(&font.info, cell.alpha.data() + size_t(kPad) * kCell + kPad, w, h, kCell,
                          scale, scale, index);
    out->u0 = float(cell.x + kPad) / float(kSize);
    out->v0 = float(cell.y + kPad) / float(kSize);
    out->u1 = float(cell.x + kPad + w) / float(kSize);
    out->v1 = float(cell.y + kPad + h) / float(kSize);
    gWaiting.push_back(std::move(cell));
    return true;
}

}  // namespace

bool find(const std::string& facePath, uint32_t code, FaceGlyph* out) {
    std::lock_guard<std::mutex> hold(gLock);
    const auto key = std::make_pair(facePath, code);
    if (auto found = gFound.find(key); found != gFound.end()) {
        if (found->second.first) *out = found->second.second;
        return found->second.first;
    }
    FaceGlyph g;
    bool has = false, full = false;
    const auto tryFont = [&](const Font* font) {
        if (has || full || font == nullptr) return;
        const int index = stbtt_FindGlyphIndex(&font->info, int(code));
        if (index == 0) return;
        has = draw(*font, index, &g);
        full = !has;
    };
    tryFont(facePath.empty() ? nullptr : fontOf(facePath));
    if (!has && !full) {
        if (!gFallbacksLoaded) loadFallbacks();
        for (const auto& font : gFallbacks) tryFont(font.get());
    }
    if (full) return false;  // asked again, in case the cache is ever emptied
    gFound[key] = {has, g};
    if (has) *out = g;
    return has;
}

bgfx::TextureHandle texture() { return gTexture; }

void make() {
    if (bgfx::isValid(gTexture)) return;
    // No memory given: a texture made with its contents cannot be updated. A cell's every level
    // is written whole before anything samples it.
    gTexture = bgfx::createTexture2D(uint16_t(kSize), uint16_t(kSize), true, 1, bgfx::TextureFormat::RGBA8,
                                     BGFX_SAMPLER_UVW_CLAMP, nullptr);
    if (bgfx::isValid(gTexture)) bgfx::setName(gTexture, "glyph cache");
}

void upload() {
    std::vector<Cell> cells;
    {
        std::lock_guard<std::mutex> hold(gLock);
        cells.swap(gWaiting);
    }
    if (!bgfx::isValid(gTexture)) return;
    for (Cell& cell : cells) {
        // As uploadFace's: white, the coverage in alpha, box-filtered a level at a time.
        std::vector<uint8_t> alpha = std::move(cell.alpha);
        int n = kCell;
        for (int level = 0; level < kLevels && n >= 1; ++level) {
            const bgfx::Memory* mem = bgfx::alloc(uint32_t(n) * uint32_t(n) * 4);
            for (int i = 0; i < n * n; ++i) {
                uint8_t* p = mem->data + size_t(i) * 4;
                p[0] = p[1] = p[2] = 0xFF;
                p[3] = alpha[size_t(i)];
            }
            bgfx::updateTexture2D(gTexture, 0, uint8_t(level), uint16_t(cell.x >> level),
                                  uint16_t(cell.y >> level), uint16_t(n), uint16_t(n), mem);
            if (n == 1) break;
            const int m = n / 2;
            std::vector<uint8_t> half(size_t(m) * m);
            for (int y = 0; y < m; ++y) {
                for (int x = 0; x < m; ++x) {
                    const size_t a = size_t(y * 2) * n + size_t(x * 2);
                    half[size_t(y) * m + x] =
                        uint8_t((alpha[a] + alpha[a + 1] + alpha[a + n] + alpha[a + n + 1] + 2) / 4);
                }
            }
            alpha.swap(half);
            n = m;
        }
    }
}

void destroy() {
    if (bgfx::isValid(gTexture)) bgfx::destroy(gTexture);
    gTexture = BGFX_INVALID_HANDLE;
    std::lock_guard<std::mutex> hold(gLock);
    // The cells drawn into the texture go with it.
    gFound.clear();
    gWaiting.clear();
    gUsed = 0;
}

}  // namespace mu::gfx::glyphs
