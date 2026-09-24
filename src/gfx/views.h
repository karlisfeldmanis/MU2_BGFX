// The frame's views and the budget accounts they answer to. Fixed from the first commit so
// that every sprint adds content to a complete frame and is priced against it; see
// docs/budget.md and docs/conventions.md.
#pragma once

#include <cstdint>

namespace mu::gfx {

// Sprint 6 inserted ViewTransparent at 5 and moved present and hud to 6 and 7. It has to sit
// between the shade and the tonemap and nowhere else: it draws into the SAME HDR target the
// shade pass wrote, so an additive flame adds to a linear radiance and is tonemapped with the
// scene it is in. Drawn after the present instead, it would be LDR sprites laid over an
// already-tonemapped image, and every additive effect would clip white at a different place
// than the fire beside it.
enum View : uint16_t {
    ViewShadow = 0,
    ViewPrepass = 1,
    ViewSsao = 2,
    ViewBlur = 3,
    ViewShade = 4,
    ViewTransparent = 5,
    // Defense's ward: the green glow round the shield, the hover ring's silhouette machinery in
    // a mask of its own. Here, between the effects and the bloom, and not over the finished
    // picture with the ring: it is drawn INTO the scene's HDR target and tested against its
    // depth at the shield's own, so an arm or a monster in front of the shield hides the glow
    // as it hides the shield, and the glow blooms and is tonemapped with the light round it
    // (the user, 2026-09-25: "integrated with the actual shield, its z").
    ViewWardMask = 6,       // the shield alone, into the ward's own tiny target
    ViewWard = 7,           // the glow, added into the scene round the shield, depth-tested
    // Sprint 8b's bloom: the HDR target halved five times, then added back up the chain.
    // Between the transparent pass and the tonemap for the same reason the transparent pass is
    // there: it reads the linear radiance the flames added into.
    ViewBloomDown = 8,   // 8..12, one per level, full to 1/32
    ViewBloomUp = 13,    // 13..16, 1/32 back up to 1/2
    ViewPresent = 17,
    // The gold ring's own two views, inserted here so it sits where it has to: AFTER the
    // present pass's tonemap, so its colour is a display colour and not a linear one added
    // into HDR, and BEFORE the HUD, so a window drawn over a ringed monster still covers it.
    // game/outline.cpp.
    ViewOutlineMask = 18,   // the hovered thing's own meshes, into their own tiny target
    ViewOutline = 19,       // the ring, composed into its box of the backbuffer
    ViewHud = 20,
    // Sprint 8c's reflection probe: six faces of the town round the player, then the
    // prefiltered copy a mip and a face at a time. AFTER the frame, and read by the next one's
    // shade pass: the faces are drawn a frame late in any case, since one face is drawn a
    // frame, and after the frame they read this frame's sun split rather than needing a view
    // of their own between the shadow and the shade.
    ViewProbeFace = 21,     // 21..26, a face each
    ViewProbeFilter = 27,   // 27..56, mip * 6 + face
    // Sprint 7's item pictures: the bag's stage at 57 and the shelf's at 64, each its own
    // target. After the HUD, so a restocked window shows its new picture a frame late; the
    // target keeps the old one meanwhile. game/items_stage.h.
    ViewStageBag = 57,
    ViewProbeChain = 58,    // 58..63, the chain of the face drawn this frame, a level each
    ViewStageShelf = 64,
    ViewStageQuick = 65,    // the potion boxes' pictures on the HUD's own stage
    // The tooltip's own picture of the one thing under the pointer, at rest: the bag turns
    // what is hovered, and a turning picture in the tooltip's head is a picture that will not
    // hold still to be read.
    ViewStageTip = 66,
    ViewCount = 67,
};
constexpr int kBloomLevels = 5;
constexpr int kProbeSize = 128;  // the raw cube's edge, texels
constexpr int kProbeMips = 5;    // the prefiltered chain: 128 down to 8, roughness 0 to 1
constexpr int kProbeChain = 6;   // the raw cube's own chain, 128 down to 4, box filtered

enum Account : uint8_t {
    AccountShadow = 0,
    AccountPrepass,
    AccountSsao,
    AccountShade,
    AccountEffects,
    AccountPresent,
    AccountProbe,
    AccountCount,
};

const char* viewName(View v);
const char* accountName(Account a);

// Which account a view's time is charged to. Two views share ssao (the pass and its blur)
// and two share present (the tonemap and the HUD).
Account viewAccount(View v);

// The documented allowance in milliseconds of GPU at 1080p.
double accountBudgetMs(Account a);

// What is deliberately unspent, so a sprint cannot quietly borrow it.
double spareBudgetMs();

// Every account plus the spare. Advisory: it is what the accounts add up to, not a figure
// anything measures directly.
double totalGpuBudgetMs();

// What one frame of wall clock is allowed to take. THE enforced number, because it is the
// only one that decides whether 180 fps happens, and the only one measured without a
// GPU timer that counts waiting.
double frameBudgetMs();

}  // namespace mu::gfx
