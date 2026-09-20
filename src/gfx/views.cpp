#include "gfx/views.h"

namespace mu::gfx {

const char* viewName(View v) {
    switch (v) {
        case ViewShadow: return "shadow";
        case ViewPrepass: return "prepass";
        case ViewSsao: return "ssao";
        case ViewBlur: return "blur";
        case ViewShade: return "shade";
        case ViewPresent: return "present";
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
        case AccountPresent: return 0.5;
        default: return 0.0;
    }
}

double spareBudgetMs() { return 0.5; }

double totalGpuBudgetMs() {
    double sum = spareBudgetMs();
    for (int a = 0; a < AccountCount; ++a) sum += accountBudgetMs(Account(a));
    return sum;  // 5.5 ms
}
double frameBudgetMs() { return 5.5; }  // 180 fps

}  // namespace mu::gfx
