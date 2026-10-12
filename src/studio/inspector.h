// The studio's inspector (docs/order-plan.md §9c, F4): the subject's recipe and the light,
// edited in place under the Subject card.
//
// Two tabs. **Recipe** is the subject's own file -- or each piece of an armour set -- as a
// tree: a number, a word or a switch is edited where it stands, an island's material is
// picked from the library, and the notes beside the values (`*_note`, `*_from`, `why`) are
// shown when asked. **Light** is the scene's map's light in its layers (studio/light_panel.h):
// the map's own sheet over the base, dusk, night and rain, reloaded by the app within a quarter
// of a second, so every knob is seen at once in the world it is for.
//
// Every change is one `edit` through mu serve (tools/mu/edit.py): the value's own span of the
// file rewritten and a line in workshop/.mu/journal.log. Nothing is written while a field is
// being typed in or dragged, only when it is let go. The card's stale marker is asked again
// after each, so what an edit costs -- nothing, a cook, a rebuild -- shows at once.
//
// The refinement, +0 to +11, is a knob of the view and not of the recipe: it is not written.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/json.h"
#include "studio/cooker.h"
#include "studio/light_panel.h"

namespace mu::studio {

class Inspector {
public:
    // `plus` is what the bench shows now; the return is the plus asked for, or -1. The Light
    // tab is the scene's map (studio/light_panel.h): `root`, `world` (empty on the Stage) and
    // the time of day, 0 to 2; `time` is set to the one asked for, or left.
    int draw(Cooker& cooker, const std::string& target, int plus, float x, float y, float w,
             float h, const std::string& root, const std::string& world, int* time);
    // Opens the Light tab on the next draw, for a scripted run (MU_STUDIO_DO's `tab light`).
    void showLight() { showLight_ = true; }

private:
    // One value or block of `doc`, at `path` (a JSON array's text, built as it descends).
    void node(Cooker& cooker, RecipeView& view, size_t file, core::Json& value,
              const std::string& key, const std::string& path, bool light);
    bool commit(Cooker& cooker, const std::string& file, const std::string& path,
                const std::string& value);
    void tab(Cooker& cooker, RecipeView& view, const std::string& target, bool light);

    RecipeView recipe_;
    LightPanel light_;
    std::unordered_map<std::string, double> numbers_;    // a field being typed in, by path
    std::unordered_map<std::string, std::string> words_;
    bool notes_ = false;
    bool showLight_ = false;
    std::string said_;
    bool saidOk_ = true;
};

}  // namespace mu::studio
