// The bench read again after a cook: ModelBench::reopen and replayClip (game/bench.h), out of
// bench.cpp, which sits at its size ratchet. The studio's swap (app/modes/studio_desk.h) is
// the one caller: a job that cooked what stands here has finished, and the new files are on
// disk while the old ones are still in the GPU.
#include <string>

#include "core/files.h"
#include "core/log.h"
#include "game/bench.h"

namespace mu::game {

bool ModelBench::reopen(const std::string& assetDir, const std::string& world,
                        content::Textures& textures) {
    if (categories_.empty()) return false;
    const std::string tab = categories_[category_].label;
    const BrowseCategory& open = categories_[category_];
    const std::string name = open.at < open.entries.size() ? open.entries[open.at].name : "";
    const int clip = clipIndex();

    // The figure every body points into goes first, then the textures the cook rewrote: each
    // holder of those handles is in the tables about to be read again. A world's own
    // textures (cooked/<world>/textures) are the town's, which this does not rebuild.
    haveFigure_ = false;
    figures_.shutdown();
    const std::string cooked = core::join(assetDir, "cooked");
    const size_t forgotten = textures.forgetChanged(core::join(cooked, "wardrobe")) +
                             textures.forgetChanged(core::join(cooked, "figures"));
    if (!fillBrowser(assetDir, world, textures)) return false;

    for (size_t c = 0; c < categories_.size(); ++c) {
        if (categories_[c].label != tab) continue;
        for (size_t i = 0; i < categories_[c].entries.size(); ++i) {
            if (categories_[c].entries[i].name != name) continue;
            category_ = c;
            categories_[c].at = i;
            const bool loaded = loadCurrent(textures);
            if (clip >= 0 && size_t(clip) < clipCount()) playClip(size_t(clip));
            core::logf("studio: %s read again after a cook, %zu textures loaded anew",
                       name.c_str(), forgotten);
            return loaded;
        }
    }
    core::logError("studio: %s is not in %s after the cook", name.c_str(), tab.c_str());
    return false;
}

void ModelBench::replayClip() {
    const int clip = clipIndex();
    // No crossfade: the bench may be held with no delta, and a fade frozen at its start is the
    // pose the figure was in before -- the first swap's before and after differed by it.
    if (clip >= 0) figure_.play(clip, true, 0.0f);
}

}  // namespace mu::game
