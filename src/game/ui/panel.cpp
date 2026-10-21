#include "game/ui/panel.h"

#include <algorithm>
#include <string>
#include <cmath>
#include <cstdio>

#include "core/json.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"
#include "game/ui/style.h"
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
        // What's New's screenshots are a slide each, seen once an update: read when shown.
        if (key.rfind("whatsnew_", 0) == 0) continue;
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
float s_unit = 2.0f;
float s_floor = 0.0f;
float s_lines = 1080.0f;
float s_rightRoom = 0.0f;
}

void setScreen(float height) {
    const float lines = std::max(height, 540.0f);
    s_lines = lines;
    s_unit = 2.0f * lines / 1080.0f;
    s_scale = kScreenShare * lines / kHeight;
    s_floor = 0.0f;
}
void setFloor(float y) { s_floor = y; }
void setRightRoom(float units) { s_rightRoom = std::max(0.0f, units); }
float sideMargin() { return std::round(kSideShare * s_lines); }
float scale() { return s_scale; }
float unit() { return s_unit; }

float columnX(float screenWidth, int column) {
    const float k = scale();
    return screenWidth - sideMargin() - s_rightRoom * k - kWidth * k * float(column) -
           kColumnGap * k * float(column - 1);
}

// Centred over the HUD (panel.h), and never off the bottom of a short window. A floor under half
// the screen is a plate not laid out yet, and ignored.
float panelY(float screenHeight) {
    const float tall = kHeight * scale();
    const float lowest = std::max(0.0f, screenHeight - tall);
    if (s_floor < screenHeight * 0.5f) return std::min(kTopShare * screenHeight, lowest);
    return std::clamp(std::round((s_floor - tall) * 0.5f), 0.0f, lowest);
}

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

const gfx::Face* titleFace() {
    return s_title.ready() && bgfx::isValid(s_titleTexture) ? &s_title : nullptr;
}
bgfx::TextureHandle titleTexture() { return s_titleTexture; }

// ---- the frame -------------------------------------------------------------------------------

gfx::Box headSocket(bool right) {
    // Centred in the WHOLE head, as the title is: MU's socket sat in its painted plate, from 11
    // to 35, five units below the title's own centre line, and the cross read as hanging low.
    return {right ? kWidth - kHeadInset - kHeadButton : kHeadInset,
            (kHeadBand - kHeadButton) * 0.5f, kHeadButton, kHeadButton};
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
    // **Sanctuary** (game/ui/style.h), chosen by the user on 2026-09-27 over skin B: iron rings
    // round a stone body, the title centred in Cinzel over a quiet iron rule, clean corners. The
    // frame is game/ui/controls.h's, cut to MU's own head band so every rectangle below it --
    // the grid at (11, 200), the foot at 382 -- stays exactly where MU put it.
    (void)arts;
    const float k = scale();
    controls::frame(canvas, scaled(x, y, {0.0f, 0.0f, kWidth, kHeight}), tip::unit(), title,
                    nullptr, kHeadBand * k);
}

namespace {
// The close button in the head's right-hand socket, at the controls' own size: MU's 24-unit box
// is 41 pixels at 1080 lines, and the small square is 28.
gfx::Box closeIn(float x, float y) {
    const gfx::Box socket = scaled(x, y, frameClose());
    const float s = std::round(std::min(style::kSmallSquare * tip::unit(), socket.h));
    return {std::round(socket.right() - s), std::round(socket.midY() - s * 0.5f), s, s};
}
}  // namespace

void close(gfx::Canvas& canvas, Arts& arts, float x, float y, bool pressed) {
    (void)arts;
    close(canvas, x, y, false, pressed);
}

void close(gfx::Canvas& canvas, float x, float y, bool over, bool pressed) {
    controls::State state;
    state.lift = over ? 1.0f : 0.0f;
    state.held = pressed;
    controls::square(canvas, closeIn(x, y), controls::Glyph::Close, state, tip::unit());
}

void field(gfx::Canvas& canvas, Arts& arts, float x, float y, const gfx::Box& units,
           const char* key) {
    (void)arts;
    (void)key;
    controls::well(canvas, scaled(x, y, units), tip::unit());
}

