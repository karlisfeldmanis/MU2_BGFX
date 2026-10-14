#include "app/modes/studio_place.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/context.h"
#include "game/world/world.h"
#include "imgui.h"
#include "studio/cooker.h"

namespace mu::app {
namespace {

const ImVec4 kGold(0.86f, 0.72f, 0.42f, 1.0f);
const ImVec4 kGreen(0.52f, 0.80f, 0.52f, 1.0f);
const ImVec4 kRed(0.92f, 0.42f, 0.38f, 1.0f);
const ImVec4 kDim(0.58f, 0.58f, 0.60f, 1.0f);

constexpr float kPi = 3.14159265f;

// core::Json reads only; this is the one place a value of it is changed after a write.
core::Json* member(core::Json& json, const char* key) {
    for (auto& [k, v] : json.members) {
        if (k == key) return &v;
    }
    return nullptr;
}

std::string number(double v) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.4f", v);
    return text;
}

core::Json numberJson(double v) {
    core::Json j;
    j.type = core::Json::Type::Number;
    j.number = v;
    return j;
}

std::string quote(const std::string& words) {
    std::string quoted = "\"";
    for (char c : words) {
        if (c == '"' || c == '\\') {
            quoted += '\\';
            quoted += c;
        } else if (c == '\n') {
            quoted += "\\n";
        } else if (c == '\t') {
            quoted += "\\t";
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            quoted += c;
        }
    }
    return quoted + "\"";
}

}  // namespace

// core::Json keeps no int from float, so a whole number is written as the file has it: with
// `pointed`, a map object's `at`, `angle` and `scale` with a point (18400.0) and the rest
// (`type`) without; without it, every whole number bare (a manifest's tiles).
std::string jsonText(const core::Json& j, bool pointed, bool floats) {
    char out[64];
    switch (j.type) {
    case core::Json::Type::Null: return "null";
    case core::Json::Type::Bool: return j.boolean ? "true" : "false";
    case core::Json::Type::Number:
        if (j.number == std::floor(j.number) && std::fabs(j.number) < 1e15) {
            std::snprintf(out, sizeof(out), floats ? "%.1f" : "%.0f", j.number);
        } else {
            std::snprintf(out, sizeof(out), "%.10g", j.number);
        }
        return out;
    case core::Json::Type::String: return quote(j.string);
    case core::Json::Type::Array: {
        std::string list = "[";
        for (size_t i = 0; i < j.items.size(); ++i) list += (i ? ", " : "") + jsonText(j.items[i], pointed, floats);
        return list + "]";
    }
    case core::Json::Type::Object: {
        std::string block = "{";
        for (size_t i = 0; i < j.members.size(); ++i) {
            const std::string& key = j.members[i].first;
            const bool point = pointed && (key == "at" || key == "angle" || key == "scale");
            block += (i ? ", " : "") + quote(key) + ": " + jsonText(j.members[i].second, pointed, point);
        }
        return block + "}";
    }
    }
    return "null";
}

void StudioPlace::world(Context& ctx, const std::string& name) {
    if (name == world_) return;
    world_ = name;
    picked_ = line_ = -1;
    dragging_ = false;
    file_ = "content/world/" + name + "/" + name + ".json";
    source_ = core::parseJsonFile(ctx.paths.root + "/" + file_);
    perTile_ = source_["units_per_tile"].numberOr(100.0);
    models_.clear();
    const core::Json& objects = source_["objects"];
    for (size_t i = 0; i < objects.size(); ++i) models_.push_back(objects.at(i)["model"].stringOr(""));
    std::sort(models_.begin(), models_.end());
    models_.erase(std::unique(models_.begin(), models_.end()), models_.end());
    adding_ = 0;
}

