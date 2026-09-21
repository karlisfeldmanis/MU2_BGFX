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
    // Sprint 8b's bloom: the HDR target halved five times, then added back up the chain.
    // Between the transparent pass and the tonemap for the same reason the transparent pass is
    // there: it reads the linear radiance the flames added into.
    ViewBloomDown = 6,   // 6..10, one per level, full to 1/32
    ViewBloomUp = 11,    // 11..14, 1/32 back up to 1/2
    ViewPresent = 15,
    ViewHud = 16,
    // Sprint 8c's reflection probe: six faces of the town round the player, then the
    // prefiltered copy a mip and a face at a time. AFTER the frame, and read by the next one's
    // shade pass: the faces are drawn a frame late in any case, since one face is drawn a
    // frame, and after the frame they read this frame's sun split rather than needing a view
    // of their own between the shadow and the shade.
    ViewProbeFace = 17,     // 17..22, a face each
    ViewProbeFilter = 23,   // 23..52, mip * 6 + face
    // Sprint 7's item pictures: the bag's stage at 53 and the shelf's at 60, each its own
    // target. After the HUD, so a restocked window shows its new picture a frame late; the
    // target keeps the old one meanwhile. game/items_stage.h.
    ViewStageBag = 53,
    ViewProbeChain = 54,    // 54..59, the chain of the face drawn this frame, a level each
    ViewStageShelf = 60,
    ViewCount = 61,
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
