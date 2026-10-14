// The game's black box. A crash or a freeze in the player's own game left nothing behind:
// a force-quit writes no macOS report, and mu2.log is renamed away by the next run, so a
// review run started while the game was up took the player's log with it.
//
// watchStart puts a report in `dir` (core::folders().crashes) for each:
//   crash-<when>.txt  a fatal signal (a bad pointer, an abort, an uncaught throw): the signal,
//                     the address, the crashing thread's stack and the last 64 KB of the log.
//                     The signal is then raised again, so macOS still writes its own .ips.
//   hang-<when>.txt   no frame for kHangSeconds: the main thread's stack (SIGUSR2, taken by
//                     the main thread itself), the log's tail, then /usr/bin/sample's look at
//                     every thread for two seconds. Taken again at kHangAgainSeconds if the
//                     game is still stuck, and the log says how long the frame was gone if it
//                     comes back. It only reports -- the game is never killed.
// Each report starts with the run's notes (watchNote): which build, which card, which renderer.
// The launcher's own are launcher-crash-<when>.txt, its `name` in front. The launcher and the
// game send them to the website on their next start (launcher/crash_upload.h).
#pragma once

namespace mu::core {

// Starts watching, from the main thread: the signal handlers now, the hang watch at the first watchBeat.
// `name` goes in front of each report's file name ("launcher-").
void watchStart(const char* dir, const char* name = "");
// A line every report from here on starts with ("MU2 0.9.4 3965dc81", "Vulkan, NVIDIA 10de:2882").
// Kept in a fixed buffer, so the crash handler reads it without allocating.
void watchNote(const char* line);
// A frame was finished. Called once per frame from the main loop.
void watchBeat();
// Before a wait that is known and long -- a world loading on a handoff -- until the next beat.
void watchPause();
// Before an exit: a clean close is not a hang.
void watchStop();
// A stop the program chose, before it aborts -- bgfx's fatal (a device lost), an uncaught throw:
// the crash report written now, with `what` first, since on Windows abort() passes no handler.
void watchReport(const char* what);

}  // namespace mu::core