void standing(gfx::Canvas& canvas, const gfx::Art& picture, float x, float y) {
    const float sx = picture.width / kWidth, sy = picture.height / kStageTall;
    canvas.region(picture, scaled(x, y, {0.0f, 0.0f, kWidth, kHeight}),
                  {0.0f, 0.0f, kWidth * sx, kHeight * sy});
}

namespace {

// A piece's sockets over its middle, in units: the item card's socket ring (tip::Mark::Ring), a
// bronze rim round the orange stone of a Rune of Creation or round a dark hole, stacked down the
// piece when it has two or three, as Diablo II sets its holes over the picture. As large as
// `kSocketStone` where the piece has the room, and smaller on a narrow one.
constexpr float kSocketStone = 7.5f;
// A socket is the jewel's own shape (the user, 2026-09-29: "border has to be same shape as
// jewel"): the Rune of Creation photographs as an upright lozenge this much narrower than it is
// tall, and the setting, the hole and the stone all follow it. `kRuneFills` is how much of its
// square the stage's picture of the rune is jewel, so the stone can be sized to its hole.
constexpr float kJewelAspect = 0.8f;
constexpr float kRuneFills = 0.81f;
constexpr float kSocketGap = 2.0f;

// Where a piece's sockets sit, in whatever space `box` is in: centres down its middle, one
// radius for all. The rings and their runes are laid by this one rule.
float socketsIn(const gfx::Box& box, int sockets, float unit, float* cy) {
    const float gap = kSocketGap * unit;
    const float r = std::min({kSocketStone * unit, box.w * 0.46f / kJewelAspect,
                              (box.h - gap * float(sockets + 1)) / float(sockets) * 0.5f});
    const float pitch = r * 2.0f + gap;
    const float top = box.midY() - pitch * float(sockets - 1) * 0.5f;
    for (int at = 0; at < sockets; ++at) cy[at] = top + pitch * float(at);
    return r;
}
constexpr uint32_t kSocketEmber = gfx::rgba(0.9f, 0.42f, 0.04f, 1.0f);
constexpr uint32_t kSocketGlint = gfx::rgba(1.0f, 0.78f, 0.45f, 0.85f);
constexpr uint32_t kSocketShade = gfx::rgba(0.0f, 0.0f, 0.0f, 0.7f);
constexpr uint32_t kSocketRim = gfx::rgba(0.62f, 0.45f, 0.24f, 1.0f);
constexpr uint32_t kSocketHole = gfx::rgba(0.05f, 0.035f, 0.03f, 0.82f);

// A lozenge the jewel's shape, `h` from its middle to its point.
void lozenge(gfx::Canvas& canvas, float cx, float cy, float h, uint32_t colour) {
    const float w = h * kJewelAspect;
    const float xy[8] = {cx, cy - h, cx + w, cy, cx, cy + h, cx - w, cy};
    canvas.polygon(nullptr, xy, nullptr, 4, colour);
}

// A filled circle, as a fan of sixteen: the canvas has rectangles and polygons, no round.
void disc(gfx::Canvas& canvas, float cx, float cy, float r, uint32_t colour) {
    float xy[32];
    for (int i = 0; i < 16; ++i) {
        const float a = float(i) * 6.2831853f / 16.0f;
        xy[i * 2] = cx + std::cos(a) * r;
        xy[i * 2 + 1] = cy + std::sin(a) * r;
    }
    canvas.polygon(nullptr, xy, nullptr, 16, colour);
}

}  // namespace

void sockets(gfx::Canvas& canvas, const gfx::Art& picture, const gfx::Box& box,
             const sim::Held& held, float k, float stageW, float stageH, const gfx::Box& rune) {
    const int count = std::min(socketsOf(held), kMostSockets);
    if (count == 0) return;
    float centres[kMostSockets];
    const float r = socketsIn(box, count, k, centres);
    for (int at = 0; at < count; ++at) {
        const float cx = box.midX(), cy = centres[at];
        lozenge(canvas, cx, cy, r + std::max(1.0f, 0.8f * k), kSocketShade);
        lozenge(canvas, cx, cy, r, kSocketRim);
        lozenge(canvas, cx, cy, r * 0.7f, kSocketHole);
        if (powerAt(held, at) == 0) continue;
        // The rune itself, cut from the strip and set in the hole at the hole's own size, so
        // the bronze shows all the way round it; the ember where there is no picture.
        if (picture.valid()) {
            const float sx = picture.width / stageW, sy = picture.height / stageH;
            const float side = r * 0.7f * 2.0f / kRuneFills;
            canvas.region(picture, {cx - side * 0.5f, cy - side * 0.5f, side, side},
                          {rune.x * sx, rune.y * sy, rune.w * sx, rune.h * sy});
        } else {
            disc(canvas, cx, cy + r * 0.03f, r * 0.54f, kSocketEmber);
            disc(canvas, cx - r * 0.18f, cy - r * 0.14f, r * 0.17f, kSocketGlint);
        }
    }
}

