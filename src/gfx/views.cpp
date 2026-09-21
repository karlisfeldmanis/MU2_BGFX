#include "gfx/views.h"

namespace mu::gfx {

const char* viewName(View v) {
    switch (v) {
        case ViewShadow: return "shadow";
        case ViewPrepass: return "prepass";
        case ViewSsao: return "ssao";
        case ViewBlur: return "blur";
        case ViewShade: return "shade";
        case ViewTransparent: return "effects";
        case ViewPresent: return "present";
        case ViewBloomDown: return "bloom_down1";
        case ViewBloomDown + 1: return "bloom_down2";
        case ViewBloomDown + 2: return "bloom_down3";
        case ViewBloomDown + 3: return "bloom_down4";
        case ViewBloomDown + 4: return "bloom_down5";
        case ViewBloomUp: return "bloom_up4";
        case ViewBloomUp + 1: return "bloom_up3";
        case ViewBloomUp + 2: return "bloom_up2";
        case ViewBloomUp + 3: return "bloom_up1";
        case ViewHud: return "hud";
        default: return "?";
    }
}

const char* accountName(Account a) {
    switch (a) {
        case AccountShadow: return "shadow";
        case AccountPrepass: return "prepass";
        case AccountSsao: return "ssao";
        case AccountShade: return "shade";
        case AccountEffects: return "effects";
        case AccountPresent: return "present";
        default: return "?";
    }
}

Account viewAccount(View v) {
    switch (v) {
        case ViewShadow: return AccountShadow;
        case ViewPrepass: return AccountPrepass;
        case ViewSsao:
        case ViewBlur: return AccountSsao;
        case ViewShade: return AccountShade;
        case ViewTransparent: return AccountEffects;
        case ViewPresent:
        case ViewHud: return AccountPresent;
        default: return AccountPresent;
    }
}

double accountBudgetMs(Account a) {
    // docs/budget.md. 5.5 ms at 1080p over Lorencia's town, 0.5 of it unspent.
    switch (a) {
        case AccountShadow: return 1.0;
        case AccountPrepass: return 0.7;
        case AccountSsao: return 0.5;
        case AccountShade: return 2.3;
        // Sprint 6's account, opened against the measurement in docs/budget.md and not
        // before it. 0.3 ms buys about sixteen full screens of blended overdraw at 1080p,
        // which is far more than MU's fight asks for: 128 sprites a metre across cost 0.18 ms
        // and 64 half-metre ones cost nothing measurable. It is paid for out of the spare,
        // explicitly and in the open, which is the one thing the sprint file allows and
        // quietly taking it is the thing it forbids.
        case AccountEffects: return 0.3;
        case AccountPresent: return 0.5;
        default: return 0.0;
    }
}

// 0.5 until sprint 6, which moved 0.3 of it into the effects account above. docs/budget.md
// has the measurement that justified it. Note that budget.md's own table said 0.2 here while
// this said 0.5 and the two had disagreed since the file was written -- the table summed to
// 5.2 against a 5.5 ms frame. It sums to 5.5 now.
double spareBudgetMs() { return 0.2; }

double totalGpuBudgetMs() {
    double sum = spareBudgetMs();
    for (int a = 0; a < AccountCount; ++a) sum += accountBudgetMs(Account(a));
    return sum;  // 5.5 ms
}
double frameBudgetMs() { return 5.5; }  // 180 fps

}  // namespace mu::gfx
