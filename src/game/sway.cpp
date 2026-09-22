#include "game/sway.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "core/files.h"
#include "core/json.h"
#include "core/log.h"
#include "game/frustum.h"

namespace mu::game {

namespace {

// Tree01 and Tree02 are ZzzObject.cpp's WD_0LORENCIA case `MODEL_TREE01`/`MODEL_TREE01 + 1`,
// `o->Velocity = 1.f / o->Scale * 0.4f`, baked at scale 1 (Tree01's `play_speed_from`).
// Tree11, Tree12 and Tree13 are object types 10, 11 and 12 -- MODEL_TREE01 + 10 is IN that
// switch, commented out (`//case MODEL_TREE01+10:`), so none of the three take the
// scale-dependent case; they keep the flat 0.16 every object starts with, which is the rate
// their clip was baked at.
bool scaleDependent(const std::string& model) { return model == "Tree01" || model == "Tree02"; }

// Where a placement's clip starts. Not MU's: CreateObject sets `AnimationFrame = 0.f` for
// every object, so MU's 165 Tree11 all sway on the same beat, as one sheet. This is MU2's
// Scenery.Take -- `PosMod(at.X * 0.37 + at.Z * 0.61, length)` -- seeded from the position so
// the town is the same town every time it loads. A bench choice, carried over from MU2 and
// marked as one.
float phaseOf(const float position[3], float length) {
    if (length <= 0.0f) return 0.0f;
    const float at = position[0] * 0.37f + position[2] * 0.61f;
    const float wrapped = std::fmod(at, length);
    return wrapped < 0.0f ? wrapped + length : wrapped;
}

// A skinned mesh leaves its bind box the moment it moves -- a branch swings out of it --
// hence Figure::radius's half again. Doubled on top of that for the shadow: at the sun's
// 52 degrees a tree throws its shadow about 0.8 of its height away, so a tree just outside
// the frame can still cast into it, and one posed in its bind pose there would hold a
// frozen shadow under a swaying canopy.
constexpr float kSwingAllowance = 1.5f;
constexpr float kShadowAllowance = 2.0f;

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

    std::unordered_set<std::string> still;
    for (const core::Json& name : manifest["still"].items) still.insert(name.string);

    for (const auto& [name, entry] : manifest["clips"].members) {
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
        const content::Bounds& bounds = mesh->bounds();
        for (int axis = 0; axis < 3; ++axis) {
            body->min[axis] = bounds.min[axis];
            body->max[axis] = bounds.max[axis];
        }
        body->radius = bounds.radius;

        Model model;
        model.library = std::move(library);
        model.body = std::move(body);
        models_[name] = std::move(model);
    }

    size_t held = 0;
    slotOf_.assign(town.cooked().instances.size(), -1);
    for (uint32_t i = 0; i < town.cooked().instances.size(); ++i) {
        const content::TownInstance& placement = town.cooked().instances[i];
        if (placement.model >= town.cooked().models.size()) continue;
        const std::string& name = town.cooked().models[placement.model].name;
        auto found = models_.find(name);
        if (found == models_.end()) continue;
        const FigureBody* body = found->second.body.get();
        const content::Bounds& bounds = body->skeletonMesh->bounds();

        Instance instance;
        instance.townIndex = i;
        instance.figure.stand(body, placement.position, placement.yaw, placement.scale);
        instance.figure.tilt(placement.pitch, placement.roll);
        if (still.count(name)) {
            // MU's `o->Velocity = 0.f`: its first key, forever.
            instance.clipRate = 0.0f;
            ++held;
        } else {
            instance.clipRate = scaleDependent(name)
                                    ? 1.0f / std::max(placement.scale, 0.01f) : 1.0f;
            instance.figure.setClock(phaseOf(placement.position, instance.figure.length()));
        }
        // The box's own centre off the pivot, horizontally, is folded into the radius
        // rather than turned by the placement's yaw: a looser sphere, and nothing to get
        // the handedness of wrong.
        const float offset = std::sqrt(bounds.centre[0] * bounds.centre[0] +
                                       bounds.centre[2] * bounds.centre[2]);
        instance.centre[0] = placement.position[0];
        instance.centre[1] = placement.position[1] + bounds.centre[1] * placement.scale;
        instance.centre[2] = placement.position[2];
        instance.radius = (bounds.radius * kSwingAllowance + offset) * placement.scale *
                          kShadowAllowance;
        slotOf_[i] = int32_t(instances_.size());
        instances_.push_back(std::move(instance));
    }

    scratch_.assign(size_t(gfx::Renderer::kMaxBones) * 12, 0.0f);
    if (!instances_.empty()) {
        core::logf("sway: %zu placements over %zu models play their own clip, %zu held on "
                   "their first key", instances_.size(), models_.size(), held);
    }
    return true;
}

void Sway::shutdown() {
    instances_.clear();
    slotOf_.clear();
    models_.clear();
    scratch_.clear();
    posed_ = 0;
}

void Sway::update(float seconds, const float* viewProj, gfx::Renderer& renderer, Town& town) {
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Frustum frustum(viewProj ? viewProj : identity);
    posed_ = 0;
    for (Instance& instance : instances_) {
        // The clock runs whether or not it is seen, so a tree turned back to is where its
        // own time has taken it and not where it was left.
        instance.figure.update(seconds, instance.clipRate);
        if (viewProj && !frustum.holds(instance.centre, instance.radius)) {
            town.setPaletteRow(instance.townIndex, -1);
            instance.posed = false;
            continue;
        }
        const int bones = instance.figure.pose(scratch_.data());
        const int row = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        town.setPaletteRow(instance.townIndex, row);
        instance.posed = bones > 0;
        ++posed_;
    }
}

}  // namespace mu::game
