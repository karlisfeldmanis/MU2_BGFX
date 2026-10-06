#pragma once

// source/raid/party.json, the raid's ten end-game characters (sim/raid.h, docs/golden-dragon-raid.md).

#include <string>
#include <vector>

#include "sim/raid.h"

namespace mu::sim {

// The party in file order, the hero's kit first. False, and the reason in the log, for a file
// that does not read whole.
bool readParty(const std::string& path, std::vector<RaiderKit>* out);

}  // namespace mu::sim
