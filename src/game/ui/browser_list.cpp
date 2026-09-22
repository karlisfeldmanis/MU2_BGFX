#include "game/ui/browser_list.h"

#include <cstdio>

namespace mu::game {

ListHit drawBrowserList(gfx::Overlay& overlay, const ModelBench& bench, int width, int height,
                        float pointerX, float pointerY) {
    // Every frame starts empty and hands the overlay the backbuffer's size. Forgetting this
    // is not a small bug: the quads pile up run-long, and the size the vertex shader divides
    // by stays zero, so the whole list is one NaN off the screen and nothing draws at all.
    overlay.begin(width, height);
    ListHit hit;

    // The face is Open Sans at a 8*scale line box, so this is the list's text size in the
    // only units the overlay has. 2.0 was right for the 5x7 bitmap, whose letters filled
    // their box; a proportional face at the same box reads a size smaller, and this is the
    // value that puts it back where a name is legible in a 1080p shot.
    constexpr float kScale = 2.8f;
    constexpr float kPad = 10.0f;
    constexpr uint32_t kBack = 0xD8140d0au;    // abgr: a dark wash, so names read over grass
    constexpr uint32_t kInk = 0xFFc8c8c8u;
    constexpr uint32_t kChosen = 0xFFffffffu;
    constexpr uint32_t kDim = 0xFF8a8a8au;
    constexpr uint32_t kChosenBar = 0xB0705030u;
    constexpr uint32_t kHoverBar = 0x60606060u;
    constexpr uint32_t kRule = 0x40ffffffu;

    // A quarter of a line of leading. The font's own cell is one pixel taller than its
    // glyphs, which is enough to keep two lines from touching and not enough to read a
    // hundred names down: set solid, the list is a grey block and the eye slides off it.
    constexpr float kLeading = 1.25f;
    const float line = gfx::Overlay::lineHeight(kScale) * kLeading;
    const size_t count = bench.browseCount();
    if (count == 0) return hit;

    // The categories, one row each above the names. Vertical rather than a strip across the
    // top: the panel is already as wide as the widest name and no wider, and four labels laid
    // side by side either overflow that or have to be shortened until they stop being the
    // words the log uses for the same thing.
    const float tabScale = kScale * 0.9f;
    const float tabLine = gfx::Overlay::lineHeight(tabScale) * kLeading * 1.15f;
    const size_t tabs = bench.categoryCount() > 8 ? 8 : bench.categoryCount();
    const float tabBlock = float(tabs) * tabLine + kPad;

    const float header = line * 1.6f;
    const float footer = line * 1.6f;
    // As many as fit in the top three quarters, so the list never runs into the frame line
    // the log prints at the bottom of a review shot.
    size_t rows =
        size_t((float(height) * 0.75f - kPad * 2.0f - tabBlock - header - footer) / line);
    if (rows < 1) rows = 1;
    if (rows > count) rows = count;
    const size_t half = rows / 2;
    size_t first = bench.browseIndex() > half ? bench.browseIndex() - half : 0;
    if (first + rows > count) first = count > rows ? count - rows : 0;
    const size_t last = first + rows;

    float widest = overlay.measure(kScale, "COOKED MODELS  999/999");
    for (size_t i = first; i < last; ++i) {
        const float w = overlay.measure(kScale, bench.browseName(i));
        if (w > widest) widest = w;
    }
    char tabLabels[8][96];
    for (size_t i = 0; i < tabs && i < 8; ++i) {
        std::snprintf(tabLabels[i], sizeof(tabLabels[i]), "%s  %zu",
                      bench.category(i).label.c_str(), bench.category(i).entries.size());
        const float w = overlay.measure(tabScale, tabLabels[i]);
        if (w > widest) widest = w;
    }

    hit.x = kPad;
    hit.y = kPad;
    hit.w = widest + kPad * 3.0f;
    hit.h = tabBlock + header + float(rows) * line + footer + kPad;
    overlay.panel(hit.x, hit.y, hit.w, hit.h, kBack);
    hit.over = pointerX >= hit.x && pointerX < hit.x + hit.w && pointerY >= hit.y &&
               pointerY < hit.y + hit.h;

    // The tabs. An empty category is drawn dim and cannot be chosen -- a world cooked with
    // no figures still shows that the monsters are a thing the viewer has, and that there
    // are none of them here, which is a different statement from the tab not existing.
    for (size_t i = 0; i < tabs && i < 8; ++i) {
        const float y = hit.y + kPad * 0.3f + float(i) * tabLine;
        const bool empty = bench.category(i).entries.empty();
        const bool open = i == bench.categoryIndex();
        const bool over = !empty && hit.over && pointerY >= y && pointerY < y + tabLine;
        if (over) hit.tab = (long long)i;
        if (open || over) {
            overlay.panel(hit.x + 2.0f, y - 1.0f, hit.w - 4.0f, tabLine,
                          open ? kChosenBar : kHoverBar);
        }
        overlay.text(hit.x + kPad, y, tabScale, empty ? kDim : (open ? kChosen : kInk),
                     tabLabels[i]);
    }
    overlay.panel(hit.x + kPad, hit.y + tabBlock - 3.0f, hit.w - kPad * 2.0f, 1.0f, kRule);

    char label[96];
    std::snprintf(label, sizeof(label), "%s  %zu/%zu",
                  bench.category(bench.categoryIndex()).label.c_str(), bench.browseIndex() + 1,
                  count);
    overlay.text(hit.x + kPad, hit.y + tabBlock + kPad * 0.6f, kScale, kDim, label);
    // A rule under the heading and above the footer, which is the whole of the chrome: a
    // panel with a line at each end reads as a list, and costs two quads.
    overlay.panel(hit.x + kPad, hit.y + tabBlock + header - 3.0f, hit.w - kPad * 2.0f, 1.0f,
                  kRule);

    const float top = hit.y + tabBlock + header;
    for (size_t i = first; i < last; ++i) {
        const float y = top + float(i - first) * line;
        const bool chosen = i == bench.browseIndex();
        const bool over = hit.over && pointerY >= y && pointerY < y + line;
        if (over) hit.hovered = (long long)i;
        if (chosen || over) {
            overlay.panel(hit.x + 2.0f, y - 1.0f, hit.w - 4.0f, line, chosen ? kChosenBar
                                                                             : kHoverBar);
        }
        overlay.text(hit.x + kPad, y, kScale, chosen ? kChosen : kInk, bench.browseName(i));
    }

    const float footTop = top + float(rows) * line;
    overlay.panel(hit.x + kPad, footTop + 2.0f, hit.w - kPad * 2.0f, 1.0f, kRule);
    // What the footer says depends on what is standing there: with a figure the useful thing
    // is which of its clips is running, because that is the one fact a still cannot show.
    overlay.text(hit.x + kPad, footTop + 6.0f, kScale * 0.75f, kDim,
                 // Brackets are drawn again now the face is a real one. They were spelled
                 // out as the word for a while, because the 5x7 fallback has no bracket in
                 // it and dropped the two keys this line exists to name.
                 bench.hasFigure() ? "Tab: category   [ ]: clip   Drag turns   Wheel zooms"
                                   : "Tab: category   Arrows walk   Drag turns   Wheel zooms");
    return hit;
}

}  // namespace mu::game
