// The cooked missiles: what a blow throws, and the mesh it throws.
//
// Written by tools/cook.py's cook_missiles, which is the only thing that writes it. A row
// names a .mum exactly like the town's models and the wardrobe's items, so the thing thrown
// is loaded with content::parseCookedMesh and drawn as an ordinary lit gfx::Drawable --
// there is no run-time .obj parser and no second material model on this path any more.
//
// Like content/showing.h and content/cooked.h, this header knows nothing about bgfx:
// parsing a file and uploading a texture are two jobs and only the second needs a device.
// That is what lets tests/ read one without a window, as foundation 9 of PLAN.md asks.
//
// Not per-world, and beside the effect sheets and the sounds in assets/cooked/showing for
// the same reason they are: a missile belongs to a blow and not to a map.
#pragma once

#include <string>
#include <vector>

namespace mu::content {

// One submesh of a missile, in the order the cook wrote the parts and therefore in the order
// the .mum's parts and materials run: part i of the mesh is this row.
//
// `additive` is index.json's `blend`, resolved. It is already in the mesh's own material as
// the glow flag -- MU's BlendMesh, which the renderer takes out of the shadow, the prepass
// and the shade and adds in the transparent pass -- and it is repeated here because what a
// part is made of is a fact about the missile that a caller may want before it has a device.
struct MissilePart {
    std::string sheet;      // a cooked .ktx, path relative to assets/
    bool additive = false;
};

struct MissileRow {
    std::string name;       // index.json's own, "Bone01"
    std::string mesh;       // path relative to assets/, a cooked .mum
    std::vector<MissilePart> parts;
    float scale = 1.0f;
    // MU's own frame count for the flight, at its 25 Hz animation clock. Not ticks: this is
    // a showing and not a rule, and the sim never reads it.
    float frames = 0.0f;
    // MU units, as index.json states it -- 150 for Bone01, 400 for the meteor's Fire01. It
    // is NOT converted here, although the geometry is: this number is read together with
    // MU's own per-frame arithmetic, and a conversion split between the file and the flight
    // is how a value gets divided by a hundred twice. Divide by the world's units_per_tile
    // where it is used, once.
    float lift = 0.0f;

    // The rest of the row the cook carries, in MU units for the same reason `lift` is. They
    // are beyond the interface this file was asked for and are here because the table holds
    // them: dropping them would make the cooked file the only record of MU's numbers.
    float muzzle[3] = {0.0f, 0.0f, 0.0f};  // where on the caster the throw starts
    float sideways = 0.0f;                 // the meteor's arrival offset
    float sidewaysSpread = 0.0f;           // and how much of it is random
};

struct Missiles {
    std::vector<MissileRow> rows;

    // By the name index.json gives it. Null for a name nothing cooked, which a caller must
    // treat as "throw nothing to look at" rather than as a reason to stop.
    const MissileRow* find(const std::string& name) const {
        for (const MissileRow& one : rows) {
            if (one.name == name) return &one;
        }
        return nullptr;
    }

    // False when the file will not open, is not a .mup, is of another version, or runs out
    // under a count it claims. Every count in the file is treated as hostile; the reason is
    // logged, because this one has no error to hand back.
    bool read(const std::string& path);
};

}  // namespace mu::content
