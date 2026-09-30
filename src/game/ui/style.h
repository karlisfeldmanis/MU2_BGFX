// The interface's one palette and one set of measures: **Sanctuary**, chosen by the user on
// 2026-09-27 from a page of five (docs of it: claude.ai/artifact/KQXbVxJPtnhbfq4Ky8fpas).
// Diablo IV's grammar in MU's windows: iron frames, ash surfaces, one blood-red accent.
//
// Why a file of its own. The audit before it found twelve golds, five copies of one bronze, ten
// text inks and eleven corner radii across `sheet`, `slab`, `tip`, `menu` and `lobby`, each
// window tuned on its own day with its own literals. From here a window asks this page for a
// colour or a size and writes no `rgba()` of its own; the controls that draw buttons, fields
// and frames (game/ui/controls.h, next) are the only other readers.
//
// Nothing reads this yet. It lands first so that every later step changes paint against names
// that already exist, and so that this step alone moves no pixel.
//
// The rules the numbers follow, and what the user turned down while they were tuned:
//   * **Iron is chrome.** Frames, rims, rules and the edges of wells are one dull iron at four
//     strengths. Nothing structural is gold.
//   * **Gold is loot.** Yellow means an item, a refinement or Zen -- `panel::kRefined` and the
//     tooltip's tones, which stay where they are and are never used for chrome.
//   * **Red is the answer.** One accent: the primary button, the thing chosen, a hover's ember.
//     Danger is a red word on iron, not a red fill.
//   * **Clean corners.** A small radius and nothing on it. No diamonds, studs or brackets
//     anywhere; the user refused each on sight.
//   * **Buttons are bold and untracked**, with no stroke round them past their own rim.
//   * **Texture is felt, not seen**: fine stone noise at a few percent, never a pattern.
//
// Colours are sRGB, as the canvas takes them. Sizes are in `tip::unit()` -- one pixel of the
// design page at 1080 lines -- which is the only unit chrome is measured in; the panels keep
// MU's rectangles in their own units and draw their chrome in this one.
#pragma once

#include <cstdint>

#include "gfx/interface.h"

