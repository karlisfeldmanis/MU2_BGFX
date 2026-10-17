// **The graphics driver warned about before the first Play** (docs/plans/launcher.md, L8; the
// user, 2026-10-17: "lets better show warning not install something"). A third of the crash
// reports by then came from a PC with no graphics driver at all -- Windows' Basic Render adapter,
// the game drawn in software until the device was lost -- or an Intel Iris Plus on a driver from
// 2020-2022, lost on its first minutes in Lorencia.
//
// The launcher installs nothing: a launcher that asks to be administrator and swaps drivers is
// what antivirus heuristics flag, and no big launcher does it. It says so in a word beside the
// card at the top left (ui.cpp drawHome), "Graphics driver is old" or "No graphics driver", and
// nothing more.
#pragma once

#include "launcher/gpu.h"

namespace mu::launcher {

// No driver of the card's own: Windows' Basic Render or Basic Display adapter, vendor 0x1414,
// which DXGI does not always flag as software.
inline bool driverMissing(const Gpu& gpu) { return gpu.known && (gpu.software || gpu.vendorId == 0x1414); }

// The machine is better off with another driver: none at all, or one kDriverYears old.
inline bool needsDriver(const Gpu& gpu) { return driverMissing(gpu) || (gpu.known && gpu.driverOld); }

}  // namespace mu::launcher
