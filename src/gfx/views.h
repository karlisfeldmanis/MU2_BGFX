// The frame's views and the budget accounts they answer to. Fixed from the first commit so
// that every sprint adds content to a complete frame and is priced against it; see
// docs/budget.md and docs/conventions.md.
#pragma once

#include <cstdint>

namespace mu::gfx {

enum View : uint16_t {
    ViewShadow = 0,
    ViewPrepass = 1,
    ViewSsao = 2,
    ViewBlur = 3,
    ViewShade = 4,
    ViewPresent = 5,
    ViewHud = 6,
    ViewCount = 7,
};

enum Account : uint8_t {
    AccountShadow = 0,
    AccountPrepass,
    AccountSsao,
    AccountShade,
    AccountPresent,
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
