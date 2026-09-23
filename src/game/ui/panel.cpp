#include "game/ui/panel.h"

#include <algorithm>
#include <cstdio>

#include "core/json.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"
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

int Arts::warm() {
    int read = 0;
    for (const auto& [key, path] : paths_) {
        (void)path;
        if (get(key).valid()) ++read;
    }
    core::logf("interface: %d pieces of art read ahead of the windows", read);
    return read;
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
    const float k = scale();
    return screenWidth - kRightMargin - kWidth * k * float(column) -
           kColumnGap * k * float(column - 1);
}

float panelY(float screenHeight) { return (screenHeight - kHeight * scale()) * 0.5f; }

// ---- the head's face -------------------------------------------------------------------------

namespace {
gfx::Face s_title;
bgfx::TextureHandle s_titleTexture = BGFX_INVALID_HANDLE;
// Baked at twice the size it is drawn at 1080 lines, as the arrival bakes its own: a title
// minified from a larger bake holds its edge on a retina backbuffer, and one magnified does not.
constexpr float kTitleBake = 48.0f;
}  // namespace

bool openTitleFace(const gfx::Interface& interface) {
    (void)interface;
    if (!s_title.bake(gfx::titleFacePath(), kTitleBake, 512, 4, 1, 0)) {
        core::logError("interface: the title face did not bake; the windows keep the body face");
        return false;
    }
    s_titleTexture = gfx::uploadFace(s_title, "window titles");
    s_title.dropPixels();
    return bgfx::isValid(s_titleTexture);
}

void closeTitleFace() {
    if (bgfx::isValid(s_titleTexture)) bgfx::destroy(s_titleTexture);
    s_titleTexture = BGFX_INVALID_HANDLE;
    s_title = gfx::Face{};
}

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
    // **No leather, no plate, no crest.** The window is the card's material now: a shadow, a
    // nearly flat near-black body, and a gradient hairline for its edge -- bright along the head
    // and fading to almost nothing at the foot, which is what makes a flat panel read as lit.
    // The user, 2026-09-23: *"the original window, just a really nice skin ... very clean, flat,
    // modern, Diablo 4 style"*, and then: drop shadows, gradient stroke, clean UI.
    //
    // MU's own rectangles are untouched -- the grid still starts at (15, 200) and the Zen strip
    // still sits at 380 -- because this repaints `Panel`, which is the one place all three
    // windows come through. `arts` is still taken: the worn slots' ghosts and the Zen coin are
    // MU's art and stay.
    (void)arts;
    const float k = scale();
    const gfx::Box window = scaled(x, y, {0.0f, 0.0f, kWidth, kHeight});
    sheet::glass(canvas, window, kRadius * k);

    // The head: a band of light under it, a mark, the name in tracked caps, and a rule.
    const gfx::Box head = scaled(x, y, {0.0f, 0.0f, kWidth, kHeadBand});
    sheet::band(canvas, head);
    const float size = kTitleSize * k;
    // Centred in the WHOLE head, not in MU's plate. The plate was a painted strip from 8 to 36
    // with a carving above it; with the carving gone the eight units above are the head's own
    // air, and a title centred in the strip alone sits visibly low in the band it is drawn on.
    const gfx::Box band = scaled(x, y, {0.0f, 0.0f, kWidth, kHeadBand});
    const float baseline = centredBaseline(canvas.face(), band, size);
    // **The title is fitted to the room between the mark and the cross.** A merchant's own name
    // is the title of his window and `Lumen the Barmaid` is nineteen tracked capitals, which ran
    // straight under the cross. It is shrunk by a quarter before anything is cut, and only then
    // trimmed -- a name shortened by a letter still reads, a name under a button does not.
    // Cinzel where it baked, the interface's own face where it did not.
    const bool gothic = s_title.ready() && bgfx::isValid(s_titleTexture);
    const gfx::Face& face = gothic ? s_title : canvas.face();
    const float room = (frameClose().x - 5.0f - kTitleX) * k;
    const std::string whole = sheet::shouted(title);
    float fitted = size;
    while (fitted > size * 0.72f &&
           tip::trackedWidth(face, fitted, sheet::kTitleTrack, whole) > room) {
        fitted -= 0.5f;
    }
    // And only then trimmed, in ONE pass. It was written as a loop that popped a letter and put
    // the two dots back inside the same condition, which for a name of exactly the wrong length
    // alternates between too long and short enough for ever: the frame never returned and the
    // game froze the moment a merchant's counter opened. A trim measures the string it is going
    // to draw, and never grows.
    std::string text = whole;
    while (text.size() > 1 &&
           tip::trackedWidth(face, fitted, sheet::kTitleTrack, text + "..") > room) {
        text.pop_back();
    }
    if (text.size() != whole.size()) text += "..";
    if (gothic) {
        // `lettered` takes the tracking in pixels, not in ems, and draws its own drop first so
        // the title holds an edge over whatever the window is lying on.
        const float tracking = fitted * sheet::kTitleTrack;
        canvas.lettered(s_title, s_titleTexture, x + kTitleX * k + 1.0f, baseline + 1.0f, fitted,
                        tracking, tip::ink::kDrop, text);
        canvas.lettered(s_title, s_titleTexture, x + kTitleX * k, baseline, fitted, tracking,
                        sheet::ink::kTitle, text);
    } else {
        sheet::kicker(canvas, x + kTitleX * k, baseline, fitted, text, sheet::ink::kTitle,
                      sheet::kTitleTrack);
    }
    sheet::rule(canvas, window.x + kEdge * k, y + kHeadBand * k, (kWidth - kEdge * 2.0f) * k,
                std::max(1.0f, k * 0.5f));
}

