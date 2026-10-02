// The character, kept between runs: where he stands, what he has earned, what he carries and
// wears, and the four potion keys. PLAN.md's "one versioned file written whole".
//
// Items are written by MU's own (group, number) and never by the cooked table's row: a recook
// that adds or orders a row differently would otherwise turn a saved sword into whatever now
// sits at its index. A pair the tables no longer know is dropped on load and said so.
//
// Written to a temporary beside the file and renamed over it, so a crash mid-write leaves the
// last good save rather than half of a new one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/realm.h"

namespace mu::game {

struct Saved {
    // Who he is on the character screen (game/roster.h): the name he was made with and the
    // pedestal he stands on, 0 to 4. A file from before the screen has neither, and reads as
    // no name and slot -1; the roster names it when it takes it in.
    std::string name;
    int slot = -1;
    // Made on the character screen and never played: the class and nothing else is his yet,
    // so the game makes him as a new character is made, at the town's gate with the class's
    // own weapon, and the first save fills the rest in. Never written back.
    bool fresh = false;
    std::string world;
    sim::HeroRecord hero;             // hero.slots filled by resolveSave
    int32_t quick[5] = {-1, -1, -1, -1, -1};  // item rows, -1 for none; filled by resolveSave
    // What is on Q W E R, by MU's own skill number, 0 for an empty key. Saved for the same
    // reason the potion keys are and against MuMain, which has no SaveHotKey: 0.75's bar could
    // not be arranged -- a knight's skill was whatever weapon was in his hand -- and this one
    // can, so an arrangement the player made by hand is his and not the session's.
    // The sixth is the right button's quick slot (Hud::kRightSlot); a file from before it had
    // five and reads its sixth as empty.
    int32_t bar[6] = {0, 0, 0, 0, 0, 0};
    // There was a `zoom` here, the metres the camera stood back, because the wheel could
    // change it. The camera is fixed at MU's 8 m since 2026-09-24 and there is nothing to
    // keep; an older file's own `zoom` key is read by nothing and ignored.

    // As read, before the tables are there to turn them into rows. The file is read before
    // the world is -- it decides the class, the level and the tile the world is raised at --
    // and the item tables arrive with the realm, so the two steps are apart.
    struct Item {
        int slot = -1, group = -1, number = -1, plus = 0, durability = 0;
        bool skill = false;
        // Luck and the additional option's level; absent in a file from before drops rolled them.
        bool luck = false;
        int option = 0;
        int excellent = 0;  // its excellent options, a bit each
        // Its sockets and the power set in each, or a Rune of Creation's own in the first
        // (sim/items.h). Absent in a file from before sockets.
        int sockets = 0;
        int powers[3] = {0, 0, 0};
        // Whether the file recorded wear. A file written before gear had durability says 0 for
        // every sword, and that 0 is not "broken" -- it is "never counted", read back as full.
        bool worn = false;
    };
    std::vector<Item> items;
    int quickGroup[5] = {-1, -1, -1, -1, -1}, quickNumber[5] = {-1, -1, -1, -1, -1};

    // The vault, read from its own file (see vaultPathBeside). Its items as read, `slot` a vault
    // cell; turned into a sim::Vault by resolveVault once the tables are there.
    int64_t vaultZen = 0;
    std::vector<Item> vaultItems;

    // What was left in the Chaos Machine's box (sim/machine.h): its items as read, `slot` a cell,
    // and the box itself once resolveSave has the tables -- and on the way out, the realm's box.
    // Absent in a file from before the machine, and in one whose box was empty.
    std::vector<Item> machineItems;
    sim::Machine machine;
};

// Where the save lives when --save does not say: saves/hero.json in the client folder
// (core::userFolder), which git ignores, so a clean rebuild or a checkout never touches it.
std::string defaultSavePath();

// False when there is no file or it cannot be read as a save; the reason is in the log.
bool loadSave(const std::string& path, Saved& out);
// Turns what loadSave read into item rows, into hero.slots and quick.
void resolveSave(const content::Tables& tables, Saved& saved);
bool writeSave(const std::string& path, const content::Tables& tables, const Saved& saved);

// **The vault is the account's, not the character's**, as MU's is (OpenMU keeps it on
// `Account.Vault`), so it is its own file beside the character's -- vault.json in the save's
// folder, or in the folder above when the save is one of the roster's in `characters/` -- and
// every character on the account finds what the last one left. Read into `saved.vaultZen` and
// `saved.vaultItems`; false when there is none, which is an empty vault and not an error.
std::string vaultPathBeside(const std::string& savePath);
bool loadVault(const std::string& path, Saved& saved);
sim::Vault resolveVault(const content::Tables& tables, const Saved& saved);
bool writeVault(const std::string& path, const content::Tables& tables, const sim::Vault& vault);

}  // namespace mu::game