bool pointerRay(Context& ctx, const gfx::Camera& camera, float origin[3], float direction[3]) {
    const int width = ctx.window.width(), height = ctx.window.height();
    if (width <= 0 || height <= 0) return false;
    float px = 0.0f, py = 0.0f;
    ctx.window.pointer(&px, &py);
    // As game/play_pointer.cpp: the pixel into clip space, y flipped, near plane by the caps.
    float view[16], proj[16], viewProj[16], inverse[16];
    ctx.renderer.cameraMatrices(camera, view, proj);
    bx::mtxMul(viewProj, view, proj);
    bx::mtxInverse(inverse, viewProj);
    const float ndcX = 2.0f * px / float(width) - 1.0f, ndcY = 1.0f - 2.0f * py / float(height);
    const float nearZ = bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f;
    const bx::Vec3 nearPoint = bx::mulH({ndcX, ndcY, nearZ}, inverse);
    const bx::Vec3 farPoint = bx::mulH({ndcX, ndcY, 1.0f}, inverse);
    const bx::Vec3 d = bx::normalize(bx::sub(farPoint, nearPoint));
    origin[0] = nearPoint.x, origin[1] = nearPoint.y, origin[2] = nearPoint.z;
    direction[0] = d.x, direction[1] = d.y, direction[2] = d.z;
    return true;
}

bool groundUnderPointer(Context& ctx, game::World& world, const gfx::Camera& camera, float at[3]) {
    float o[3], d[3];
    if (!pointerRay(ctx, camera, o, d)) return false;
    const content::Ground& ground = world.ground();
    // Down the ray in half metres to the first point under the land, then halved twelve times.
    float low = 0.0f, high = -1.0f;
    for (float t = 0.5f; t < 400.0f; t += 0.5f) {
        if (o[1] + d[1] * t < ground.heightAt(o[0] + d[0] * t, o[2] + d[2] * t)) {
            low = t - 0.5f, high = t;
            break;
        }
    }
    if (high < 0.0f) return false;
    for (int i = 0; i < 12; ++i) {
        const float mid = 0.5f * (low + high);
        if (o[1] + d[1] * mid < ground.heightAt(o[0] + d[0] * mid, o[2] + d[2] * mid)) {
            high = mid;
        } else {
            low = mid;
        }
    }
    for (int a = 0; a < 3; ++a) at[a] = o[a] + d[a] * high;
    return true;
}

bool toScreen(Context& ctx, const gfx::Camera& camera, const float point[3], float* x, float* y) {
    float view[16], proj[16], viewProj[16];
    ctx.renderer.cameraMatrices(camera, view, proj);
    bx::mtxMul(viewProj, view, proj);
    const float clip[4] = {
        viewProj[0] * point[0] + viewProj[4] * point[1] + viewProj[8] * point[2] + viewProj[12],
        viewProj[1] * point[0] + viewProj[5] * point[1] + viewProj[9] * point[2] + viewProj[13],
        0.0f,
        viewProj[3] * point[0] + viewProj[7] * point[1] + viewProj[11] * point[2] + viewProj[15]};
    if (clip[3] <= 0.0f) return false;   // behind the camera
    *x = (clip[0] / clip[3] * 0.5f + 0.5f) * float(ctx.window.width());
    *y = (0.5f - clip[1] / clip[3] * 0.5f) * float(ctx.window.height());
    return true;
}

void StudioPlace::match(game::World& world) { line_ = lineOf(world, size_t(picked_)); }

int StudioPlace::lineOf(game::World& world, size_t instance) const {
    // The source's line for an instance: its model, and where the cook put it --
    // x = at[0] / per_tile, z = -at[1] / per_tile (tools/cook.py, cook_placements).
    const content::CookedTown& town = world.town().cooked();
    const content::TownInstance& one = town.instances[instance];
    const std::string& model = town.models[one.model].name;
    const core::Json& objects = source_["objects"];
    for (size_t i = 0; i < objects.size(); ++i) {
        const core::Json& o = objects.at(i);
        if (o["model"].stringOr("") != model) continue;
        const double x = o["at"].at(0).numberOr(0.0) / perTile_;
        const double z = -o["at"].at(1).numberOr(0.0) / perTile_;
        if (std::fabs(x - one.position[0]) < 0.02 && std::fabs(z - one.position[2]) < 0.02) return int(i);
    }
    return -1;
}

int StudioPlace::instanceOf(game::World& world, size_t line) const {
    const core::Json& o = source_["objects"].at(line);
    const std::string model = o["model"].stringOr("");
    const double x = o["at"].at(0).numberOr(0.0) / perTile_, z = -o["at"].at(1).numberOr(0.0) / perTile_;
    const content::CookedTown& town = world.town().cooked();
    for (size_t i = 0; i < town.instances.size(); ++i) {
        const content::TownInstance& one = town.instances[i];
        if ((one.flags & 8) != 0 || town.models[one.model].name != model) continue;
        if (std::fabs(x - one.position[0]) < 0.02 && std::fabs(z - one.position[2]) < 0.02) return int(i);
    }
    return -1;
}

