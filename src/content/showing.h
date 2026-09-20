// The cooked showing: which sheet an effect draws with, and which files an event sounds.
// Written by tools/cook.py's cook_showing, which is the only thing that writes it.
//
// Like content/tables.h and content/cooked.h, this header knows nothing about bgfx: parsing
// a file and uploading a texture are two jobs and only the second needs a device. That is
// what lets tests/ read one without a window, as foundation 9 of PLAN.md asks.
//
// Neither half is per-world. An effect belongs to a blow and a sound to an event, so both
// live in assets/cooked/showing beside the figures rather than under a map.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::content {

// One named effect: MU2's own name for it and the one sheet it draws with, cooked to a .ktx
// with its mip chain. The blend mode is NOT here -- index.json carries `blend` per missile
// part and the rest of MU's effects take theirs from what they are, so it is the caller's
// and not the sheet's.
struct EffectSheet {
    std::string name;   // "hit_blood", "move_marker"
    std::string path;   // relative to assets/
};

// One sound event, which is one to four interchangeable files MU picks between at random.
struct SoundEvent {
    std::string name;   // "melee_hit"
    // Seconds of silence at the head of the file. MU's wavs have a silent lead of up to a
    // quarter of a second, and a cue that fires on the FILE rather than on the SOUND lands
    // that late. Already measured by MU2's pipeline; carried, never rediscovered.
    float onset = 0.0f;
    // A per-event trim in decibels, as the asset states it. Decibels and not a linear factor
    // on purpose: converting here would put the engine's arithmetic into a table of facts.
    float gainDb = 0.0f;
    std::vector<std::string> files;  // relative to assets/
};

struct Showing {
    // What every cooked sound was made, and what a mixer can therefore assume of all of
    // them. Checked rather than trusted: a table cooked mono at 22050 and read by a device
    // set up for something else is a fight that is quietly the wrong pitch.
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    std::vector<EffectSheet> effects;
    std::vector<SoundEvent> events;

    // By the name index.json gives it. Null for a name nothing cooked, which a caller must
    // treat as "draw nothing" rather than as a reason to stop.
    const EffectSheet* effect(const std::string& name) const {
        for (const EffectSheet& one : effects) {
            if (one.name == name) return &one;
        }
        return nullptr;
    }
    const SoundEvent* event(const std::string& name) const {
        for (const SoundEvent& one : events) {
            if (one.name == name) return &one;
        }
        return nullptr;
    }
};

// Both fill `error` with a sentence rather than logging: the caller knows which file it
// asked for. Every count in the file is treated as hostile.
bool parseShowing(const std::vector<uint8_t>& bytes, Showing& out, std::string& error);
bool loadShowing(const std::string& path, Showing& out, std::string& error);

}  // namespace mu::content
