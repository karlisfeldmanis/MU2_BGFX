// One player's capsule: SD a thin amber band along its top, health the body, mana along its foot,
// flush and rounded as one shape, his name on a dark band over the health's left and a number at
// its right. The party bar's member and raider (game/ui/party_bar.h) and the target frame's
// (game/ui/target_frame.h) are this one drawing at their own sizes, so the three read alike (the
// user, 2026-10-10: "correct direction, which we can use for party and also raid").
#pragma once

#include <cstdint>
#include <string>

#include "gfx/interface.h"

namespace mu::game::capsule {

// A capsule's make: its three bands top to foot, its corner, the gap under it, and its words.
struct Make {
    float shield, life, mana, radius, gap;
    float name, tag, number;  // the name's size, the level tag's, the number's
    float pad;                // the words in from the capsule's ends
    float cell;               // the class's letters in their cell, 0 for no cell
    float band;               // the dark band behind the name, a share of the width
};

// What one capsule says.
struct Look {
    std::string name;
    int kin = -1;        // sim::Kin, -1 for none
    std::string tag;     // the level after the name, or empty
    std::string number;  // at the right: his health, its percent, a map's name, or empty
    float shield = -1.0f, life = 0.0f, mana = -1.0f, trail = 0.0f;  // shares, -1 for none
    bool leader = false, away = false, dead = false;
    bool crown = true;   // the leader's crown on its corner (the target frame wears it on the face)
};

// MU's short names for the four classes, as the item card's requirement rows say them, and each
// one's muted tint.
extern const char* const kKinShort[4];
extern const uint32_t kKinInk[4];
constexpr uint32_t kGold = gfx::rgba(0.878f, 0.714f, 0.361f);      // the leader: the tracker's gold
constexpr uint32_t kCrownDrop = gfx::rgba(0.0f, 0.0f, 0.0f, 0.85f); // under the leader's badge
constexpr uint32_t kShadow = gfx::rgba(0.0f, 0.0f, 0.0f, 0.22f);    // under a capsule or a menu
constexpr float kLow = 30.0f;  // percent: the fill turns hot and pulses

// The capsule at `x`, `y`, `wide` across, `make`'s size in units of `u`; `pulse` 0 to 1 is the
// low health's breath. Returns its height with the gap under it.
float draw(gfx::Canvas& canvas, float x, float y, float u, float wide, const Make& make,
           const Look& look, float pulse);

// The leader's mark: a small crown, three points over a band, drawn from rects (no art for it in
// MU's interface). `x`, `base`: its left and its foot, `s` the unit.
void crown(gfx::Canvas& canvas, float x, float base, float s, uint32_t ink);

uint32_t faded(uint32_t abgr, float alpha);

// The button that opens a player's menu (game/ui/player_menu.h), beside his capsule: a dark
// rounded tab with three dots, lighter under the pointer (the user, 2026-10-11: "add some simple
// button which triggers that right click menu, that button also has to be on party label").
void more(gfx::Canvas& canvas, const gfx::Box& box, float u, bool lit);

// A disc of `art` (or of ink alone) at `cx`, `cy`.
void disc(gfx::Canvas& canvas, float cx, float cy, float r, const gfx::Art* art, uint32_t ink);
// His face in its round frame, as the target frame and the party's capsules wear it: a dark ring
// `ring` wide, the well, and his head (game/portrait.h) when `picture` is his -- greyed when he
// lies dead, dimmed when he is away -- else his class's letters in the well.
void face(gfx::Canvas& canvas, float cx, float cy, float r, float ring, const gfx::Art* picture, int kin,
          bool dead, bool away);

}  // namespace mu::game::capsule
