// A message in the system's own box, for when the game cannot open its window at all or must not:
// a player's build started without the launcher (application.cpp). Blocks until it is closed.
#pragma once

namespace mu::app {

void systemAlert(const char* title, const char* text);

}  // namespace mu::app
