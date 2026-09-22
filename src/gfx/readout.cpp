#include "gfx/readout.h"

#include <algorithm>
#include <cstdio>

#include <bx/timer.h>

#include "gfx/overlay.h"
#include "gfx/views.h"

namespace mu::gfx {

void Readout::draw(Overlay& overlay, int width, int height) {
    if (!overlay.ready() || width <= 0 || height <= 0) return;

    const int64_t now = bx::getHPCounter();
    if (last_ != 0) {
        const double toMs = 1000.0 / double(bx::getHPFrequency());
        const double ms = double(now - last_) * toMs;
        samples_[next_] = ms;
        next_ = (next_ + 1) % kWindow;
        if (filled_ < kWindow) ++filled_;
        sinceText_ += ms;
    }
    last_ = now;
    if (filled_ == 0) return;

    if (sinceText_ >= kRefreshMs) {
        sinceText_ = 0.0;
        // Copied out and partially sorted, because nth_element rearranges what it is given
        // and the ring's order is what tells the oldest sample from the newest. 31 doubles on
        // the stack, four times a second.
        double sorted[kWindow];
        std::copy(samples_, samples_ + filled_, sorted);
        const int mid = filled_ / 2;
        std::nth_element(sorted, sorted + mid, sorted + filled_);
        const double median = sorted[mid];
        char buf[16];
        // Whole frames a second: a tenth of a frame is below what the median of half a second
        // can honestly claim, and a digit that flickers is the thing this is trying to avoid.
        const int fps = median > 0.0 ? int(1000.0 / median + 0.5) : 0;
        std::snprintf(buf, sizeof buf, "%d FPS", fps > 9999 ? 9999 : fps);
        // assign into the member rather than building a string: after the first call the
        // capacity is already there, so no frame allocates.
        text_.assign(buf);
    }
    if (text_.empty()) return;

    // Scaled off 1080, as the tile over the character's head is (app/modes/play_mode.cpp), so the readout is
    // the same size on screen whatever --height the run asked for. 2.4 is the browser list's
    // own scale, which is a 19 px line at 1080: small enough to ignore, large enough to read.
    const float scale = 2.4f * float(height) / 1080.0f;
    const float margin = 10.0f * float(height) / 1080.0f;
    const float across = overlay.measure(scale, text_);
    overlay.begin(width, height);
    // No panel behind it. Overlay::text already draws every glyph twice, once a pixel down and
    // right in three-quarters-opaque black and once in the ink -- which is that file's own
    // answer to "the background is both bright and dark", and it is why a plate is not needed
    // to stay legible over pale grass or over night. A plate would also be the opposite of
    // unobtrusive. The ink is the same near-white the tile label uses.
    overlay.text(float(width) - across - margin, margin, scale, 0xFFE8F4FFu, text_);
    overlay.submit(ViewHud);
}

}  // namespace mu::gfx
