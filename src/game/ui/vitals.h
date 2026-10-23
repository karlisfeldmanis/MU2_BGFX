// The monster under the pointer: its name, and under it a rounded, lit, shadowed bar with the
// two health figures printed small inside it, floated over its head.
//
// MU2's `client/core/Vitals.cs`, and ours rather than MU's. MU's own readout is
// `CNewUINameWindow::RenderName`'s monster branch -- a name and twenty segments pinned to the
// top edge of the screen -- and MU2 built that first and replaced it, because a top-edge banner
// cannot say WHICH of the six around you it describes. Vitals.cs carries the long reasons for
// each of its numbers; they are copied here without the essays.
//
// What is kept: the fixed width (the length is the reading, so it cannot depend on the name or
// the monster), the pale trail that hangs a moment and then closes on the health, the quick
// fade up and the short linger after the pointer leaves, the longer eased fade on a kill, and
// the health as SHOWN rather than as the realm has it, so the red never drops before the number
// that took it goes up.
//
// And the wait for the fall, as MU2 had it: the realm kills a monster on the tick and the
// drawing holds the collapse for the killing blow's landing cue, so the bar reads
// Play::shownAlive and shownHealth, and the pick keeps a body that is still standing
// (Play::point). And MU2's Escort, since sprint 15: the elf's summon wears a small green bar
// over its head for as long as it lives, whether pointed at or not (the user, 2026-09-28: "a
// minimal HP bar for elf summons which is always visible"). No name -- green, because red over
// something on her side reads as a thing to hit (Vitals.cs's Friendly) -- and its figures inside
// it, at the hover bar's proportion of type to bar (the user, 2026-10-02: "show HP number for elf
// summons based on hp bar scale").
//
// Godot drew the rounded pieces with StyleBoxFlat; the canvas has no such thing, so every
// shape is laid down one pixel row at a time, each row cut to the rounded outline and its two
// ends antialiased by coverage. A bar is fifteen-odd rows, so a layer is fifteen-odd quads.
//
// **And every player, his own included, WoW's way** (the user, 2026-10-08: "use wow style name labels for
// players and bots"): his name always over his head while he is in range, in WoW's own Friz
// Quadrata with a hard black outline and no backing, in WoW's friendly green, sliding with him by
// fractions of a pixel; under it his guild in angle brackets -- `<Bot>` for a bot, until guilds
// exist. Pointed at, the name lifts toward white (and
// he is ringed green, app/modes/play_mode.cpp); dead, it is grey. It fades out toward the edge of
// the range, and the nearer names are drawn over the farther. No bar. Pointed at, a small line
// under it all gives his level and class (the user chose W4 of the WoW names page, 2026-10-08).
// A reborn (sim/rebirth.h) wears a mark over his name: small spaced gold capitals between two
// fading gold hairlines, "REBIRTH" and his numeral (the user, 2026-10-23).
//
// **And what the fight laid on the monster, under its bar** (the user, 2026-10-16: "we need to show
// debufs under monster hp bar, for example if char does poison,fire,bleed,slow,etc"): a small cell
// each for poison, burning, bleeding (its stacks in the corner), iced and frozen, in the HUD's own
// icons and the target frame's red edge, each one's time a clock swept round it. A debuff's whole
// is not on the body -- a poison lasts as long as the skill that laid it says -- so the longest
// time left seen on this monster stands for it, and a fresh one starts its clock full.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gfx/interface.h"
#include "net/wire.h"

namespace mu::game {

class Play;
namespace panel {
class Arts;
}

class Vitals {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts) {
        interface.adopt(canvas_);
        arts_ = arts;
    }

    // A frame. `pointed` is the body under the pointer or 0, polled every frame as MU2's Show
    // was -- what is on screen after the pointer leaves is the last monster's bar living out its
    // linger. `paneled` is whether a window has the pointer, which hides a new reading but lets
    // one already up run out. Rebuilds the canvas only when what it draws moved.
    // `folk` is the townsperson under the pointer or -1: his name alone, with no bar, on the
    // same fade and linger. MU names an NPC over his head (CNewUINameWindow's NPC branch) and
    // has nothing to say about his health.
    void update(float seconds, const Play& play, uint32_t pointed, int folk, bool paneled,
                const float* viewProj, int width, int height);
    // Takes it down now, without the linger or the fade: leaving the world, not the monster.
    void dismiss();

