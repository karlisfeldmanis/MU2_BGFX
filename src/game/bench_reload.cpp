// The bench read again after a cook: ModelBench::reopen and replayClip (game/bench.h), out of
// bench.cpp, which sits at its size ratchet. The studio's swap (app/modes/studio_desk.h) is
// the one caller: a job that cooked what stands here has finished, and the new files are on
// disk while the old ones are still in the GPU.
#include <string>

#include "core/files.h"
#include "core/log.h"
#include "game/bench.h"

namespace mu::game {

ModelBench::Held ModelBench::held() const {
    Held out;
    if (categories_.empty()) return out;
    const BrowseCategory& open = categories_[category_];
    out.tab = open.label;
    out.name = open.at < open.entries.size() ? open.entries[open.at].name : "";
    out.clip = clipIndex();
    return out;
}

bool ModelBench::putBack(const Held& held, content::Textures& textures) {
    for (size_t c = 0; c < categories_.size(); ++c) {
        if (categories_[c].label != held.tab) continue;
        for (size_t i = 0; i < categories_[c].entries.size(); ++i) {
            if (categories_[c].entries[i].name != held.name) continue;
            category_ = c;
            categories_[c].at = i;
            const bool loaded = loadCurrent(textures);
            if (held.clip >= 0 && size_t(held.clip) < clipCount()) playClip(size_t(held.clip));
            return loaded;
        }
    }
    core::logError("studio: %s is not in %s here", held.name.c_str(), held.tab.c_str());
    return false;
}

bool ModelBench::restand(const std::string& assetDir, const std::string& world,
                         const float stand[3], const float fire[3], content::Textures& textures) {
    const Held was = held();
    haveFigure_ = false;
    figures_.shutdown();
    if (!openStudio(assetDir, world, stand, fire, textures)) return false;
    return putBack(was, textures);
}

bool ModelBench::reopen(const std::string& assetDir, const std::string& world,
                        content::Textures& textures) {
    if (categories_.empty()) return false;
    const Held was = held();

    // The figure every body points into goes first, then the textures the cook rewrote: each
    // holder of those handles is in the tables about to be read again. A world's own
    // textures (cooked/<world>/textures) are the town's, which this does not rebuild.
    haveFigure_ = false;
    figures_.shutdown();
    const std::string cooked = core::join(assetDir, "cooked");
    const size_t forgotten = textures.forgetChanged(core::join(cooked, "wardrobe")) +
                             textures.forgetChanged(core::join(cooked, "figures"));
    if (!fillBrowser(assetDir, world, textures)) return false;
    core::logf("studio: %s read again after a cook, %zu textures loaded anew", was.name.c_str(),
               forgotten);
    return putBack(was, textures);
}

void ModelBench::replayClip() {
    const int clip = clipIndex();
    // No crossfade: the bench may be held with no delta, and a fade frozen at its start is the
    // pose the figure was in before -- the first swap's before and after differed by it.
    if (clip >= 0) figure_.play(clip, true, 0.0f);
}

}  // namespace mu::game
