#include "game/sway.h"

#include <algorithm>

#include "core/files.h"
#include "core/json.h"
#include "core/log.h"

namespace mu::game {

namespace {

// The models this file plays, out of every one the cook found a clip for -- see sway.h.
// Tree01 and Tree02 are ZzzObject.cpp's WD_0LORENCIA case `MODEL_TREE01`/`MODEL_TREE01 + 1`,
// `o->Velocity = 1.f / o->Scale * 0.4f`. Tree11, Tree12 and Tree13 are object types 10, 11
// and 12 -- MODEL_TREE01 + 10 is IN that source file, commented out (`//case
// MODEL_TREE01+10:`), so none of the three take the scale-dependent case or any other; they
// fall through to the flat `o->Velocity = 0.16f` every object in the town starts with. What
// is NOT traced past that: whether the three glbs were baked assuming that 0.16 is their
// clip's own natural pace, the way Tree01's recipe states outright that its clip was "baked
// at scale 1.0, which is 0.4". No such note exists for these three, so they play at their
// own baked rate (clipRate 1.0) rather than at a derived multiple of it -- the honest
// default until someone traces their bake the way Tree01's was.
bool scaleDependent(const std::string& model) { return model == "Tree01" || model == "Tree02"; }
bool known(const std::string& model) {
    return scaleDependent(model) || model == "Tree11" || model == "Tree12" || model == "Tree13";
}

}  // namespace

bool Sway::open(const std::string& assetDir, const std::string& world, const Town& town) {
    const std::string path = core::join(assetDir, "cooked/" + world + "/clips.json");
    core::Json manifest = core::parseJsonFile(path);
    if (manifest.isNull()) {
        // Not every world has cooked one yet, and that is not an error: a world with no
        // rigged placement writes an empty clips.json, and one cooked before this sprint
        // writes none at all. Either way there is nothing to sway.
        return true;
    }

    for (const auto& [name, entry] : manifest["clips"].members) {
        if (!known(name)) continue;

        size_t modelIndex = town.cooked().models.size();
        for (size_t i = 0; i < town.cooked().models.size(); ++i) {
            if (town.cooked().models[i].name == name) modelIndex = i;
        }
        const content::Mesh* mesh = modelIndex < town.cooked().models.size()
                                        ? town.meshAt(modelIndex) : nullptr;
        if (!mesh || !mesh->isSkinned()) continue;

        std::vector<uint8_t> bytes = core::readFile(core::join(assetDir, entry.string));
        auto library = std::make_unique<ClipLibrary>();
        std::string error;
        if (bytes.empty() || !content::parseCookedClips(bytes, library->clips, error)) {
            core::logError("%s: %s", entry.string.c_str(),
                           bytes.empty() ? "is not there" : error.c_str());
            continue;
        }
        if (library->clips.clips.empty()) continue;

        auto body = std::make_unique<FigureBody>();
        body->name = name;
        body->parts.push_back(mesh);
        body->skeletonMesh = mesh;
        body->library = library.get();
        // Identity: a world object's clip is baked from the very same glb as its mesh, in
        // the very same joint order (cook_world_clip reads `skins[0].joints` off the model's
        // own document, exactly as the mesh cook does), so there is no second rig here to
        // name-match against the way a worn part's is.
        const size_t bones = std::min(mesh->bones().size(), size_t(library->clips.bones));
        body->clipBoneOf.assign(mesh->bones().size(), -1);
        for (size_t i = 0; i < bones; ++i) body->clipBoneOf[i] = int32_t(i);
        body->idleClip = 0;

        Model model;
        model.library = std::move(library);
        model.body = std::move(body);
        models_[name] = std::move(model);
    }

    for (uint32_t i = 0; i < town.cooked().instances.size(); ++i) {
        const content::TownInstance& placement = town.cooked().instances[i];
        if (placement.model >= town.cooked().models.size()) continue;
        const std::string& name = town.cooked().models[placement.model].name;
        auto found = models_.find(name);
        if (found == models_.end()) continue;

        Instance instance;
        instance.townIndex = i;
        instance.scale = placement.scale;
        instance.scaleDependent = scaleDependent(name);
        instance.figure.stand(found->second.body.get(), placement.position, placement.yaw,
                              placement.scale);
        instances_.push_back(std::move(instance));
    }

    scratch_.assign(size_t(gfx::Renderer::kMaxBones) * 12, 0.0f);
    if (!instances_.empty()) {
        core::logf("sway: %zu placements over %zu models play their own clip",
                   instances_.size(), models_.size());
    }
    return true;
}

void Sway::shutdown() {
    instances_.clear();
    models_.clear();
    scratch_.clear();
}

void Sway::update(float seconds, gfx::Renderer& renderer, Town& town) {
    for (Instance& instance : instances_) {
        // MU's `o->Velocity`, scaled to this Figure's own clip-second-per-real-second the
        // way Figure::update already expects a walk's clip rate: see sway.h and the note in
        // the anonymous namespace above for what is traced and what is not.
        const float clipRate = instance.scaleDependent
                                    ? 1.0f / std::max(instance.scale, 0.01f)
                                    : 1.0f;
        instance.figure.update(seconds, clipRate);
        const int bones = instance.figure.pose(scratch_.data());
        const int row = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        town.setPaletteRow(instance.townIndex, row);
    }
}

}  // namespace mu::game
