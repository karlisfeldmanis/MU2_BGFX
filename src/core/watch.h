// The game's black box. A crash or a freeze in the player's own game left nothing behind:
// a force-quit writes no macOS report, and mu2.log is renamed away by the next run, so a
// review run started while the game was up took the player's log with it.
//
// watchStart puts a report in <root>/crashes/ for each:
//   crash-<when>.txt  a fatal signal (a bad pointer, an abort, an uncaught throw): the signal,
//                     the address, the crashing thread's stack and the last 64 KB of the log.
//                     The signal is then raised again, so macOS still writes its own .ips.
//   hang-<when>.txt   no frame for kHangSeconds: the main thread's stack (SIGUSR2, taken by
//                     the main thread itself), the log's tail, then /usr/bin/sample's look at
//                     every thread for two seconds. Taken again at kHangAgainSeconds if the
//                     game is still stuck, and the log says how long the frame was gone if it
//                     comes back. It only reports -- the game is never killed.
#pragma once

namespace mu::core {

// Starts watching, from the main thread: the signal handlers now, the hang watch at the first watchBeat.
void watchStart(const char* root);
// A frame was finished. Called once per frame from the main loop.
void watchBeat();
// Before a wait that is known and long -- a world loading on a handoff -- until the next beat.
void watchPause();
// Before an exit: a clean close is not a hang.
void watchStop();

}  // namespace mu::core
