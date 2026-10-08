// The account's characters: who stands on the character screen's five pedestals.
//
// **The server's** (server-plan phase 6, docs/sprints/23-the-account.md): the screen asks for them
// with the account's key (net::Account) and is answered with them (net::Roster), and a character
// is made and deleted by asking (net::Create, net::Delete). Nothing about a character is kept on
// this machine -- the user, 2026-10-08: "we are going away from local saves, we have to use
// server/client envorment". What is: the account's key, made once, and his windows' layout.
//
// The rules are OpenMU's and MuMain's, the server's to keep (sim/cradle.h): five characters an
// account, and a name of letters and digits, four to ten, unique on the server without regard to
// case. The screen refuses a short or symbolled name before it asks, as MuMain's does ("Type more
// than 4 letters").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/tables.h"
#include "game/save.h"
#include "net/wire.h"
#include "sim/cradle.h"
#include "sim/rules.h"

namespace mu::game {

constexpr int kRosterSlots = sim::kRosterSlots;
constexpr int kNameLetters = sim::kNameMost;

// One character as the screen sees him: enough to stand him on his pedestal and name him.
struct Seat {
    std::string name;
    uint64_t token = 0;  // the server's, which his Hello plays him with
    std::string world;   // where he comes in
    int slot = 0;
    sim::Kin kin = sim::Kin::DarkKnight;
    bool second = false;  // his class's second, Sevina's treasure handed in (sim::promoted)
    int level = 1;
    std::vector<Saved::Item> items;  // what he wears, by slot below sim::kWorn
};

// The server's Roster as the pedestals stand it, the worn things read by `tables`' rows.
std::vector<Seat> seatsOf(const net::Roster& roster, const content::Tables& tables);

// The account's key: saves/account.key in the client folder, made the first time it is asked for
// -- 32 hex digits off the system's entropy. It is the account until there is a login screen.
std::string accountKey();

// The characters this machine played on `server` (host:port) before accounts: each save in
// saves/characters with a token for it beside it (Name.server), to be claimed onto the account.
// Read only for that; the server keeps him from then on.
std::vector<net::Claim> claimsFor(const std::string& server);

// Where a character's windows' layout is kept between runs (game::writeLayout beside it): the
// client's own preference, saves/layouts/Name.ui. No save is ever written at the path itself.
std::string layoutBase(const std::string& name);

// Why a name cannot be asked for, or None: the screen's own refusals, before the server's.
enum class Refusal { None, TooShort, Symbols, Taken, NoRoom };
Refusal refusalOf(const std::string& name);

// What a new character is given to hold: OpenMU's Version075 by way of MU2's Cradle.cs -- a
// Small Axe for the knight and a Short Bow for the elf -- and, OURS, the Skull Staff for the
// wizard, whom 0.75 makes empty-handed. The user's choice, 2026-09-28: a bare-handed swing reads
// as nothing, whether MU's flailing fist or the sword's clip with no sword in it, and with a
// staff his left button is a weapon swing as the knight's is. The staff's rise also lifts his
// Energy Ball its 3%. He is given it short of its strength, as the knight is his axe.
const char* cradleWeapon(sim::Kin kin);

// The class as the name plate says it: MU's texts 20-22.
const char* className(sim::Kin kin);

// The bare body a class wears under no armour, by index.json's name: the first class's, or once
// promoted MU's second-class body (HelmClass201-203 and their fellows, ZzzOpenData.cpp:121-127;
// SKIN_CLASS_SOULMASTER and on, _enum.h:3250). `figures` says whether it is cooked: until it is,
// the first class's.
class Figures;
const char* bareBody(sim::Kin kin, bool second, const Figures* figures = nullptr);

}  // namespace mu::game