bool StudioPlace::follow(game::World& world, const core::Json& line) {
    if (line["file"].stringOr("") != file_ || !world.town().isOpen() || source_.isNull()) return false;
    // "objects.12.at.0" into its steps.
    std::vector<std::string> steps;
    const std::string dotted = line["path"].stringOr("");
    for (size_t at = 0; at <= dotted.size();) {
        size_t end = dotted.find('.', at);
        if (end == std::string::npos) end = dotted.size();
        steps.push_back(dotted.substr(at, end - at));
        at = end + 1;
    }
    core::Json* objects = member(source_, "objects");
    if (!objects || steps.size() < 2 || steps[0] != "objects") return false;
    const int n = std::atoi(steps[1].c_str());
    const std::string op = line["op"].stringOr("set");
    game::Town& town = world.town();
    const content::Ground& ground = world.ground();
    if (op == "remove" && steps.size() == 2 && n >= 0 && n < int(objects->items.size())) {
        const int shown = instanceOf(world, size_t(n));
        if (shown >= 0) town.setHidden(uint32_t(shown), true);
        objects->items.erase(objects->items.begin() + n);
        if (line_ == n) picked_ = line_ = -1;
        else if (line_ > n) --line_;
        return true;
    }
    if (op == "insert" && steps.size() == 2 && n >= 0 && n <= int(objects->items.size())) {
        const core::Json& o = line["new"];
        const std::string model = o["model"].stringOr("");
        // A template of the model the source holds, for its flags, the cook's lift and light.
        const content::CookedTown& cooked = town.cooked();
        int found = -1, held = -1;
        for (size_t i = 0; i < cooked.instances.size() && found < 0; ++i) {
            if (cooked.models[cooked.instances[i].model].name != model) continue;
            if ((held = lineOf(world, i)) >= 0) found = int(i);
        }
        objects->items.insert(objects->items.begin() + n, o);
        if (line_ >= n) ++line_;
        if (found < 0) return true;   // a model the town has not cooked: in the file, not shown
        content::TownInstance one = cooked.instances[size_t(found)];
        const core::Json& was = objects->items[size_t(held >= n ? held + 1 : held)];
        const float lift = one.position[1] - float(was["at"].at(2).numberOr(0.0) / perTile_);
        one.position[0] = float(o["at"].at(0).numberOr(0.0) / perTile_);
        one.position[2] = float(-o["at"].at(1).numberOr(0.0) / perTile_);
        one.position[1] = (one.flags & 1) ? ground.heightAt(one.position[0], one.position[2])
                                          : float(o["at"].at(2).numberOr(0.0) / perTile_) + lift;
        one.yaw = float(o["angle"].at(2).numberOr(0.0)) * kPi / 180.0f;
        one.scale = float(o["scale"].numberOr(1.0));
        one.flags = uint16_t(one.flags & ~8);
        town.addInstance(one);
        return true;
    }
    if (op != "set" || steps.size() < 3 || n < 0 || n >= int(objects->items.size())) return false;
    const int shown = instanceOf(world, size_t(n));
    core::Json& o = objects->items[size_t(n)];
    const double value = line["new"].numberOr(0.0);
    const std::string& field = steps[2];
    const int k = steps.size() > 3 ? std::atoi(steps[3].c_str()) : -1;
    core::Json* slot = field == "scale" ? member(o, "scale")
                       : (field == "at" || field == "angle") && k >= 0 && member(o, field.c_str()) &&
                               k < int(member(o, field.c_str())->items.size())
                           ? &member(o, field.c_str())->items[size_t(k)]
                           : nullptr;
    if (!slot) return false;
    const double old = slot->number;
    slot->number = value;
    if (shown < 0) return true;
    content::TownInstance& one = town.instanceAt(size_t(shown));
    float position[3] = {one.position[0], one.position[1], one.position[2]};
    float yaw = one.yaw;
    if (field == "at" && k == 0) position[0] = float(value / perTile_);
    if (field == "at" && k == 1) position[2] = float(-value / perTile_);
    if (field == "at" && k == 2 && (one.flags & 1) == 0) position[1] += float((value - old) / perTile_);
    if ((one.flags & 1) != 0) position[1] = ground.heightAt(position[0], position[2]);
    if (field == "angle" && k == 2) yaw = float(value) * kPi / 180.0f;
    if (field == "scale") one.scale = float(value);
    town.movePlacement(uint32_t(shown), yaw, position);
    if (picked_ == shown) storedY_ = position[1];
    return true;
}