namespace mu::game::style {

// ---- ash: the surfaces, deepest first --------------------------------------------------------

inline constexpr uint32_t kVoid = gfx::rgba(0.020f, 0.012f, 0.012f);  // wells, fields
inline constexpr uint32_t kAsh0 = gfx::rgba(0.027f, 0.020f, 0.016f);  // a sheet's foot
inline constexpr uint32_t kAsh1 = gfx::rgba(0.051f, 0.039f, 0.031f);  // cards, rows
inline constexpr uint32_t kAsh2 = gfx::rgba(0.082f, 0.063f, 0.051f);  // a sheet's top
inline constexpr uint32_t kAsh3 = gfx::rgba(0.110f, 0.082f, 0.067f);  // raised
inline constexpr uint32_t kAsh4 = gfx::rgba(0.141f, 0.106f, 0.086f);  // a button's top
// A sheet is opaque but for a breath of the world: 98%.
inline constexpr float kSheetAlpha = 0.98f;

// ---- iron: every frame, rim and rule ---------------------------------------------------------

inline constexpr uint32_t kIronHi = gfx::rgba(0.659f, 0.545f, 0.424f);  // a rim under the pointer
inline constexpr uint32_t kIron = gfx::rgba(0.420f, 0.337f, 0.271f);    // rims, frames
inline constexpr uint32_t kIronLo = gfx::rgba(0.239f, 0.192f, 0.157f);  // a well's edge
inline constexpr uint32_t kIronDk = gfx::rgba(0.141f, 0.106f, 0.082f);  // a dark rule, a field's edge
inline constexpr uint32_t kSeam = gfx::rgba(0.0f, 0.0f, 0.0f);          // 1u outside a frame
// An item grid's floor and its lines: lifted off kVoid and a shade cooler than the ash, so
// dark steel, MU's reds and the added glows (the Bluewing, the Legendary Shield) stand off it.
inline constexpr uint32_t kCellFloor = gfx::rgba(0.090f, 0.086f, 0.082f);
inline constexpr uint32_t kCellLine = gfx::rgba(0.200f, 0.173f, 0.145f);

// ---- blood: the one accent ---------------------------------------------------------------------

inline constexpr uint32_t kBloodHi = gfx::rgba(0.941f, 0.380f, 0.314f);  // a caret, a lit rim
inline constexpr uint32_t kBlood = gfx::rgba(0.761f, 0.212f, 0.169f);    // the chosen thing
inline constexpr uint32_t kBloodMd = gfx::rgba(0.478f, 0.102f, 0.075f);  // a primary's top
inline constexpr uint32_t kBloodDk = gfx::rgba(0.227f, 0.043f, 0.027f);  // and its foot
// The ember a hover lifts from a button's foot, at its strongest.
inline constexpr float kEmberAlpha = 0.32f;

// ---- bone: the inks ----------------------------------------------------------------------------

inline constexpr uint32_t kBoneHi = gfx::rgba(0.953f, 0.910f, 0.831f);  // titles, values
inline constexpr uint32_t kBone = gfx::rgba(0.851f, 0.812f, 0.741f);    // prose
inline constexpr uint32_t kBone2 = gfx::rgba(0.788f, 0.749f, 0.682f);   // labels, a button's word
inline constexpr uint32_t kAshInk = gfx::rgba(0.561f, 0.522f, 0.459f);  // quiet
inline constexpr uint32_t kAshInk2 = gfx::rgba(0.361f, 0.322f, 0.286f); // inactive
inline constexpr uint32_t kDanger = gfx::rgba(0.878f, 0.478f, 0.424f);  // a danger button's word
// The drop under every letter, as the card has it (`tip::ink::kDrop`).
inline constexpr uint32_t kDrop = gfx::rgba(0.0f, 0.0f, 0.0f, 0.75f);

// ---- meters and the drop target: meaning, not accent ------------------------------------------

inline constexpr uint32_t kLife = gfx::rgba(0.702f, 0.149f, 0.118f);
inline constexpr uint32_t kMana = gfx::rgba(0.184f, 0.392f, 0.788f);
// Green, not MU2's blue: blue already means "has an option" on the loot in the same grid.
inline constexpr uint32_t kFits = gfx::rgba(0.431f, 0.620f, 0.345f);

// ---- measures, in tip::unit() ------------------------------------------------------------------

// Corners: windows and cards, then buttons, rows and fields, then cells and small parts.
inline constexpr float kRadiusSheet = 4.0f;
inline constexpr float kRadiusControl = 3.0f;
inline constexpr float kRadiusSmall = 2.0f;

// A button's three heights, and the two square icon buttons.
inline constexpr float kButtonL = 56.0f;
inline constexpr float kButtonM = 44.0f;
inline constexpr float kButtonS = 32.0f;
inline constexpr float kSmallSquare = 28.0f;  // close, spend
inline constexpr float kIconSquare = 36.0f;   // the coins, the hammers
inline constexpr float kRow = 44.0f;          // a setting's row, a text field

// The frame, outside in: a seam of black, the lit iron edge, a dark gap, a second iron ring.
inline constexpr float kSeamWidth = 1.0f;
inline constexpr float kEdgeWidth = 1.0f;
inline constexpr float kFrameGap = 3.0f;
inline constexpr float kRingWidth = 1.0f;
inline constexpr float kHead = 50.0f;  // a window's head, down to its rule

// Air: a window's padding, between two buttons, a button's word to its edge.
inline constexpr float kPad = 16.0f;
inline constexpr float kGap = 12.0f;
inline constexpr float kWordInset = 24.0f;

// ---- type, in tip::unit() ----------------------------------------------------------------------
//
// Titles are Cinzel, the face the game already says its names in. Buttons are a bold small-caps
// sans with NO tracking -- the user, 2026-09-27: "doont use letter spacing for buttons". Its
// face lands with step 2 (the fonts); until then a caller falls back to the windows' own.

inline constexpr float kTitleSize = 18.0f;
inline constexpr float kTitleTrack = 0.20f;  // ems
inline constexpr float kButtonWordL = 18.0f;
inline constexpr float kButtonWordM = 15.0f;
inline constexpr float kButtonWordS = 13.0f;
inline constexpr float kKickerSize = 12.0f;
inline constexpr float kKickerTrack = 0.14f;
inline constexpr float kBodySize = 15.0f;

// ---- motion, in seconds ------------------------------------------------------------------------

inline constexpr float kHoverSeconds = 0.12f;  // the lift in and out, smoothstepped
inline constexpr float kEmberSeconds = 0.18f;  // the ember rising under a hover
inline constexpr float kOpenSeconds = 0.14f;   // a window's fade and 4u rise
inline constexpr float kOpenRise = 4.0f;
inline constexpr float kCaretSeconds = 1.0f;   // one blink, lit for the first 55%
inline constexpr float kCaretLit = 0.55f;

}  // namespace mu::game::style
