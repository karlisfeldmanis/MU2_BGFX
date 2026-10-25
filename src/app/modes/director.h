// The director: a recording's hand on the mouse and the keys, read from a cue file
// (--script FILE), so a showcase is played the same way every time and the pointer drawn in it
// is the script's, never the mouse's. While a script runs the real mouse is not read at all.
//
// A cue is a line: the second of game time it fires on, a verb, and the verb's words. It fires
// on the first frame whose clock has reached it, so a recording at 30 or 60 frames a second
// plays the same scene. tools/mu/record.py reads the same file for its `args` and `end` lines,
// which the game skips.
//
//   # a comment
//   args --arena bull_fighter --no-hand     the game's switches (record.py's, skipped here)
//   end 20                                  the recording's length in seconds (record.py's)
//   0.0  move 0.5 0.5                       the pointer to a screen fraction, at once
//   0.5  move 0.62 0.40 0.8                 ...or glided there over 0.8 s (default 0.5)
//   1.5  aim monster 0.6                    glided onto the nearest monster, then kept on it
//   1.5  aim loot | aim hero | aim tile C R the nearest drop, the hero, a tile, the same way
//   1.5  aim elite                         the nearest Elite (sim/elite.h), the same way
//   2.2  click | rclick                     the left or right button, pressed and let go
//   2.2  hold 1.5 | rhold 1.5               ...or held down for that long
//   3.0  skill q | key 1 | type enter       a skill key (q w e r t), a potion key, typing
//                                           (type bag, journal, guild: those windows' keys)
//   3.0  hold-skill q 1.2                   a skill key held down for that long
//   3.0  shift-key 2                        a potion key with Shift: the Dark Raven's mode
//   0.2  run 1 -1                           run on along tile direction (1,-1): the pointer kept
//                                           five tiles ahead of him, clicking as a player does
//   9.0  stop                               ...until this
//   2.5  wear ArmorMale10:1:+7W Sword10:1:+7W  put on at once, as --give's W does; local only
//   2.5  unwear 12                          a worn slot back into the bag (12 the mount)
//   2.5  flare                              the level-up's flares and sound on him
//   2.0  pin CLIP HERO BEAT PULSE GALLOP    a cut's clocks (record.py; Play::Stride), -1 for none
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mu::app {

class Director {
public:
    // What the world answers the director's aims with. All in tiles and backbuffer pixels.
    struct Eyes {
        // A tile's place on the screen; false when it is behind the camera.
        std::function<bool(float column, float row, float* x, float* y)> toScreen;
        // The nearest living monster's id, or 0.
        std::function<uint32_t()> nearestMonster;
        // The nearest living Elite's id (sim/elite.h), or 0.
        std::function<uint32_t()> nearestElite;
        // Where a body is drawn now; false when it is gone.
        std::function<bool(uint32_t id, float* column, float* row)> shownAt;
        // The nearest drop's tile; false when nothing lies near.
        std::function<bool(float* column, float* row)> nearestLoot;
        // The hero's id.
        uint32_t hero = 0;
    };

    // This frame's hand: where the pointer is and what was pressed.
    struct Hand {
        float x = 0.0f, y = 0.0f;
        bool pressed = false, released = false, held = false;
        bool rightPressed = false, rightHeld = false;
        int potion = -1;     // 0..3
        bool shifted = false;  // ...pressed with Shift (shift-key)
        int skill = -1;      // 0 is Q
        int holdSkill = -1;  // held this frame
        std::string typed;
        std::vector<std::string> wear;  // NAME:COUNT:EXTRAS, as --give takes them
        int unwear = -1;
        bool flare = false;
        // `run`'s click: a walk to this tile, never an attack on what wandered under the pointer.
        bool walk = false;
        int walkColumn = 0, walkRow = 0;
        bool pin = false;
        int pinClip = -1;
        float pinHero = -1.0f, pinBeat = -1.0f, pinPulse = -1.0f, pinGallop = -1.0f;
    };

    // False on a file that cannot be read or a line that cannot be understood; the error is
    // logged with its line number.
    bool open(const std::string& path);
    bool on() const { return on_; }

    Hand step(double seconds, float width, float height, const Eyes& eyes);

private:
    enum class Verb : uint8_t { Move, AimMonster, AimElite, AimLoot, AimHero, AimTile, Click, RightClick,
                                Hold, RightHold, Skill, Potion, HoldSkill, Type, Run, Stop, Wear,
                                Unwear, Flare, Pin };
    struct Cue {
        double at = 0.0;
        Verb verb = Verb::Move;
        float a = 0.0f, b = 0.0f;  // a screen fraction or a tile
        float seconds = 0.0f;      // a glide's length or a hold's
        int key = -1;
        bool shifted = false;  // shift-key
        std::string text;
        std::vector<std::string> words;
        float pins[5] = {-1, -1, -1, -1, -1};
        int line = 0;
    };
    enum class Aim : uint8_t { Screen, Body, Tile, Ahead };

    bool target(const Eyes& eyes, float width, float height, float* x, float* y) const;

    bool on_ = false;
    std::vector<Cue> cues_;
    size_t next_ = 0;
    // The glide: from where, since when, for how long, toward what.
    float x_ = -1.0f, y_ = -1.0f, fromX_ = 0.0f, fromY_ = 0.0f;
    double glideAt_ = 0.0;
    float glideFor_ = 0.0f;
    Aim aim_ = Aim::Screen;
    float aimA_ = 0.5f, aimB_ = 0.5f;
    uint32_t aimBody_ = 0;
    // The buttons and the held skill, until when.
    double leftUntil_ = -1.0, rightUntil_ = -1.0, skillUntil_ = -1.0;
    bool leftDown_ = false, rightDown_ = false;
    int heldSkill_ = -1;
    // `run`: the direction in tiles, and when the next click is due.
    bool running_ = false;
    double clickAt_ = 0.0;
};

}  // namespace mu::app
