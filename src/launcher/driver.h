// **The graphics driver installed before the first Play** (docs/plans/launcher.md, L8; the user,
// 2026-10-17: "identify missing drivers, install them before first play"). A third of the crash
// reports by then came from a PC with no graphics driver at all -- Windows' Basic Render adapter,
// the game drawn in software until the device was lost -- or an Intel Iris Plus on a driver from
// 2020-2022, lost on its first minutes in Lorencia.
//
// Where a driver is wanted (needsDriver), the launcher asks Windows Update for one of the display
// class while the player signs in (findDriver: no administrator needed to ask). One found is
// offered at the first Play -- Install driver, or Play anyway -- and installed by the launcher run
// again as administrator (installDriver, through Windows' own consent box), from Windows Update:
// the driver Windows itself would put there, signed, for this card, whatever its vendor. A driver
// swap takes every device of the card with it, the launcher's window too, so the launcher closes
// its window for the install and opens again after it (ui.cpp), on the new driver.
//
// Where Windows Update has nothing newer, the vendor's page stays the way (gpu.h driverUrl).
#pragma once

#include <string>

#include "launcher/gpu.h"

namespace mu::launcher {

// No driver of the card's own: Windows' Basic Render or Basic Display adapter, vendor 0x1414,
// which DXGI does not always flag as software.
inline bool driverMissing(const Gpu& gpu) { return gpu.known && (gpu.software || gpu.vendorId == 0x1414); }

// The machine is better off with another driver: none at all, or one kDriverYears old.
inline bool needsDriver(const Gpu& gpu) { return driverMissing(gpu) || (gpu.known && gpu.driverOld); }

// A display driver Windows Update has for this PC and has not installed.
struct DriverOffer {
    std::string id;     // its UpdateID, a GUID: what the installing run is handed
    std::string title;  // Windows Update's own words: "Intel Corporation - Display - 31.0.101.2130"
    std::string model;  // the card it is for: "Intel(R) Iris(R) Plus Graphics"
};

// How an install ended: the installing run's exit code, and the word the launcher opened after it
// is handed (--driver-done).
enum class DriverDone { Installed = 0, Failed = 1, NotFound = 2, Restart = 3, Refused = 4 };
inline const char* doneName(DriverDone done) {
    switch (done) {
        case DriverDone::Installed: return "installed";
        case DriverDone::NotFound: return "none";
        case DriverDone::Restart: return "restart";
        case DriverDone::Refused: return "refused";
        default: return "failed";
    }
}
// The word back to the outcome; Failed for one not known.
inline DriverDone doneNamed(const std::string& name) {
    for (DriverDone done : {DriverDone::Installed, DriverDone::NotFound, DriverDone::Restart, DriverDone::Refused}) {
        if (name == doneName(done)) return done;
    }
    return DriverDone::Failed;
}

#ifdef _WIN32
// Asks Windows Update for display drivers not installed here, and takes the one for `gpu`'s card
// (another card's when that is all there is). Blocks: from seconds to a minute or two, so call it
// on a thread of its own. False with `note` saying why when there is none to offer.
bool findDriver(const Gpu& gpu, DriverOffer& out, std::string& note);

// The player's half: the launcher run again as administrator with --install-driver ID (Windows'
// consent box), waited for. Refused when the box was answered No. Blocks for the whole install.
DriverDone installDriver(const std::string& id);

// The administrator's half, MU2Launcher.exe --install-driver ID: a small window of its own says
// what is happening while the update is downloaded and installed. Returns the exit code.
int installDriverHere(const std::string& id);
#else
// The Mac's driver is the system's, and Linux's the distribution's: nothing to offer.
inline bool findDriver(const Gpu&, DriverOffer&, std::string& note) {
    note = "drivers come with the system here";
    return false;
}
inline DriverDone installDriver(const std::string&) { return DriverDone::NotFound; }
inline int installDriverHere(const std::string&) { return int(DriverDone::NotFound); }
#endif

}  // namespace mu::launcher
