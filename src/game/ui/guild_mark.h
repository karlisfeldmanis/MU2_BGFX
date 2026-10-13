// A guild's mark (docs/guild.md): MU's 8x8 of 16 colours, drawn a rect a pixel wherever it is
// shown -- the nameplate, the target frame, the Card, the chat's card and the guild's windows.
// Sixty-four quads at most, a handful of marks on screen: no atlas is worth it.
#pragma once

#include <cstdint>

#include "gfx/interface.h"
#include "net/wire.h"

namespace mu::game::guildmark {

// MuMain's palette (ZzzInventory.cpp CreateGuildMark), index 0 clear.
inline uint32_t colour(int index, float alpha = 1.0f) {
    static constexpr uint8_t kRgb[16][3] = {
        {0, 0, 0},     {0, 0, 0},     {128, 128, 128}, {255, 255, 255}, {255, 0, 0},   {255, 128, 0},
        {255, 255, 0}, {128, 255, 0}, {0, 255, 0},     {0, 255, 128},   {0, 255, 255}, {0, 128, 255},
        {0, 0, 255},   {128, 0, 255}, {255, 0, 255},   {255, 0, 128}};
    const uint8_t* c = kRgb[index & 15];
    return gfx::rgba(float(c[0]) / 255.0f, float(c[1]) / 255.0f, float(c[2]) / 255.0f, alpha);
}

// The mark in `box`, square; its clear pixels left to what is under it. `rim` a dark line round
// it, for one drawn over the world.
inline void draw(gfx::Canvas& canvas, const gfx::Box& box, const net::GuildMarkBits& mark, float alpha = 1.0f,
                 bool rim = false) {
    const float px = box.w / 8.0f;
    if (rim) canvas.rect({box.x - 1.0f, box.y - 1.0f, box.w + 2.0f, box.h + 2.0f}, gfx::rgba(0, 0, 0, 0.85f * alpha));
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int p = net::guildPixel(mark, x, y);
            if (p == 0) continue;
            canvas.rect({box.x + float(x) * px, box.y + float(y) * px, px + 0.25f, px + 0.25f}, colour(p, alpha));
        }
    }
}

}  // namespace mu::game::guildmark
