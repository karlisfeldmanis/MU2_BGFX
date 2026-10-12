// The studio's Light tab (docs/order-plan.md §9c): a map's light, tuned in its own world.
//
// The game lays its light in layers (app/context.h, TimeOfDay): sheets/lighting.json under
// everything, the map's own sheets/worlds/<w>.json over it, then dusk's or night's
// sheets/time/<t>.json, and in rain the map's <w>_rain.json blended in. Each is watched and
// re-applied within a quarter of a second, so a value dragged here is seen at once, in the
// world it is for.
//
// The tab shows one layer at a time, its knobs in groups -- the sun, the sky and daylight,
// shadows, the tonemap, bloom and glow, the rest -- and Noon, Dusk and Night to see the time
// it is tuned at. In a layer over the base, a knob the layer does not set shows the value it
// inherits, dimmed, and "set here" writes that value into the layer, after which it is the
// layer's to drag. Every write is tools/mu/edit.py's (a value set or a key inserted), journalled;
// the sheets are read from disk again whenever they change, so an agent's edit shows too.
#pragma once

#include <string>
#include <vector>

#include "core/json.h"

namespace mu::studio {

class Cooker;

class LightPanel {
public:
    // `root` is MU2_BGFX; `world` the scene's world (empty on the Stage, which wears the base
    // alone); `time` 0 noon, 1 dusk, 2 night. Returns the time of day asked for, or -1.
    int draw(Cooker& cooker, const std::string& root, const std::string& world, int time);

private:
    struct Layer {
        std::string label, file;   // file relative to root
    };
    void read(const std::string& root);
    void knob(Cooker& cooker, const std::string& key);
    bool write(Cooker& cooker, const std::string& path, const std::string& value, const char* how);

    std::vector<Layer> layers_;
    int layer_ = 0;
    std::string world_;
    std::string file_;         // the layer shown
    long long stamps_[2] = {-1, -1};   // the base's and the layer's dates when read
    core::Json base_, sheet_;  // sheets/lighting.json, and the layer (the same on the base)
    std::string said_;
    bool saidOk_ = true;
};

}  // namespace mu::studio
