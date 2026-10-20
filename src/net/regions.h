// The regions, the launcher's and the game's both: the launcher picks one to play in, and the game
// tells a player on a slow line which one is nearer (game/ui/line_chip.h).
#pragma once

#include <string>
#include <vector>

namespace mu::net {

// **A region is a realm of its own** (the user, 2026-10-16, a player in Argentina at 200 ms from
// Helsinki): each one a server with its own world -- players in two regions never meet -- and the
// accounts shared, the website's. The game is started at the region's server (launcher::play). A
// region added here is in every launcher's list and every game's.
//
// **Chosen by ping until the player chooses** (the user, 2026-10-17: "if user is from that region
// it automatically selects that region"): each launcher run times a connect to every region's
// server (pingMs) and plays where it is quickest, unless he has picked one from the list himself.
struct Region {
    std::string id;      // kept in launcher.json; never shown
    std::string name;    // what the player picks
    std::string city;    // where its server is
    std::string server;  // the game's --server: HOST[:PORT]
};
const std::vector<Region>& regions();
// The region kept under `id`, or the first one when none is (a launcher.json from before regions).
const Region& regionNamed(const std::string& id);
// Which region a game's --server is, by its index in regions(): empty is the default host, North
// Europe's. -1 for any other server (a test's, localhost).
int regionOfServer(const std::string& server);
// The round trip to a region's game server, in ms: a TCP connect to it timed, the quicker of two
// (the first may have paid for the name's lookup). -1 when it does not answer within `seconds`.
// Blocks: call it off the screen's thread.
double pingMs(const Region& region, double seconds = 2.0);
// The region with the quickest of `pings` (one a region, in regions()' order), or "" when none
// answered.
std::string quickest(const std::vector<double>& pings);
// The region a player in region `here` would do much better in: the quickest of `pings` other
// than his, when it answered at least `by` ms sooner than his own did, timed the same way (a
// connect, not the game's ping, which carries the server's side as well). -1 for none.
int nearer(const std::vector<double>& pings, int here, double by = 80.0);

// **Every region timed once, in the background** (the game's, 2026-10-20: a slow line is told of a
// nearer region): the first call starts a thread that times each in turn; later calls do nothing.
// It lives as long as the process, so a map change, which starts a new play mode, keeps what it
// found. `pingAllTaken` fills `pings` (one a region, -1 for none that answered) once it is done.
void pingAllOnce();
bool pingAllTaken(std::vector<double>* pings);

}  // namespace mu::net
