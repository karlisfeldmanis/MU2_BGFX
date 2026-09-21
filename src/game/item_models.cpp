#include "game/item_models.h"

#include <cstdio>

#include "content/cooked.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {

void ItemModels::shutdown() {
    for (auto& [path, mesh] : meshes_) {
        if (mesh) mesh->shutdown();
    }
    meshes_.clear();
}

const content::Mesh* ItemModels::read(const std::string& glb, const std::string& label) {
    auto found = meshes_.find(glb);
    if (found != meshes_.end()) return found->second.get();
    std::unique_ptr<content::Mesh>& slot = meshes_[glb];
    if (glb.empty() || !textures_) {
        core::logError("items: %s has no model to draw", label.c_str());
        return nullptr;
    }
    // "items/weapons/Sword01/Sword01.glb" -> "Sword01", the wardrobe's name for it.
    const size_t slash = glb.find_last_of('/');
    std::string name = slash == std::string::npos ? glb : glb.substr(slash + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".glb") == 0) {
        name.resize(name.size() - 4);
    }
    auto made = std::make_unique<content::Mesh>();
    const std::string cookedPath =
        core::join(assetDir_, "cooked/wardrobe/meshes/" + name + ".mum");
    // Asked for quietly: the cook's wardrobe holds the weapons and the armour, and the
    // potions, jewels, scrolls and Zen are read from their glb instead. Going through
    // core::readFile would log a missing file as an error for every one of them, which is a
    // run with errors in it and nothing wrong.
    std::vector<uint8_t> bytes;
    if (std::FILE* file = std::fopen(cookedPath.c_str(), "rb")) {
        std::fclose(file);
        bytes = core::readFile(cookedPath);
    }
    content::CookedMesh cooked;
    std::string error;
    if (!bytes.empty() && content::parseCookedMesh(bytes, cooked, error) &&
        made->buildFromCooked(cooked, name, assetDir_, *textures_)) {
        slot = std::move(made);
        return slot.get();
    }
    made = std::make_unique<content::Mesh>();
    if (made->load(core::join(assetDir_, glb), *textures_)) {
        slot = std::move(made);
        return slot.get();
    }
    core::logError("items: %s has no model to draw (%s)", label.c_str(), glb.c_str());
    return nullptr;
}

const content::Mesh* ItemModels::of(int32_t item) {
    if (!tables_ || item < 0 || size_t(item) >= tables_->items.size()) return nullptr;
    const content::ItemRow& row = tables_->items[size_t(item)];
    return read(row.glb, row.label);
}

const content::Mesh* ItemModels::coin() { return read("items/misc/Gold01/Gold01.glb", "Zen"); }

}  // namespace mu::game
