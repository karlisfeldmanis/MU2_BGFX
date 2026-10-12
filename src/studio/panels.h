// The studio's panels, F2 of the Cooker (docs/order-plan.md §9c): the browser down the left,
// the subject's card at the top right, the jobs along the bottom. ImGui, drawn between
// gfx::Gui's begin() and end(); no bgfx and no bench here, so cooker_test draws them with no
// window. What the panels see of the bench is Shelf, filled by the mode; what a click asks
// of it comes back as Asked.
#pragma once

#include <string>
#include <vector>

#include "studio/inspector.h"

namespace mu::studio {

class Cooker;

// The bench's browser as the panels see it: its tabs, the open tab's names, the one on the
// bench, and the `mu make` name of that one (empty when it has none).
struct Shelf {
    std::vector<std::string> categories;
    int category = 0;
    std::vector<std::string> names;
    int selected = -1;
    std::string target;
    std::string clip;   // the clip line under a figure, empty for a mesh
    std::string time;   // the time of day, and the key that changes it
    std::string swapped;  // the last swap's before/after sheet, once one was taken
    int plus = 0;         // the refinement the bench shows
    bool monster = false; // a monster stands here: it may be fought in the arena
    bool arena = false;   // the arena window is up
    bool placing = false; // the Place window has the inspector's spot
    std::vector<std::string> scenes;   // "Stage", "Home", then the cooked worlds
    int scene = 0;
};

struct Asked {
    int category = -1;  // open this tab
    int pick = -1;      // put this row of the open tab on the bench
    bool openSheet = false;  // show the last swap's before/after
    bool reread = false;     // read the subject again from disk now, as after a cook
    int scene = -1;          // stand it in shelf.scenes[scene]
    int plus = -1;           // show the items at +plus
    int arena = -1;          // 1 open the arena window, 0 close it
    int place = -1;          // 1 place objects, 0 stop
};

class Panels {
public:
    // `width` and `height` are the window's, in framebuffer pixels; `now` wall seconds.
    Asked draw(const Shelf& shelf, Cooker& cooker, float width, float height, double now);
    void showLight() { inspector_.showLight(); }
    // Where the inspector stands (x, y, w, h), for the Place window that takes its spot.
    void sideRect(float out[4]) const {
        for (int i = 0; i < 4; ++i) out[i] = side_[i];
    }

private:
    void browser(const Shelf& shelf, Asked& asked, float x, float y, float w, float h);
    // Returns the card's bottom edge, where the inspector begins.
    float subject(const Shelf& shelf, Cooker& cooker, float x, float y, float w);
    void jobs(Cooker& cooker, float x, float y, float w, float h, double now);

    char filter_[64] = {};
    Inspector inspector_;
    float side_[4] = {0, 0, 0, 0};
    Asked asked_;
    size_t finishedSeen_ = 0;
};

}  // namespace mu::studio
