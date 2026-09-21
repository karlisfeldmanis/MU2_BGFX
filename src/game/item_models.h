// The item models, by row: what the windows' stages and the drops on the ground draw.
//
// One store for both, so a sword seen in the bag and the same sword on the grass are one
// mesh read once. The cooked copy first -- `cooked/wardrobe/meshes/<name>.mum`, BC7 and
// flattened, the copy the studio passed -- and the row's own glb where the cook wrote none,
// which today is the potions, jewels, scrolls and Zen (tools/cook.py's wardrobe takes the
// weapons and armour only). Read the first time a row is asked for and kept: a session sees a
// few dozen kinds of item, and a hitch on the first sight of each is MU2's Stockroom without
// the loading curtain.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "content/mesh.h"
#include "content/tables.h"
#include "content/texture.h"

namespace mu::game {

class ItemModels {
public:
    void open(const content::Tables* tables, const std::string& assetDir,
              content::Textures* textures) {
        tables_ = tables;
        assetDir_ = assetDir;
        textures_ = textures;
    }
    void shutdown();

    // The model of item row `item`, or null when it has none (said once in the log).
    const content::Mesh* of(int32_t item);
    // Zen's heap, which is not a row a player can hold: Gold01, MU's MODEL_ZEN.
    const content::Mesh* coin();

    const content::Tables* tables() const { return tables_; }

private:
    const content::Mesh* read(const std::string& glb, const std::string& label);

    const content::Tables* tables_ = nullptr;
    std::string assetDir_;
    content::Textures* textures_ = nullptr;
    // By glb path, so the thirteen scrolls on Book01 are one mesh.
    std::unordered_map<std::string, std::unique_ptr<content::Mesh>> meshes_;
};

}  // namespace mu::game