bool StudioPlace::pointer(Context& ctx, game::World& world, const gfx::Camera& camera,
                          bool overPanels) {
    if (!world.town().isOpen() || source_.isNull()) return false;
    game::Town& town = world.town();
    const content::Ground& ground = world.ground();
    if (dragging_) {
        content::TownInstance& one = town.instanceAt(size_t(picked_));
        if (ctx.window.held(0)) {
            float g[3];
            if (groundUnderPointer(ctx, world, camera, g)) {
                one.position[0] = g[0] - grab_[0];
                one.position[2] = g[2] - grab_[2];
                one.position[1] = ground.heightAt(one.position[0], one.position[2]) +
                                  ((one.flags & 1) ? 0.0f : heightOver_);
            }
            return true;
        }
        dragging_ = false;
        saidOk_ = true;
        said_ = "moved; let go writes it";
        return true;   // the release: written by the panel, which holds the cooker
    }
    if (overPanels || !ctx.window.clicked(0)) return false;

    float o[3], d[3];
    if (!pointerRay(ctx, camera, o, d)) return false;
    const content::CookedTown& cooked = town.cooked();
    int best = -1;
    float bestT = 1e30f;
    for (size_t i = 0; i < cooked.instances.size(); ++i) {
        const content::TownInstance& one = cooked.instances[i];
        if ((one.flags & 8) != 0) continue;   // held back, or deleted here
        const content::TownModel& model = cooked.models[one.model];
        float c[3], r2 = 0.0f;
        for (int a = 0; a < 3; ++a) {
            c[a] = one.position[a] + 0.5f * (model.min[a] + model.max[a]) * one.scale;
            const float half = 0.5f * (model.max[a] - model.min[a]) * one.scale;
            r2 += half * half;
        }
        // The sphere round its box: through it, the nearest entry wins.
        float oc[3] = {c[0] - o[0], c[1] - o[1], c[2] - o[2]};
        const float t = oc[0] * d[0] + oc[1] * d[1] + oc[2] * d[2];
        if (t < 0.0f) continue;
        const float miss2 = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - t * t;
        if (miss2 > r2) continue;
        const float enter = t - std::sqrt(r2 - miss2);
        if (enter < bestT) {
            bestT = enter;
            best = int(i);
        }
    }
    if (best < 0) {
        picked_ = line_ = -1;
        return false;   // the land: a drag there orbits, as ever
    }
    picked_ = best;
    match(world);
    said_.clear();
    storedY_ = cooked.instances[size_t(best)].position[1];
    // A press on it starts a walk, when the source holds it.
    const content::TownInstance& one = cooked.instances[size_t(best)];
    float g[3];
    if (line_ >= 0 && groundUnderPointer(ctx, world, camera, g)) {
        grab_[0] = g[0] - one.position[0];
        grab_[2] = g[2] - one.position[2];
        heightOver_ = one.position[1] - ground.heightAt(one.position[0], one.position[2]);
        dragging_ = true;
    }
    return true;
}

bool StudioPlace::pickModel(game::World& world, const std::string& model, const float near[3]) {
    const content::CookedTown& cooked = world.town().cooked();
    float best = 1e30f;
    picked_ = -1;
    for (size_t i = 0; i < cooked.instances.size(); ++i) {
        const content::TownInstance& one = cooked.instances[i];
        if (cooked.models[one.model].name != model || (one.flags & 8) != 0) continue;
        const float dx = one.position[0] - near[0], dz = one.position[2] - near[2];
        if (dx * dx + dz * dz < best) {
            best = dx * dx + dz * dz;
            picked_ = int(i);
        }
    }
    if (picked_ < 0) return false;
    match(world);
    storedY_ = cooked.instances[size_t(picked_)].position[1];
    return true;
}

