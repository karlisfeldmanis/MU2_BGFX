#include "game/ui/panel.h"

#include <algorithm>
#include <cstdio>

#include "core/json.h"
#include "core/log.h"

namespace mu::game::panel {

// ---- the art -------------------------------------------------------------------------------

bool Arts::open(const std::string& assetDir, content::Textures* textures) {
    assetDir_ = assetDir;
    textures_ = textures;
    const core::Json index = core::parseJsonFile(assetDir + "/index.json");
    const core::Json& effects = index["effects"];
    for (const auto& [key, path] : effects.members) {
        // Only the interface's: the effects table also names every spell sheet, and those are
        // the showing's to load, as the showing wants them.
        if (path.type == core::Json::Type::String && path.string.rfind("interface/", 0) == 0) {
            paths_[key] = path.string;
        }
    }
    core::logf("interface: %zu pieces of art in the index", paths_.size());
    return !paths_.empty();
}

const gfx::Art& Arts::get(const std::string& key) {
    auto found = loaded_.find(key);
    if (found != loaded_.end()) return found->second;
    gfx::Art art;
    auto path = paths_.find(key);
    if (path == paths_.end() || textures_ == nullptr) {
        core::logError("interface art '%s' is not in the build; drawing without it", key.c_str());
    } else {
        art.handle = textures_->load(assetDir_ + "/" + path->second,
                                     content::TextureRole::Interface);
        uint32_t w = 0, h = 0;
        if (textures_->sizeOf(art.handle, &w, &h)) {
            art.width = float(w);
            art.height = float(h);
        }
    }
    return loaded_.emplace(key, art).first->second;
}

// ---- the two frames ------------------------------------------------------------------------

Screen screenOf(float width, float height) {
    Screen s;
    s.scale = std::max(height / kReferenceHeight, 0.5f);
    s.originX = (width - kReferenceWidth * s.scale) * 0.5f;
    s.originY = height - kReferenceHeight * s.scale;
    return s;
}

namespace {
float s_scale = 2.0f;
}

void setScreen(float height) { s_scale = 2.0f * std::max(height, 540.0f) / 1080.0f; }
float scale() { return s_scale; }

float columnX(float screenWidth, int column) {
    return screenWidth - kWidth * scale() * float(column) - kRightMargin;
}

float panelY(float screenHeight) { return (screenHeight - kHeight * scale()) * 0.5f; }

// ---- the frame -------------------------------------------------------------------------------

gfx::Box headSocket(bool right) {
    return {right ? kWidth - kHeadInset - kHeadButton : kHeadInset,
            kPlateTop + (kPlateHeight - kHeadButton) * 0.5f + kHeadDrop, kHeadButton,
            kHeadButton};
}

float centredBaseline(const gfx::Face& face, const gfx::Box& box, float fontSize) {
    return box.y + (box.h - face.ascent(fontSize) - face.descent(fontSize)) * 0.5f +
           face.ascent(fontSize);
}

gfx::Box buttonState(const gfx::Art& art, bool pressed, int states) {
    const float tall = art.height / float(states);
    return {0.0f, pressed ? tall : 0.0f, art.width, tall};
}

void frame(gfx::Canvas& canvas, Arts& arts, float x, float y, const std::string& title) {
    // The leather from the plate's top down; the strip above it is the crest's, with the world
    // behind it. `Panel.Frame`, one texture nine-sliced at cut time.
    canvas.image(arts.get("bag_back"), scaled(x, y, {0.0f, kPlateTop, kWidth, kHeight - kPlateTop}));
    canvas.image(arts.get("bag_plate"), scaled(x, y, {0.0f, kPlateTop, kWidth, kPlateHeight}));
    const gfx::Art& crest = arts.get("bag_crest");
    if (crest.valid()) {
        // At its own width and centred, not stretched: it is a carving with a middle.
        const float wide = crest.width / scale(), tall = crest.height / scale();
        canvas.image(crest, scaled(x, y, {(kWidth - wide) * 0.5f, 0.0f, wide, tall}));
    }
    // The name, centred across the whole window and down the plate.
    const float size = kTitleSize * scale();
    const gfx::Box plate = scaled(x, y, {0.0f, kPlateTop, kWidth, kPlateHeight});
    canvas.text(x, centredBaseline(canvas.face(), plate, size), size, kLettering, title,
                gfx::Align::Centre, kWidth * scale());
}

void close(gfx::Canvas& canvas, Arts& arts, float x, float y, bool pressed) {
    const gfx::Art& cross = arts.get("bag_close");
    const gfx::Box box = scaled(x, y, frameClose());
    if (cross.valid()) {
        canvas.region(cross, box, buttonState(cross, pressed));
    } else if (pressed) {
        canvas.rect(box, gfx::rgba(0.0f, 0.0f, 0.0f, 0.45f));
    }
}

void field(gfx::Canvas& canvas, Arts& arts, float x, float y, const gfx::Box& units,
           const char* key) {
    const gfx::Art& art = arts.get(key);
    if (!art.valid()) return;
    // Nine draws, because a border stretches with the thing it borders: the art is cut at one
    // size and asked for at several. window_cut.py's 12 is the border's thickness in the art.
    constexpr float kInset = 12.0f;
    const gfx::Box target = scaled(x, y, units);
    const float ix = std::min(kInset, target.w * 0.5f), iy = std::min(kInset, target.h * 0.5f);
    const float cutX[4] = {0.0f, kInset, art.width - kInset, art.width};
    const float cutY[4] = {0.0f, kInset, art.height - kInset, art.height};
    const float putX[4] = {target.x, target.x + ix, target.right() - ix, target.right()};
    const float putY[4] = {target.y, target.y + iy, target.bottom() - iy, target.bottom()};
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            const gfx::Box into{putX[c], putY[r], putX[c + 1] - putX[c], putY[r + 1] - putY[r]};
            if (into.w <= 0.0f || into.h <= 0.0f) continue;
            canvas.region(art, into,
                          {cutX[c], cutY[r], cutX[c + 1] - cutX[c], cutY[r + 1] - cutY[r]});
        }
    }
}

