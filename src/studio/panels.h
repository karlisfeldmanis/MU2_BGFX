// The studio's panels, F2 of the Cooker (docs/order-plan.md §9c): the browser down the left,
// the subject's card at the top right, the jobs along the bottom. ImGui, drawn between
// gfx::Gui's begin() and end(); no bgfx and no bench here, so cooker_test draws them with no
// window. What the panels see of the bench is Shelf, filled by the mode; what a click asks
// of it comes back as Asked.
#pragma once

#include <string>
#include <vector>

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
};

struct Asked {
    int category = -1;  // open this tab
    int pick = -1;      // put this row of the open tab on the bench
};

class Panels {
public:
    // `width` and `height` are the window's, in framebuffer pixels; `now` wall seconds.
    Asked draw(const Shelf& shelf, Cooker& cooker, float width, float height, double now);

private:
    void browser(const Shelf& shelf, Asked& asked, float x, float y, float w, float h);
    void subject(const Shelf& shelf, Cooker& cooker, float x, float y, float w);
    void jobs(Cooker& cooker, float x, float y, float w, float h, double now);

    char filter_[64] = {};
    size_t finishedSeen_ = 0;
};

}  // namespace mu::studio
