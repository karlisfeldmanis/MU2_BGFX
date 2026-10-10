// The party, down the left side at middle height (docs/party.md, sprint 2): each other member's
// name and level, his shield, health and mana, as the server last said them (net::PartyState, a
// second apart). In the Golden Dragon's fight, the rest of the raid under them, a line each.
//
// Each member is one capsule (the user, 2026-10-10, choosing "Capsule with numbers" from
// claude.ai/artifact/AnKTqqjjmk75iDMPcqpojj: "correct direction, which we can use for party and
// also raid"): SD a thin amber band along its top, health the body, mana along its foot, flush and
// rounded as one shape. The name stands on a dark band over the health's left, the level a small
// tag after it, and the number at the right -- his health on a party plate, its percent on a
// raid line. Recent damage leaves a pale trail that drains to the new health, as the boss bar's
// does; a member under 30% pulses. A member on another map dimmed, the fill greyed, the map's
// name for the number; a dead one struck through, "Dead" for the number.
//
// Moved off the top left on 2026-10-10 (the user: "party memeber blocks is blocking experience
// gains and top left corner"): the exp lane keeps that corner.
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"
#include "net/wire.h"

namespace mu::game {

class Play;

class PartyBar {
public:
    void open(const gfx::Interface& interface);
    // A frame: redrawn on a change, and each frame while a trail drains or a member pulses.
    // A left click on a capsule makes him the target (Play::target; the target frame, the
    // user, 2026-10-10), from any map; a right click asks for his menu (game/ui/player_menu.h),
    // which the desk opens.
    void update(float seconds, Play& play, const Pointer& pointer);
    // Whether the pointer is on a capsule: the world does not take the click.
    bool covers(float x, float y) const;
    // A right click on a capsule this frame: whose, and where the menu opens. Taken once.
    bool takeMenuAsk(std::string* name, float* x, float* y) {
        if (menuAsk_.empty()) return false;
        *name = std::exchange(menuAsk_, std::string());
        *x = menuX_;
        *y = menuY_;
        return true;
    }
    bool showing() const { return showing_; }
    const gfx::Canvas& canvas() const { return canvas_; }

    // One of the rest of the raid: not in his party, fighting the dragon with him.
    struct Raider {
        std::string name;
        int kin = -1;    // sim::Kin
        int health = 0;  // percent
        int mana = -1;   // percent, -1 when he has none to show
        int shield = -1; // SD, percent, -1 when he has none
        bool operator==(const Raider& o) const {
            return name == o.name && kin == o.kin && health == o.health && mana == o.mana && shield == o.shield;
        }
    };

private:
    gfx::Canvas canvas_;
    bool showing_ = false;
    net::PartyState drawn_;
    std::vector<Raider> raid_;
    std::string me_;
    float unit_ = 0.0f;
    float clock_ = 0.0f;  // the pulse's phase
    // Each one's trail, by name: the health he had, easing down to the health he has.
    std::unordered_map<std::string, float> trail_;
    // Each capsule as last drawn, for the pointer.
    struct Hit {
        gfx::Box box;
        std::string name;
        bool member = false;  // of his party; else a raider of none of his
        uint32_t here = 0;    // his body in this world, 0 when he is on another map
    };
    std::vector<Hit> hits_;
    std::string menuAsk_;  // takeMenuAsk
    float menuX_ = 0.0f, menuY_ = 0.0f;
    std::string targeted_;  // the target's name as last drawn: his capsule wears the gold edge
    // A press the bar used this frame, which the world must not also take (the user: "i click to
    // whisper and character used click to move").
    bool took_ = false;
};

}  // namespace mu::game