void carried(gfx::Canvas& canvas, const gfx::Art& picture, const gfx::Box& units, float px, float py) {
    const float k = scale();
    const gfx::Box to{px - units.w * k * 0.5f, py - units.h * k * 0.5f, units.w * k, units.h * k};
    if (!picture.valid()) {
        canvas.rect(to, gfx::rgba(0.68f, 0.60f, 0.40f, 0.5f));
        return;
    }
    // The picture alone, whole: the dark box once under it read as a background the thing was
    // carried on (the user, 2026-10-02).
    const float sx = picture.width / kWidth, sy = picture.height / kStageTall;
    const gfx::Box from = carryBox(units);
    canvas.region(picture, to, {from.x * sx, from.y * sy, from.w * sx, from.h * sy});
}

void carriedCount(gfx::Canvas& canvas, const gfx::Box& units, float px, float py, int count) {
    const float k = scale();
    const gfx::Box to{px - units.w * k * 0.5f, py - units.h * k * 0.5f, units.w * k, units.h * k};
    // At its lower left, where the pointer's own glyph does not sit over it.
    canvas.shadowed(to.x + 1.0f * k, to.bottom() - 2.0f * k, 8.0f * k, kLettering, gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f),
                    std::max(1.0f, 0.5f * k), std::to_string(count), gfx::Align::Left, to.w - 2.0f * k);
}

void cell(gfx::Canvas& canvas, float x, float y, const gfx::Box& units, sheet::Cell state) {
    controls::Cell to = controls::Cell::Rest;
    switch (state) {
        case sheet::Cell::Rest: to = controls::Cell::Rest; break;
        case sheet::Cell::Over: to = controls::Cell::Over; break;
        case sheet::Cell::Held: to = controls::Cell::Held; break;
        case sheet::Cell::Fits: to = controls::Cell::Fits; break;
        case sheet::Cell::Blocked: to = controls::Cell::Blocked; break;
    }
    controls::cell(canvas, scaled(x, y, units), to, tip::unit());
}

void grid(gfx::Canvas& canvas, float x, float y, float ux, float uy, int columns, int rows) {
    controls::grid(canvas,
                   scaled(x, y, {ux, uy, kPitch * float(columns), kPitch * float(rows)}), columns,
                   rows, tip::unit());
}

namespace {
// MU's (11, 364, 170, 26) Zen strip, moved down to the foot and centred in it; the coins at its
// left at MU's own distance, and the figure against the value edge every window ranges to.
constexpr gfx::Box kMoneyStrip{kEdge, kFootTop, kWidth - kEdge * 2.0f, 26.0f};
constexpr gfx::Box kMoneyIcon{18.0f, kFootTop + 4.0f, 20.0f, 18.0f};
constexpr float kMoneyFrom = 18.0f + 20.0f + 6.0f;
}  // namespace

void zenFoot(gfx::Canvas& canvas, Arts& arts, float x, float y, long long money,
             float valueRight) {
    const float k = scale(), u = tip::unit();
    controls::foot(canvas, scaled(x, y, {0.0f, 0.0f, kWidth, kHeight}), y + kFootRule * k, u);
    canvas.image(arts.get("bag_zen"), scaled(x, y, kMoneyIcon));
    const gfx::Box strip = scaled(x, y, kMoneyStrip);
    controls::caps(canvas, x + kMoneyFrom * k, controls::middle(strip.y, strip.h, 12.0f * u),
                   12.0f * u, style::kAshInk, "ZEN");
    const float size = 16.0f * u;
    controls::ranged(canvas, x + valueRight * k, controls::middle(strip.y, strip.h, size), size,
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