    bool showing() const {
        return (on_ != 0 && shown_ > 0.0f) || (folk_ >= 0 && folkShown_ > 0.0f) ||
               drawn_.escort != 0 || !drawn_.tags.empty();
    }
    // How far a townsperson's name is shown this frame, 0 to 1, on its own fade and linger: what
    // the quest marker over him rises by, so the two move as one.
    float folkShown(int folk) const { return folk >= 0 && folk_ == folk ? folkShown_ : 0.0f; }
    // The townsperson whose name is up, pointed at or still lingering, or -1.
    int namedFolk() const { return folk_; }
    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    // One player's name over his head.
    struct Tag {
        uint32_t id = 0;
        float x = 0, y = 0;
        float alpha = 0, lit = 0;  // the range's fade, and the pointer's lift toward white
        std::string name;
        bool bot = false, dead = false;
        // His guild's name and mark (docs/game/guild.md, the user's pick A): the line under his name.
        std::string guild;
        net::GuildMarkBits mark{};
        // His rebirths (sim/rebirth.h): a reborn's mark over his name, 0 for none.
        int rebirths = 0;
        bool operator==(const Tag& o) const {
            return id == o.id && x == o.x && y == o.y && alpha == o.alpha && lit == o.lit &&
                   name == o.name && bot == o.bot && dead == o.dead && guild == o.guild && mark == o.mark &&
                   rebirths == o.rebirths;
        }
    };
    // One debuff's cell under the monster's bar: its icon (Hud::artOf), what is left of it in
    // sixteenths, and a bleed's stacks.
    struct Bane {
        const char* art = nullptr;
        float share = 0;
        int stacks = 0;
        bool operator==(const Bane& o) const {
            return art == o.art && share == o.share && stacks == o.stacks;
        }
    };
    static constexpr int kBanes = 5;  // poison, burning, bleeding, iced, frozen
    // Everything the canvas draws, compared whole to decide a rebuild.
    struct Readout {
        uint32_t on = 0;
        float x = 0, y = 0;
        float shown = 0, lag = 0, health = 0;
        int reading = 0, maximum = 0;
        float unit = 0;
        std::vector<Bane> banes;
        int folk = -1;
        float folkX = 0, folkY = 0, folkShown = 0;
        // Her summon's escort bar, 0 for none showing (sprint 15).
        uint32_t escort = 0;
        float escortX = 0, escortY = 0, escortHealth = 0;
        int escortReading = 0, escortMaximum = 0;
        // The other players' names, farthest first.
        std::vector<Tag> tags;
        bool operator==(const Readout& o) const {
            return on == o.on && x == o.x && y == o.y && shown == o.shown && lag == o.lag &&
                   health == o.health && reading == o.reading && maximum == o.maximum &&
                   unit == o.unit && banes == o.banes && folk == o.folk && folkX == o.folkX && folkY == o.folkY &&
                   folkShown == o.folkShown && escort == o.escort && escortX == o.escortX &&
                   escortY == o.escortY && escortHealth == o.escortHealth &&
                   escortReading == o.escortReading && escortMaximum == o.escortMaximum &&
                   tags == o.tags;
        }
    };

    void trail(float now, float seconds);
    void rebuild(const Play& play, const Readout& r);

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    // The longest time left seen of each of the monster's debuffs, in ticks, in kBanes' order:
    // what its clock sweeps against. Let go when the debuff is, and when the bar moves to another.
    int64_t baneWhole_[kBanes] = {};
    uint32_t on_ = 0;
    bool alive_ = false;   // whether it was alive last frame, so the kill can be caught
    float left_ = 0.0f;    // seconds of the linger left
    float shown_ = 0.0f;   // the fade, 0 to 1
    float lag_ = 0.0f;     // the trail's edge, as a fraction of the bar
    float was_ = 0.0f;     // the fraction last frame, so a drop can be noticed
    float holding_ = 0.0f; // seconds before the trail starts closing
    int folk_ = -1;        // the townsperson named, by the tables' folk index
    float folkLeft_ = 0.0f, folkShown_ = 0.0f;
    uint32_t player_ = 0;  // the other player named, by his body's id
    float playerLeft_ = 0.0f, playerShown_ = 0.0f;
    Readout drawn_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
