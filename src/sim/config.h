#pragma once

// What a realm is raised under that is a setting and not a rule: the timed gates' clocks and
// the switches a test turns on. Handed to Realm::configure before raise(), and held by the realm
// it was handed to -- never a global, because the world host (server/README.md phase 3) steps every
// map's realm in one process and a mutable global would be shared by all of them (sprint 16,
// step 2). The default is the real game; app/ fills it from the command line's test switches.

namespace mu::sim {

// A gate that opens on the local wall clock, in seconds of the day.
struct GateClock {
    int period;   // between openings
    int opensAt;  // the first opening, after midnight
    int entry;    // how long it stays open

    // How far `daySeconds` is past the last opening.
    constexpr int phase(int daySeconds) const {
        return ((daySeconds - opensAt) % period + period) % period;
    }
    // Seconds left to go in, or 0 when the gate is shut.
    constexpr int entryLeft(int daySeconds) const {
        const int p = phase(daySeconds);
        return p < entry ? entry - p : 0;
    }
};

struct RealmConfig {
    // **Blood Castle's door** (the user, 2026-10-02: 'every 1 hour BC is opened'; 2026-10-07:
    // 'BC has to happen once in 1 hour', 'BC gates is open 5 minutes'): WebZen's hourly default.
    // The Messenger of Archangel in Devias lets a ticket holder in from hh:25 for five minutes
    // (BloodCastle.cpp:778-802, entry closed at hh:30, :1128-1171). The two-minute test clock
    // the user asked for on 2026-10-04 is `--castle-period 120` now, not the default.
    GateClock castle{3600, 25 * 60, 5 * 60};
    // **Devil Square's door**, WebZen 0.97d's (DevilSquare.cpp:843-883): the fight on every odd
    // hour, every two hours, and Charon's gate open the five minutes before it (OPEN 5, the
    // repack's DevilSquare.dat:4), so 00:55 to 01:00, 02:55 to 03:00 ... never with Blood
    // Castle's hh:25. `--square-period S` tests it every S seconds for S/2.
    GateClock square{2 * 3600, 55 * 60, 5 * 60};
    // `--quest-demo`: Peia gives sim::kDemoQuests too, so her window lists them.
    bool questDemo = false;
};

}  // namespace mu::sim
