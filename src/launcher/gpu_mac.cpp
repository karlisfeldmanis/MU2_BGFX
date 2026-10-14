// The probe where there is none yet (docs/plans/launcher.md, L6): the Mac launcher is L5 and
// later, and until it has one of its own nothing is known about the machine here -- no line on
// the screens and no first-run graphics written.
#include "launcher/gpu.h"

namespace mu::launcher {

Gpu probeGpu() { return {}; }

}  // namespace mu::launcher