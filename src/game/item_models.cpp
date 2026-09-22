#include "game/item_models.h"

#include <cstdio>
#include <vector>

#include "content/cooked.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// MU's HideSkin. Every garment in Data/Player ships the body under it -- a helm carries the
// face inside its visor, a coat the bare forearms, pants the thighs -- because the model is
// only ever drawn on somebody, where that body is the wearer's. RenderItems draws the same file
// with nobody in it and passes HideSkin = true (ZzzObject.cpp), and RenderMesh then skips any
// mesh whose texture the loader classed as skin. MU2's bake keeps that class as a material
// named "skin" (Model.HideSkin), and nine of the cooked wardrobe's pieces carry one: five
// helms, three coats and a pair of the elf's pants.
//
// Cut rather than hidden, as MU2 learned: the stage frames an item by its bounds, and a coat
// framed round forearms nobody can see sits small and off-centre in its cell. So the
// vertices the remaining parts no longer use go too, and the bounds follow what is left.
// Only here, on the item paths -- the bag, the shelf, the potion boxes, the ground. A piece
// worn on a character is Figures' and keeps its body.
constexpr const char* kSkin = "skin";

template <typename V>
int cutSkin(std::vector<V>& vertices, content::CookedMesh& mesh) {
    std::vector<bool> skin(mesh.materials.size(), false);
    int cut = 0;
    for (size_t i = 0; i < mesh.materials.size(); ++i) skin[i] = mesh.materials[i].name == kSkin;
    for (const content::CookedPart& part : mesh.parts) {
        if (part.material < skin.size() && skin[part.material]) ++cut;
    }
    if (cut == 0) return 0;
    std::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
    std::vector<V> kept;
    std::vector<uint32_t> indices;
    std::vector<content::CookedPart> parts;
    for (const content::CookedPart& part : mesh.parts) {
        if (part.material < skin.size() && skin[part.material]) continue;
        content::CookedPart out = part;
        out.firstIndex = uint32_t(indices.size());
        for (uint32_t i = part.firstIndex; i < part.firstIndex + part.indexCount; ++i) {
            const uint32_t from = mesh.indices[i];
            if (remap[from] == UINT32_MAX) {
                remap[from] = uint32_t(kept.size());
                kept.push_back(vertices[from]);
            }
            indices.push_back(remap[from]);
        }
        parts.push_back(out);
    }
    vertices = std::move(kept);
    mesh.indices = std::move(indices);
    mesh.parts = std::move(parts);
    return cut;
}

}  // namespace

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
    bool parsed = !bytes.empty() && content::parseCookedMesh(bytes, cooked, error);
    if (parsed) {
        const int cut = cooked.isSkinned() ? cutSkin(cooked.skinned, cooked)
                                           : cutSkin(cooked.vertices, cooked);
        if (cut > 0) core::logf("items: %s drawn with its wearer's skin cut (%d part%s)",
                                name.c_str(), cut, cut == 1 ? "" : "s");
    }
    if (parsed && made->buildFromCooked(cooked, name, assetDir_, *textures_)) {
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
