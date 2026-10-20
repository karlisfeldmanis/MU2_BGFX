#pragma once

#include <string>

namespace mu::core {

// Opens `url` in the player's own browser, as a link clicked on the desktop does: the shell's
// open on Windows, /usr/bin/open on the Mac, xdg-open on Linux. It returns at once and never
// waits on the browser, so a click in the game's menu does not hold the frame; whether a
// browser came up is not known, and a failure to start one is only logged.
void openInBrowser(const std::string& url);

// The game's Discord: the Escape menu's and the launcher's. The site's short name for the invite
// (web/Caddyfile), so a new invite is a change on the site, not a release of the game.
inline constexpr const char* kDiscordInvite = "https://mu-classic.com/discord";

}  // namespace mu::core
