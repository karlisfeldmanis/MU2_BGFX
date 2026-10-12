// The MU checklist in the inspector (tools/mu/research.py, `mu research <Monster> --write`).
//
// The user, 2026-10-12, choosing it as the feature to cook new monsters by: every block of
// MuMain's source that names a monster -- its body, its effects, lights, chrome, sounds and play
// speeds -- is an item in the recipe's `mu_checklist`, todo, done or set aside. Drawn at the top
// of the Recipe tab, a line an item: its state (a click turns todo -> done -> skip -> todo, one
// `edit` of mu_checklist.N.state), its kind, the function it is in, the actions it waits on and
// what it calls; MuMain's own lines on hover, read from LEGACY/reference once; its note under it.
#pragma once

#include <string>
#include <unordered_map>

#include "core/json.h"
#include "studio/cooker.h"

namespace mu::studio {

class Checklist {
public:
    // `list` is the recipe's mu_checklist, edited in place after a write; `file` the recipe's
    // path for the edit, `root` the repository's. `said` and `ok` take the edit's answer.
    void draw(Cooker& cooker, const std::string& file, core::Json& list, const std::string& root,
              std::string* said, bool* ok);

private:
    const std::string& code(const std::string& root, const std::string& at, int lines);
    std::unordered_map<std::string, std::string> codes_;
};

}  // namespace mu::studio
