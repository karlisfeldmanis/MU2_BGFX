// The studio's world marks (docs/order-plan.md §9c, sprint G): a world's gates and its
// townsfolk's spots, drawn over the land in Place and moved there.
//
// Both are the world's manifest, source/world/<w>/world.json (sprint D3): `gates.enters` (a
// box of tiles, the level it asks, the gate it sends to) and `gates.exits` (a box and the way
// a character comes out facing), and `npcs` (a townsperson's tile and the way they look). A
// click on the land picks the mark there -- the townsperson within a tile and a half, or the
// smallest gate box the tile is in -- and a drag moves it by whole tiles; the window holds its
// numbers. Each is written when let go, one value at a time through mu serve's edit, and the
// manifest is read again, so what is drawn is what the file says.
//
// A townsperson can be duplicated (a tile east) and deleted, list operations of
// tools/mu/edit.py. A gate is added and deleted with its pair, its way in here and its way out
// in the world it leads to (tools/mu/gates.py, `mu gate`); a gate is not duplicated.
//
// Neither shows in the game until it is cooked: the gates are compiled into the realm (`mu
// make realm`, then a rebuild of the game and the server), the townsfolk stood by the world's
// tables (`mu make tables <w>`). The card's target says which. A townsperson's look is MU2's
// Look, 1 West clockwise to 8 NorthWest (tools/cook.py's folk).
#pragma once

#include <string>
#include <vector>

#include "core/json.h"
#include "gfx/renderer.h"

namespace mu::game {
class World;
}
namespace mu::studio {
class Cooker;
}

namespace mu::app {

struct Context;

// The row over Place's window that says what is placed: 0 objects, 1 gates, 2 townsfolk.
int placeModeBar(float x, float y, float w, int mode);

class StudioMarks {
public:
    enum class Kind { Gates, Townsfolk };

    // The world, its manifest read when the world changes. Called each frame with the scene.
    void world(Context& ctx, const std::string& name);
    void kind(Context& ctx, Kind k);
    Kind kind() const { return kind_; }
    // `mu make`'s target for the card: what cooks this kind into the game.
    std::string target() const { return kind_ == Kind::Gates ? "realm" : "tables " + world_; }
    // A click picks, a drag moves; true when the pointer was the marks' this frame.
    bool pointer(Context& ctx, game::World& world, const gfx::Camera& camera, bool overPanels);
    // The window in the inspector's place, and every mark of the kind drawn over the land.
    void panel(Context& ctx, game::World& world, studio::Cooker& cooker, const gfx::Camera& camera,
               float x, float y, float w, float h);
    void drop() { picked_ = -1; }

    // For the control words: a gate by its number or a townsperson by their index, picked;
    // the picked moved by whole tiles and written; a townsperson's look set; duplicate, delete.
    bool pick(int which);
    bool budge(Context& ctx, studio::Cooker& cooker, int columns, int rows, std::string* answer);
    bool look(Context& ctx, studio::Cooker& cooker, int look, std::string* answer);
    bool duplicate(Context& ctx, studio::Cooker& cooker, std::string* answer);
    bool remove(Context& ctx, studio::Cooker& cooker, std::string* answer);
    // A gate with its pair (tools/mu/gates.py): a way in at (column, row) here, its way out on
    // `to`'s arrival tile, picked. remove() in the gates takes the picked one with its pair.
    bool addWay(Context& ctx, studio::Cooker& cooker, const std::string& to, int column, int row,
                std::string* answer);
    int picked() const { return picked_; }

private:
    // One gate or one townsperson, as the manifest holds it and as it is now.
    struct Mark {
        std::string list;   // "enters", "exits" or "" for a townsperson
        int index = 0;      // in its list
        int number = 0;     // a gate's number, a townsperson's npc
        std::string name;   // a townsperson's
        int box[4] = {0, 0, 0, 0};   // a gate's tiles, x1 y1 x2 y2, both ends in; a person's at[0..1]
        int level = 0, target = 0;   // an enter's
        int faces[2] = {0, 0};       // an exit's; a person's look in faces[0]
        int stored[4] = {0, 0, 0, 0};
        int storedLevel = 0, storedTarget = 0, storedFaces[2] = {0, 0};
    };
    void load(Context& ctx);
    bool write(Context& ctx, studio::Cooker& cooker, std::string* answer);
    std::string path(const Mark& m, const char* field, int element) const;
    void draw(Context& ctx, game::World& world, const gfx::Camera& camera) const;

    std::string world_, file_;
    Kind kind_ = Kind::Townsfolk;
    core::Json manifest_;
    std::vector<Mark> marks_;   // of the kind
    std::vector<std::string> worlds_;   // the worlds a new gate may lead to: with rules.arrive
    int leadsTo_ = 0;
    int picked_ = -1;           // in marks_
    bool dragging_ = false, released_ = false;
    int grab_[2] = {0, 0};      // the pointer's tile less the mark's, at the press
    std::string said_;
    bool saidOk_ = true;
};

}  // namespace mu::app
