// The sim with no window: `--headless --seed S --ticks N`.
//
// This is the half of sprint 5 that carries its proving sentence, and it comes first on
// purpose. A sim proved stable with nothing in it but itself has exactly one new thing in it
// the day the window is added, so when the byte comparison breaks there is one place to look.
#pragma once

#include "core/args.h"

namespace mu::game {

// Runs the hunt and writes the event log. Returns a process exit code: 0 when the run was
// clean, 1 when an invariant was broken or a file could not be written.
int runHeadless(const core::Args& args, const char* assetDir);

}  // namespace mu::game