void StudioPlace::nudge(Context& ctx, game::World& world, studio::Cooker& cooker, float dx, float dz) {
    if (picked_ < 0 || line_ < 0) return;
    content::TownInstance& one = world.town().instanceAt(size_t(picked_));
    const float above = one.position[1] - world.ground().heightAt(one.position[0], one.position[2]);
    one.position[0] += dx;
    one.position[2] += dz;
    one.position[1] = world.ground().heightAt(one.position[0], one.position[2]) + ((one.flags & 1) ? 0.0f : above);
    write(ctx, world, cooker);
}

void StudioPlace::write(Context& ctx, game::World& world, studio::Cooker& cooker) {
    if (picked_ < 0 || line_ < 0) return;
    const content::TownInstance& one = world.town().cooked().instances[size_t(picked_)];
    core::Json& o = member(source_, "objects")->items[size_t(line_)];
    core::Json* at = member(o, "at");
    core::Json* angle = member(o, "angle");
    // Height by the change, not by the value: the cook lifts and lowers some models on the way
    // (LIFTED_BY_WORLD, LOWERED), and those offsets must not be written into the source.
    const std::string base = "[\"objects\", " + std::to_string(line_) + ", ";
    const double values[3] = {double(one.position[0]) * perTile_, -double(one.position[2]) * perTile_,
                              o["at"].at(2).numberOr(0.0) +
                                  ((one.flags & 1) ? 0.0 : (double(one.position[1]) - storedY_) * perTile_)};
    std::string answer;
    bool ok = true;
    for (int a = 0; a < 3 && ok; ++a) {
        if (std::fabs(values[a] - o["at"].at(size_t(a)).numberOr(0.0)) < 1e-4) continue;
        ok = cooker.edit(file_, base + "\"at\", " + std::to_string(a) + "]", number(values[a]), &answer);
        if (ok && at) at->items[size_t(a)].number = values[a];
    }
    const double yawDegrees = double(one.yaw) * 180.0 / kPi;
    if (ok && std::fabs(yawDegrees - o["angle"].at(2).numberOr(0.0)) > 1e-3) {
        ok = cooker.edit(file_, base + "\"angle\", 2]", number(yawDegrees), &answer);
        if (ok && angle) angle->items[2].number = yawDegrees;
    }
    if (ok && !o["scale"].isNull() && std::fabs(double(one.scale) - o["scale"].numberOr(1.0)) > 1e-4) {
        ok = cooker.edit(file_, base + "\"scale\"]", number(one.scale), &answer);
        if (ok) member(o, "scale")->number = one.scale;
    }
    storedY_ = one.position[1];
    saidOk_ = ok;
    said_ = ok ? "written to " + file_ + "; Cook makes it the cooked truth" : answer;
    if (ok) cooker.forgetPlan();
}

// ---------------------------------------------------------------------------- sprint G

bool StudioPlace::append(game::World& world, studio::Cooker& cooker, const core::Json& o,
                         const content::TownInstance& one, std::string* answer) {
    core::Json* objects = member(source_, "objects");
    if (!objects) return *answer = "the source has no objects", false;
    const size_t at = objects->items.size();
    if (!cooker.edit(file_, "[\"objects\", " + std::to_string(at) + "]", jsonText(o, true), answer, "insert")) {
        return false;
    }
    objects->items.push_back(o);
    picked_ = int(world.town().addInstance(one));
    line_ = int(at);
    storedY_ = one.position[1];
    cooker.forgetPlan();
    return true;
}

bool StudioPlace::add(game::World& world, studio::Cooker& cooker, const std::string& model, float x,
                      float z, std::string* answer) {
    // A template: an instance of the model that the source holds, for its type, its flags, the
    // cook's lift and its light -- the cook's own numbers until the Cook replaces them.
    const content::CookedTown& cooked = world.town().cooked();
    int found = -1, line = -1;
    for (size_t i = 0; i < cooked.instances.size() && found < 0; ++i) {
        if (cooked.models[cooked.instances[i].model].name != model) continue;
        if ((line = lineOf(world, i)) >= 0) found = int(i);
    }
    if (found < 0) return *answer = "no " + model + " in " + file_ + " to copy", false;
    content::TownInstance one = cooked.instances[size_t(found)];
    core::Json o = source_["objects"].at(size_t(line));
    const float lift = one.position[1] - float(o["at"].at(2).numberOr(0.0) / perTile_);
    const float ground = world.ground().heightAt(x, z);
    one.position[0] = x, one.position[2] = z;
    one.position[1] = ground + ((one.flags & 1) ? 0.0f : lift);
    one.yaw = one.pitch = one.roll = 0.0f;
    one.scale = 1.0f;
    one.flags = uint16_t(one.flags & ~8);
    // In whole units, as the map's own are mostly written: float metres would give 18239.99939.
    member(o, "at")->items = {numberJson(std::round(double(x) * perTile_)), numberJson(std::round(-double(z) * perTile_)),
                              numberJson(std::round(double(ground) * perTile_))};
    if (core::Json* angle = member(o, "angle")) angle->items = {numberJson(0), numberJson(0), numberJson(0)};
    if (core::Json* scale = member(o, "scale")) scale->number = 1.0;
    if (!append(world, cooker, o, one, answer)) return false;
    saidOk_ = true;
    said_ = *answer = model + " added as objects[" + std::to_string(line_) + "]; Cook makes it the cooked truth";
    return true;
}