void close(gfx::Canvas& canvas, Arts& arts, float x, float y, bool pressed) {
    (void)arts;  // MU's two-state button art is not drawn any more; the cross is.
    sheet::close(canvas, scaled(x, y, frameClose()), false, pressed);
}

void close(gfx::Canvas& canvas, float x, float y, bool over, bool pressed) {
    sheet::close(canvas, scaled(x, y, frameClose()), over, pressed);
}

void field(gfx::Canvas& canvas, Arts& arts, float x, float y, const gfx::Box& units,
           const char* key) {
    // The leather's nine-sliced well becomes the card's framed block: 3% white under a 10%
    // hairline, which is what the card itself frames a section with.
    (void)arts;
    (void)key;
    sheet::well(canvas, scaled(x, y, units), std::max(1.0f, scale() * 0.5f));
}

void cell(gfx::Canvas& canvas, float x, float y, const gfx::Box& units, sheet::Cell state) {
    sheet::cell(canvas, scaled(x, y, units), state, std::max(1.0f, scale() * 0.5f));
}

namespace {
// MU's (11, 364, 170, 26) Zen strip, moved down to the foot and centred in it; the coins at its
// left at MU's own distance, and the figure against the value edge every window ranges to.
constexpr gfx::Box kMoneyStrip{kEdge, kFootTop, kWidth - kEdge * 2.0f, 26.0f};
constexpr gfx::Box kMoneyIcon{18.0f, kFootTop + 4.0f, 20.0f, 18.0f};
constexpr float kMoneyFrom = 18.0f + 20.0f + 6.0f;
constexpr float kMoneySize = 9.5f;
}  // namespace

void zenFoot(gfx::Canvas& canvas, Arts& arts, float x, float y, long long money) {
    const float k = scale();
    const gfx::Face& face = canvas.face();
    sheet::band(canvas, scaled(x, y, {0.0f, kFootRule, kWidth, kHeight - kFootRule}), false);
    sheet::rule(canvas, x + kEdge * k, y + kFootRule * k, (kWidth - kEdge * 2.0f) * k,
                std::max(1.0f, k * 0.5f));
    canvas.image(arts.get("bag_zen"), scaled(x, y, kMoneyIcon));
    const gfx::Box strip = scaled(x, y, kMoneyStrip);
    sheet::kicker(canvas, x + kMoneyFrom * k, centredBaseline(face, strip, 8.0f * k), 8.0f * k,
                  "ZEN");
    const float size = kMoneySize * k;
    sheet::ranged(canvas, x + kValueRight * k, centredBaseline(face, strip, size), size,
                  moneyColour(money), commas(money));
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
