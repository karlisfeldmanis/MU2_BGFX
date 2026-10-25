// What his Discord says he is doing: "Playing MU Classic", the map, the class and level, the
// party, and how long he has played (Discord's Rich Presence).
//
// Said to the Discord app on his own machine, over its local socket (\\.\pipe\discord-ipc-N on
// Windows, $TMPDIR / $XDG_RUNTIME_DIR discord-ipc-N elsewhere) -- no SDK, no library, nothing
// sent off the machine by us. The application is MU Classic's in the Discord Developer Portal
// (kApplication); its pictures are the Art Assets uploaded there under their keys ("logo").
//
// The socket is a thread's of its own: a Discord that is not running, is starting, or hangs is
// never the frame's to wait on. The modes only say what is true -- the character screen, or a
// world with him in it -- every frame if they like; the thread sends a change at most each
// kGap seconds (Discord takes five in twenty) and tries the socket again each kRetry while
// there is none. Closing the socket, at the end of the run or by the process ending, takes the
// activity off his profile.
#pragma once

#include <cstdint>

namespace mu::app::presence {

// Raised by the Application for a player's run: not a headless one, the studio's, a scripted
// --frames run, nor one with MU2_NO_DISCORD set.
void start();
void stop();

// On the character screen.
void choosing();
// In a world. `map` as the Move list says it ("Lorencia"), `kinName` the class as the plate
// says it, first or second ("Blade Knight"), `party` the members of his party with him, 0 alone.
// Both strings are copied only when they change.
void playing(const char* map, const char* kinName, int32_t level, int32_t rebirths, int party);

}  // namespace mu::app::presence
