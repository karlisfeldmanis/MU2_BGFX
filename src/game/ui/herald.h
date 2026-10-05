// The herald: one line at the top of the screen that says an event his level can enter is
// coming, and where to go for it -- "BLOOD CASTLE 2  4:32 | Devias · Messenger".
//
// The proposal of 2026-10-05 (claude.ai/artifact/Wj1TFKh7Df8VgD1sMg8K2M) as the user tuned it:
// option C, the ledger, turned into a slider at the top centre. A soft black band, darkest in its
// middle and gone at both ends, a gold hairline over it and a fainter one under; the name in
// Cinzel, the clock, a thin upright rule and the place. Nothing more ("UX has to be cleaner with
// less text ... its informative"); not even a missing ticket ("dont show that character dont
// have ticket, stick on what we designed").
//
// It is not always up. It speaks when an event first comes within half an hour, briefly; then for
// the last six seconds before its gate, counting down, and on into the gate's opening under the
// green dot, held four seconds, then goes. Several at once take four seconds each. It drops in from the top and leaves
// upward; a second event slides in from the right. While a gate is open a green dot breathes
// before the name; nothing marks an event that is only coming. The cross puts it away at once;
// the next moment still raises it.
//
// Every word is set on one middle line by its own ink: the capitals' height for the name and the
// place, the figures' for the clock, the small capitals' for the state (the user: "all text items
// has to be perfectly align in midle").
//
// Blood Castle on the realm's own clock (sim/event.h); Devil Square on the travel list's
// timetable, counted to as that card does though its square is not built. Chaos Castle and the
// invasions were on the proposal; they join here when the realm has them.
//
// Ours: 0.75 has no event notices; WebZen's server sends the hall a line of text a minute
// (BloodCastle.cpp:778-802), which is the herald's first reason.
#pragma once

#include <cstdint>
#include <string>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Play;

class Herald {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    void close();
    // One frame. Returns true on the frame its cross was clicked, for the desk's click.
    bool update(float seconds, const Play& play, const Pointer& pointer, int width, int height);
    bool covers(float x, float y) const { return alpha_ > 0.0f && close_.has(x, y); }
    bool showing() const { return !canvas_.empty(); }
    const gfx::Canvas& canvas() const { return canvas_; }

    // One event as the line says it.
    struct Call {
        std::string name;    // "Blood Castle 2"
        std::string place;   // "Devias \xB7 Messenger"
        std::string state;   // "gate open", or empty while it is only coming
        int seconds = 0;     // to its start, or left to enter while the gate is open; -1 for none
        bool live = false;   // the gate is open: the green dot
    };

private:
    // An event the herald follows: the start it is counting to and the last moment it spoke at
    // for that start (0 none, 1 within half an hour, 2 within a minute, 3 open).
    struct Source {
        int64_t startAt = 0;
        int spoken = 0;
    };
    static constexpr int kSources = 2;  // Blood Castle, Devil Square
    void raise(int source, float hold, bool pin);
    void rebuild(int width);

    gfx::Canvas canvas_;
    Source sources_[kSources];
    Call calls_[kSources];
    // The showing: which sources it is going through, the one up, and the clocks.
    int queue_[kSources] = {};
    int queued_ = 0;
    int at_ = 0;
    int pinned_ = -1;       // a gate counting down or open: the line stays on it
    float life_ = 0.0f;     // seconds the showing has left
    float dwell_ = 0.0f;    // seconds on the line up
    float age_ = 0.0f;      // seconds since the band came in, for the drop
    float slide_ = 1.0f;    // seconds since the line up came in, for the slide
    float leaving_ = -1.0f; // seconds into the band's going, or -1
    float pulse_ = 0.0f;
    float alpha_ = 0.0f;
    bool hover_ = false;
    gfx::Box close_;
};

}  // namespace mu::game
