// The Sanctuary interface's controls: every button, field, row, switch, slider and frame a
// window draws, in the one palette of game/ui/style.h.
//
// A window lays out its own rectangles and asks here for what goes in them; it writes no colour
// of its own. That is the whole reason for the file: the audit that came before it found each
// window drawing its own buttons with its own literals (`sheet::button`, `slab::draw`, MU's
// sprites), and no two of them agreed.
//
// Everything is drawn in screen pixels, with `u` the caller's unit (tip::unit(), one pixel at
// 1080 lines) handed in so a size in style.h means the same on every screen.
//
// **The stone.** Three faint noises -- stone for a sheet, iron for a button's face, a darker one
// for a well -- are made here at start-up, not loaded: 128 texels square each, side by side in
// one texture, laid 1 texel to 1 pixel so they never swim when a window moves or scales. A
// surface takes one quad a tile over its fill. They are the only texture the controls ask for.
#pragma once

#include <string>

#include <bgfx/bgfx.h>

#include "gfx/interface.h"

namespace mu::game::controls {

// Bakes the button and label faces and makes the stone. False, with the reason in the log, when
// a face is missing; the controls then fall back to the windows' own face and no stone.
bool open();
void close();

enum class Kind : uint8_t { Secondary, Primary, Danger, Quiet };

// What a control is doing. `lift` is the eased hover, 0 to 1; `held` is the pointer down on it;
// `off` is inactive, which wins over both.
struct State {
    float lift = 0.0f;
    bool held = false;
    bool off = false;
};

// ---- surfaces ----------------------------------------------------------------------------------

// The stone noise laid over a box that is already filled: what a card from elsewhere (the item
// tooltip) uses to sit in the same material as the windows.
void grain(gfx::Canvas& canvas, const gfx::Box& box);

// A window: its shadow, the frame's rings from the outside in (seam, gap, ring, seam, the lit
// iron edge), the body on stone, the head's wash and top highlight, the title in Cinzel and the
// rule under the head. `closeBox` comes back as where the head's close button goes, for the
// caller to draw and hit; the frame draws nothing there.
// `head` is the head's height in pixels, 0 for style.h's own; a panel passes MU's band. A title
// too wide for the room between the two ends is shrunk by up to a quarter and then trimmed.
void frame(gfx::Canvas& canvas, const gfx::Box& window, float u, const std::string& title,
           gfx::Box* closeBox = nullptr, float head = 0.0f);
// The foot: the body darkening toward its bottom edge from `top`, under a rule.
void foot(gfx::Canvas& canvas, const gfx::Box& window, float top, float u);
// A framed well: a pit cut into the sheet.
void well(gfx::Canvas& canvas, const gfx::Box& box, float u);
// One cell, in the five states a drop target and the pointer give it.
enum class Cell : uint8_t { Rest, Over, Held, Fits, Blocked };
void cell(gfx::Canvas& canvas, const gfx::Box& box, Cell state, float u);
// A block of resting cells, `columns` by `rows`, ruled with one line between neighbours.
void grid(gfx::Canvas& canvas, const gfx::Box& box, int columns, int rows, float u);
// A quiet iron rule across a sheet, fading at both ends.
void rule(gfx::Canvas& canvas, float x, float y, float wide, float u);
// A tracked small-caps heading over a group: the one place the sans is tracked.
void kicker(gfx::Canvas& canvas, float x, float baseline, const std::string& text, float u);

// ---- buttons -----------------------------------------------------------------------------------

// A worded button, its word in Alegreya Sans SC Bold with no tracking, centred. The word's size
// follows the button's height unless `wordSize` (in pixels) asks for another.
void button(gfx::Canvas& canvas, const gfx::Box& box, const std::string& word, Kind kind,
            const State& state, float u, float wordSize = 0.0f);

// A tab over a page: its word on bare stone over a dark iron floor, the chosen one lit with a
// red bar on the floor and a faint wash rising from it. Tabs laid edge to edge share the floor.
void tab(gfx::Canvas& canvas, const gfx::Box& box, const std::string& word, bool chosen,
         const State& state, float u);

enum class Glyph : uint8_t { Close, Plus, Left, Right, CoinIn, CoinOut, Hammer, Hammers, Undo };
// A small square button (close, spend) or an icon square (the coins, the hammers). `red` is the
// spend's look; `on` is a toggle held down, the repair mode.
void square(gfx::Canvas& canvas, const gfx::Box& box, Glyph glyph, const State& state, float u,
            bool red = false, bool on = false);
// A bare chevron for a setting's value, iron at rest and lit under the pointer.
void chevron(gfx::Canvas& canvas, const gfx::Box& box, bool right, float lift, float u);

// ---- settings and input ------------------------------------------------------------------------

// A setting's row: a rounded well on stone with its label at the left. The value is the caller's.
void row(gfx::Canvas& canvas, const gfx::Box& box, const std::string& label, float lift, float u);
// Two answers side by side, the chosen one filled red: "Off | On".
void toggle(gfx::Canvas& canvas, const gfx::Box& box, const char* left, const char* right,
            bool rightOn, float u);
// A value from 0 to 1 on a track, with an upright handle and the figure beside it.
void slider(gfx::Canvas& canvas, const gfx::Box& box, float value, const std::string& figure,
            float u);
// A text field. An empty `text` shows `placeholder`; `error` rims it red.
void field(gfx::Canvas& canvas, const gfx::Box& box, const std::string& text,
           const std::string& placeholder, bool focus, bool error, bool caret, bool off, float u);
// The line under a refused field.
void message(gfx::Canvas& canvas, float x, float baseline, const std::string& text, float u);
// A small label over a thing the pointer rests on: stone under an iron rim, standing on
// (x, bottom) and centred over x.
void hint(gfx::Canvas& canvas, float x, float bottom, const std::string& text, float u);
// A key cap, "ESC" or "I": an iron-rimmed stone key with its lower edge thicker, the key's name
// in bone, standing at `x` and centred on `midY`. Returns its width.
float keycap(gfx::Canvas& canvas, float x, float midY, const std::string& key, float u);
// The same key filling `box`, its letter sized to the box: for a place the art already sets
// aside, the HUD's strip under its keys and a skill's chip.
void keycap(gfx::Canvas& canvas, const gfx::Box& box, const std::string& key, float u);
// A meter: a dark well and a lit fill.
void meter(gfx::Canvas& canvas, const gfx::Box& box, float share, uint32_t top, uint32_t foot,
           float u);

// ---- type --------------------------------------------------------------------------------------

// A line in the label face (Alegreya Sans Medium) over its drop; returns its width. `size` is
// in pixels.
float label(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink,
            const std::string& text);
float labelWidth(float size, const std::string& text);
// The label face and its texture, for a window that measures, wraps and sets its own lines in
// it; null (and an invalid texture) before open() or when the face failed to bake.
const gfx::Face* labelFace();
bgfx::TextureHandle labelTexture();
// The button words' face (Alegreya Sans SC Bold) and its texture, for a line that sets its own
// small capitals and fades its own drop; null before open() or when the face failed to bake.
const gfx::Face* wordFace();
bgfx::TextureHandle wordTexture();
// The players' names over their heads, in WoW's own face (Friz Quadrata); null before open() or
// when the face is missing from extern/.
const gfx::Face* nameFace();
bgfx::TextureHandle nameTexture();
// The same ranged against `right`.
float ranged(gfx::Canvas& canvas, float right, float baseline, float size, uint32_t ink,
             const std::string& text);
// A tracked small-caps heading at any size and ink.
void caps(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink,
          const std::string& text, float track = 0.14f);
// Its width, for ranging one against a right edge.
float capsWidth(float size, const std::string& text, float track = 0.14f);
// Where a line of `size` sits to be centred in a box `tall` high from `top`, on its capitals.
float middle(float top, float tall, float size);

}  // namespace mu::game::controls