bool StudioPlace::duplicate(game::World& world, studio::Cooker& cooker, std::string* answer) {
    if (picked_ < 0 || line_ < 0) return *answer = "nothing picked that the source holds", false;
    content::TownInstance one = world.town().cooked().instances[size_t(picked_)];
    core::Json o = source_["objects"].at(size_t(line_));
    // A metre east, at its height over the land: the height in the source by the change, as
    // write() does, so the cook's lift stays out.
    const float above = one.position[1] - world.ground().heightAt(one.position[0], one.position[2]);
    const float y = one.position[1];
    one.position[0] += 1.0f;
    one.position[1] = world.ground().heightAt(one.position[0], one.position[2]) + ((one.flags & 1) ? 0.0f : above);
    core::Json* at = member(o, "at");
    at->items[0].number += perTile_;
    if ((one.flags & 1) == 0) at->items[2].number += double(one.position[1] - y) * perTile_;
    const std::string model = world.town().cooked().models[one.model].name;
    if (!append(world, cooker, o, one, answer)) return false;
    saidOk_ = true;
    said_ = *answer = model + " duplicated as objects[" + std::to_string(line_) + "]";
    return true;
}

bool StudioPlace::remove(game::World& world, studio::Cooker& cooker, std::string* answer) {
    if (picked_ < 0 || line_ < 0) return *answer = "nothing picked that the source holds", false;
    if (!cooker.edit(file_, "[\"objects\", " + std::to_string(line_) + "]", "", answer, "remove")) return false;
    core::Json* objects = member(source_, "objects");
    objects->items.erase(objects->items.begin() + line_);
    world.town().setHidden(uint32_t(picked_), true);
    saidOk_ = true;
    said_ = *answer = "objects[" + std::to_string(line_) + "] deleted; the lines after it moved up one";
    picked_ = line_ = -1;
    cooker.forgetPlan();
    return true;
}