// ---- words -----------------------------------------------------------------------------------

namespace {
std::string groupedBy(long long value, char by) {
    char digits[32];
    std::snprintf(digits, sizeof digits, "%lld", value < 0 ? -value : value);
    std::string out;
    const int n = int(std::char_traits<char>::length(digits));
    for (int i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out += by;
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}
}  // namespace

std::string grouped(long long value) { return groupedBy(value, ' '); }
std::string commas(long long value) { return groupedBy(value, ','); }

void tooltip(gfx::Canvas& canvas, float x, float y, const std::vector<Line>& lines,
             float fontSize, float screenWidth, float screenHeight) {
    if (lines.empty()) return;
    const gfx::Face& face = canvas.face();
    float width = 0.0f;
    for (const Line& line : lines) {
        width = std::max(width, face.measure(line.bold ? fontSize + 1.0f : fontSize, line.text));
    }
    // RenderTipTextList's two numbers: `fWidth += 4` at a twelve-point font, two a side, which
    // is a sixth of a size; and a tenth of leading between the lines and no margin round them.
    const float pad = fontSize / 6.0f;
    const float lineTall = face.height(fontSize);
    const float spacing = lineTall * 1.1f;
    const float w = width + pad * 2.0f, h = float(lines.size()) * spacing;
    constexpr float kMargin = 4.0f;
    // Centred on the point and standing on it: `iPos_x = sx - fWidth / 2`, STRP_BOTTOMCENTER.
    float ox = std::clamp(x - w * 0.5f, kMargin, std::max(kMargin, screenWidth - kMargin - w));
    float oy = std::clamp(y - h, kMargin, std::max(kMargin, screenHeight - kMargin - h));
    const gfx::Box box{ox, oy, w, h};
    canvas.rect(box, gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f));
    canvas.outline(box, 1.0f, gfx::rgba(0.0f, 0.0f, 0.0f, 1.0f));
    canvas.outline(box.grown(-1.0f), 1.0f, gfx::rgba(0.62f, 0.52f, 0.34f, 0.35f));
    float pen = oy + face.ascent(fontSize) + (spacing - lineTall) * 0.5f;
    for (const Line& line : lines) {
        canvas.text(ox + pad, pen, line.bold ? fontSize + 1.0f : fontSize, line.colour,
                    line.text, gfx::Align::Centre, width);
        pen += spacing;
    }
}

}  // namespace mu::game::panel
