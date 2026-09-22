#include "app/preloader.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

#include "core/log.h"
#include "gfx/views.h"

namespace mu::app {

bool Preloader::run(Context& ctx, const std::function<bool()>& load, bool* quitEarly) {
    std::atomic<int> loaded{0};  // 0 loading, 1 ready, -1 what was asked for did not open
    std::thread loader([&]() { loaded.store(load() ? 1 : -1); });

    ctx.curtain.init(ctx.paths.shaders);
    // The spinner's dot: a soft disc, made here in a few lines rather than loaded, because
    // nothing of the cook is in hand yet. Drawn through the effects pass, in screen space,
    // so a dot is a round antialiased sprite -- the overlay can only lay axis-aligned
    // rectangles, and a ring built out of those reads as a staircase of squares.
    bgfx::TextureHandle dot = BGFX_INVALID_HANDLE;
    {
        constexpr int kSide = 64;
        const bgfx::Memory* pixels = bgfx::alloc(kSide * kSide * 4);
        for (int y = 0; y < kSide; ++y) {
            for (int x = 0; x < kSide; ++x) {
                const float dx = (float(x) + 0.5f) / kSide * 2.0f - 1.0f;
                const float dy = (float(y) + 0.5f) / kSide * 2.0f - 1.0f;
                const float r = std::sqrt(dx * dx + dy * dy);
                // Solid to 0.55 of the radius and out to nothing by 1.0: a soft edge rather
                // than a hard circle, which at eight pixels across is what reads as round.
                const float a = std::max(0.0f, std::min(1.0f, (1.0f - r) / 0.45f));
                uint8_t* at = pixels->data + (size_t(y) * kSide + size_t(x)) * 4;
                at[0] = at[1] = at[2] = 255;
                at[3] = uint8_t(a * a * 255.0f);
            }
        }
        dot = bgfx::createTexture2D(kSide, kSide, false, 1, bgfx::TextureFormat::RGBA8,
                                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, pixels);
    }
    // A comet arc chasing round: its head is the brightest and largest dot, at a continuous
    // angle, so it moves by fractions of a dot a frame rather than stepping between them.
    // MU's marker gold.
    const auto spinner = [&](float alpha, double seconds) {
        const int w = ctx.window.width(), h = ctx.window.height();
        bgfx::setViewFrameBuffer(gfx::ViewHud, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(gfx::ViewHud, 0, 0, uint16_t(w), uint16_t(h));
        bgfx::setViewClear(gfx::ViewHud, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff,
                           1.0f, 0);
        bgfx::touch(gfx::ViewHud);
        const float unit = float(h) / 1080.0f;
        const float cx = float(w) * 0.5f, cy = float(h) * 0.5f;
        const float radius = 30.0f * unit;
        if (alpha > 0.0f && bgfx::isValid(dot)) {
            // Screen space through the effects pass: an orthographic projection in pixels
            // with y running down, and a view that is the identity, which leaves its
            // billboard axes as x right and y down -- so a sprite's position is a pixel and
            // its half-extents are pixels too.
            float screen[16];
            bx::mtxOrtho(screen, 0.0f, float(w), float(h), 0.0f, -1.0f, 1.0f, 0.0f,
                         bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
            float eye[16];
            bx::mtxIdentity(eye);
            ctx.renderer.effects().begin();
            const auto put = [&](float x, float y, float half, float a, bool add) {
                gfx::Sprite s;
                s.position[0] = x;
                s.position[1] = y;
                s.position[2] = 0.0f;
                s.halfWidth = s.halfHeight = half;
                s.sheet = dot;
                s.blend = add ? gfx::Blend::Additive : gfx::Blend::Alpha;
                // MU's marker gold, warmer at the head than at the tail.
                s.colour[0] = 1.0f;
                s.colour[1] = 0.74f;
                s.colour[2] = 0.38f;
                s.colour[3] = a * alpha;
                ctx.renderer.effects().add(s);
            };
            // The track the ribbon runs on: a faint closed ring, so the arc is read as
            // something travelling round a circle rather than as a shape adrift.
            // Enough of them, and wide enough, that they overlap into a line: at 72 dots
            // of 3 px the ring read as a dotted circle, which is the opposite of smooth.
            for (int i = 0; i < 160; ++i) {
                const float angle = float(i) / 160.0f * 6.2831853f;
                put(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius,
                    2.2f * unit, 0.055f, false);
            }
            // The ribbon: enough overlapping dots that it is a continuous stroke and not a
            // string of beads, thickening and brightening towards the head. Additive, so
            // where the dots overlap it glows rather than banding.
            const float head = float(seconds) * 4.6f;  // radians a second
            constexpr int kAlong = 120;
            constexpr float kSweep = 3.5f;  // radians of arc behind the head
            for (int i = 0; i < kAlong; ++i) {
                const float along = float(i) / float(kAlong - 1);  // 0 tail .. 1 head
                const float angle = head - (1.0f - along) * kSweep;
                // Eased at both ends: the tail dies away instead of stopping, and the head
                // is a rounded cap rather than a cut.
                const float taper = along * along * (3.0f - 2.0f * along);
                const float cap = 1.0f - std::pow(std::max(0.0f, along - 0.94f) / 0.06f, 2.0f);
                put(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius,
                    (1.1f + 2.2f * taper) * unit, 0.10f * taper * std::max(0.0f, cap), true);
            }
            // And the flare at the head: two soft haloes over the brightest dot, which is
            // what makes it read as a light being carried round rather than a painted arc.
            put(cx + std::cos(head) * radius, cy + std::sin(head) * radius, 5.0f * unit,
                0.55f, true);
            put(cx + std::cos(head) * radius, cy + std::sin(head) * radius, 11.0f * unit,
                0.16f, true);
            put(cx + std::cos(head) * radius, cy + std::sin(head) * radius, 22.0f * unit,
                0.05f, true);
            const float at[3] = {0.0f, 0.0f, 1.0f};
            ctx.renderer.effects().draw(gfx::ViewHud, eye, screen, at);
        }
        if (ctx.curtain.ready() && alpha > 0.0f) {
            ctx.curtain.begin(w, h);
            const float scale = 2.2f * unit;
            const char* word = "Loading";
            const float across = ctx.curtain.measure(scale, word);
            const uint32_t ink = (uint32_t(alpha * 0.5f * 255.0f) << 24) | 0x00c8d8e6u;
            ctx.curtain.text(cx - across * 0.5f, cy + radius + 24.0f * unit, scale, ink, word);
            ctx.curtain.submit(gfx::ViewHud);
        }
        bgfx::frame();
    };
    const int64_t spun = bx::getHPCounter();
    const auto since = [&](int64_t from) {
        return double(bx::getHPCounter() - from) / double(bx::getHPFrequency());
    };
    // Up while the worker loads, and then a quarter second out, on black, so the game does
    // not arrive on top of a spinner caught mid-turn.
    //
    // Presented WITHOUT vsync, and paced here instead. bgfx holds its resource lock through
    // the whole of a frame -- in single-threaded rendering the frame renders, and waits for
    // the display, inside it -- so a spinner paced by vsync held the lock nearly all the
    // time and the worker crept: 3.6 s for what loads in 0.35 s alone. Unsynced, a frame
    // holds it for about a millisecond, the rest of each 8 ms is the worker's, and the
    // spinner's angle is taken from the clock at the moment it is drawn, so its motion is
    // even whatever the cadence. Tearing is possible and invisible: a small arc on black.
    ctx.window.holdVsync(true);
    // Paced to the display's own refresh by DEADLINES rather than by sleeping a slice after
    // each frame: sleeping a fixed slice after variable work makes the spacing drift with
    // whatever the frame cost, which is the judder an eye reads as a stutter. Each frame is
    // aimed at the next whole tick of the refresh, and one that overruns aims at the one
    // after rather than trying to catch up.
    //
    // At the display's own rate. A spinner frame costs 2.4 ms of this thread and the rest
    // of each period is the loader's; what used to spoil the cadence was the upload of
    // everything the loader had created since the last frame, which the valve above now
    // meters. The first frame is the exception at about 17 ms -- Metal compiles the pass's
    // pipeline there -- and it happens while the spinner is still fading up from nothing.
    const double period = 1.0 / double(ctx.window.refreshHz());
    double due = since(spun);
    double worstGap = 0.0, lastAt = -1.0, frameCost = 0.0, worstFrame = 0.0;
    int spins = 0;
    // A shot of the spinner itself, for review: the loop below is not the frame loop and
    // --shot does not reach it. MU2_SPIN_SHOT=/abs/path.png takes one a second in.
    const char* spinShot = std::getenv("MU2_SPIN_SHOT");
    bool spinShotTaken = false;
    while (loaded.load() == 0 || (spinShot != nullptr && since(spun) < 1.4)) {
        if (!*quitEarly && (!ctx.window.pump() || ctx.window.escapePressed())) *quitEarly = true;
        if (!*quitEarly) {
            const double at = since(spun);
            // After the first three: the first frame compiles the pass's pipeline and the
            // spinner is invisible through it anyway, and a worst case dominated by it says
            // nothing about the cadence an eye sees.
            if (lastAt >= 0.0 && spins > 3) worstGap = std::max(worstGap, at - lastAt);
            lastAt = at;
            ++spins;
            const int64_t before = bx::getHPCounter();
            spinner(std::min(1.0f, float(at) / 0.15f), at);
            const double cost = since(before);
            worstFrame = std::max(worstFrame, cost);
            frameCost += cost;
        }
        if (spinShot != nullptr && !spinShotTaken && since(spun) > 1.0) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, spinShot);
            spinShotTaken = true;
        }
        due += period;
        if (due < since(spun)) due = since(spun);  // overran: aim at the next one, not at a catch-up
        const double left = due - since(spun);
        // Most of the wait asleep and the last of it spinning on the clock: a sleep is only
        // accurate to a millisecond or so, which at 120 Hz is an eighth of a frame.
        if (left > 0.002) {
            std::this_thread::sleep_for(std::chrono::microseconds(int((left - 0.002) * 1e6)));
        }
        while (since(spun) < due) {
        }
    }
    loader.join();
    ctx.window.holdVsync(false);
    if (bgfx::isValid(dot)) bgfx::destroy(dot);
    core::logf("preloader: the world loaded behind the spinner in %.2f s; %d spinner frames "
               "at %d Hz, mean %.1f ms, worst gap %.1f ms, frame %.1f ms mean %.1f worst",
               since(spun), spins, ctx.window.refreshHz(),
               spins > 0 ? since(spun) * 1000.0 / spins : 0.0, worstGap * 1000.0,
               spins > 0 ? frameCost * 1000.0 / spins : 0.0, worstFrame * 1000.0);
    // The HUD view goes back to drawing over the frame rather than clearing it.
    bgfx::setViewClear(gfx::ViewHud, BGFX_CLEAR_NONE);

    return loaded.load() > 0;
}

}  // namespace mu::app
