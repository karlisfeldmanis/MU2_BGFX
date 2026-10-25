// A line as it is seen rather than as it was typed: Hebrew and Arabic run right to left, and
// Arabic's letters take their joined forms (Crylos on Discord, 2026-10-24: "RTL-oriented
// languages are reversed when typing (Arabic, Hebrew etc)"). The text is held in typed order
// everywhere -- the wire, the chat log, the draft -- and turned only where a line is drawn
// or measured.
#pragma once

#include <string>

namespace mu::core::bidi {

// `s` in the order its letters are drawn, left to right. A line with no right-to-left letter
// is `s` itself, untouched and unallocated; otherwise a buffer of this thread's, good until
// the next call.
//
// Unicode's bidi algorithm cut to one line of chat: no embeddings or isolates (the server takes
// the controls out), its direction the first strong letter's, numbers kept left to right
// within a right-to-left run, the brackets in one mirrored. Arabic is joined from the
// presentation forms (Arabic and Persian letters, and lam-alef), which the fallback faces have.
const std::string& visual(const std::string& s);

// True when the first strong letter of `s` is a right-to-left one: a line that reads from the
// right, whose typing point is at its left.
bool rightToLeft(const std::string& s);

}  // namespace mu::core::bidi
