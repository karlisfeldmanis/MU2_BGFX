// What a release has that an older one does not, asked at compile time so one dump builds
// against every release it is pointed at (5d7ddebc, 2eec46a1, HEAD). A function is found by its
// arguments in a template, so a missing one is a branch not taken, not an error; a constant is
// armory.cmake's armory_probe. A release that lacks a rule gets the field left out, never a guess.
#pragma once

#include "content/tables.h"
#include "sim/items.h"

namespace armory::compat {

// sim::runeLevel (the level a socketed rune asks), 0 where the release has no rune levels.
template <class H>
int runeLevel(const mu::content::Tables& t, const H& held) {
    if constexpr (requires { runeLevel(t, held); }) {
        return runeLevel(t, held);  // mu::sim's, by its arguments
    } else {
        return 0;
    }
}

// sim::piecesSold (how many of a stack the counter takes), 0 where the release has none.
template <class H>
int piecesSold(const mu::content::Tables& t, const H& held) {
    if constexpr (requires { piecesSold(t, held); }) {
        return piecesSold(t, held);
    } else {
        return 0;
    }
}

// A quest item's rank (QuestItem::Rank): "first", "second" or "any".
template <class Q>
const char* rankOf(const Q& q) {
    if constexpr (requires { q.rank; }) {
        using R = decltype(q.rank);
        return q.rank == R::First ? "first" : q.rank == R::Second ? "second" : "any";
    } else {
        return "any";
    }
}

}  // namespace armory::compat