void StudioPlace::addPanel(game::World& world, studio::Cooker& cooker, const gfx::Camera& camera) {
    if (models_.empty()) return;
    ImGui::Separator();
    adding_ = std::min(adding_, int(models_.size()) - 1);
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::BeginCombo("##add", models_[size_t(adding_)].c_str(), ImGuiComboFlags_HeightLarge)) {
        for (size_t i = 0; i < models_.size(); ++i) {
            if (ImGui::Selectable(models_[i].c_str(), int(i) == adding_)) adding_ = int(i);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    std::string answer;
    if (ImGui::Button("Add") &&
        !add(world, cooker, models_[size_t(adding_)], camera.target[0], camera.target[2], &answer)) {
        saidOk_ = false;
        said_ = answer;
    }
    ImGui::SetItemTooltip("stands it on the land at the view's middle");
}

void StudioPlace::panel(Context& ctx, game::World& world, studio::Cooker& cooker,
                        const gfx::Camera& camera, float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Place", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushTextWrapPos(0.0f);
    if (!world.town().isOpen()) {
        ImGui::TextColored(kDim, "no town is open: choose a world as the scene");
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    // A walk just ended: what it moved is written now, by the hand that holds the cooker.
    if (picked_ >= 0 && !dragging_ && said_ == "moved; let go writes it") write(ctx, world, cooker);

    ImGui::TextColored(kDim, "click an object in %s; drag it over the land", world_.c_str());
    addPanel(world, cooker, camera);
    if (picked_ < 0) {
        if (!said_.empty()) ImGui::TextColored(saidOk_ ? kGreen : kRed, "%s", said_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    content::TownInstance& one = world.town().instanceAt(size_t(picked_));
    const content::TownModel& model = world.town().cooked().models[one.model];
    ImGui::TextColored(kGold, "%s", model.name.c_str());
    if (line_ < 0) {
        ImGui::TextColored(kDim, "placed by the index, not in %s: shown, not moved", file_.c_str());
    } else {
        ImGui::TextColored(kDim, "objects[%d] of %s", line_, file_.c_str());
        const float ground = world.ground().heightAt(one.position[0], one.position[2]);
        float pos[2] = {one.position[0], one.position[2]};
        float above = one.position[1] - ground;
        float yaw = one.yaw * 180.0f / kPi;
        float scale = one.scale;
        bool done = false;
        if (ImGui::DragFloat2("x, z (m)", pos, 0.02f)) {
            one.position[0] = pos[0];
            one.position[2] = pos[1];
            one.position[1] = world.ground().heightAt(pos[0], pos[1]) + above;
        }
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::BeginDisabled((one.flags & 1) != 0);
        if (ImGui::DragFloat("over the land (m)", &above, 0.01f)) one.position[1] = ground + above;
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        if (ImGui::DragFloat("yaw (deg)", &yaw, 0.5f)) one.yaw = yaw * kPi / 180.0f;
        done |= ImGui::IsItemDeactivatedAfterEdit();
        const bool scaled = !source_["objects"].at(size_t(line_))["scale"].isNull();
        ImGui::BeginDisabled(!scaled);
        if (ImGui::DragFloat("scale", &scale, 0.005f, 0.05f, 20.0f)) one.scale = scale;
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        std::string answer;
        if (!scaled) {
            ImGui::SameLine();
            if (ImGui::SmallButton("add a scale")) {
                // The cook's 1.0 written in, after which it is dragged like the rest.
                saidOk_ = cooker.edit(file_, "[\"objects\", " + std::to_string(line_) + ", \"scale\"]", "1.0",
                                      &answer, "insert");
                said_ = answer;
                if (saidOk_) {
                    member(source_, "objects")->items[size_t(line_)].members.emplace_back("scale", numberJson(1.0));
                    cooker.forgetPlan();
                }
            }
            ImGui::SetItemTooltip("the source writes no scale for it (the cook's 1.0): write one in");
        }
        if (done) write(ctx, world, cooker);
        if (ImGui::Button("Duplicate") && !duplicate(world, cooker, &answer)) saidOk_ = false, said_ = answer;
        ImGui::SetItemTooltip("a copy a metre east of it, added to the source");
        ImGui::SameLine();
        if (ImGui::Button("Delete") && !remove(world, cooker, &answer)) saidOk_ = false, said_ = answer;
        ImGui::SetItemTooltip("taken out of the source; hidden until the Cook");
        if (picked_ < 0) {   // deleted just now
            ImGui::TextColored(kGreen, "%s", said_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::End();
            return;
        }
    }
    if (!said_.empty() && said_ != "moved; let go writes it") {
        ImGui::TextColored(saidOk_ ? kGreen : kRed, "%s", said_.c_str());
    }
    if (ImGui::SmallButton("Drop the selection")) picked_ = line_ = -1;
    ImGui::PopTextWrapPos();
    ImGui::End();

    // The mark: a ring on the screen round the picked object's middle, over everything.
    if (picked_ >= 0) {
        float view[16], proj[16], viewProj[16];
        ctx.renderer.cameraMatrices(camera, view, proj);
        bx::mtxMul(viewProj, view, proj);
        const float mid = 0.5f * (model.min[1] + model.max[1]) * one.scale;
        const bx::Vec3 clip = bx::mulH({one.position[0], one.position[1] + mid, one.position[2]}, viewProj);
        const float sx = (clip.x * 0.5f + 0.5f) * float(ctx.window.width());
        const float sy = (0.5f - clip.y * 0.5f) * float(ctx.window.height());
        ImGui::GetForegroundDrawList()->AddCircle(ImVec2(sx, sy), 1.6f * ImGui::GetFontSize(),
                                                  IM_COL32(220, 184, 107, 230), 32, 2.0f);
    }
}

}  // namespace mu::app
