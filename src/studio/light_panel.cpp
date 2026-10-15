#include "studio/light_panel.h"

#include <cstdio>
#include <cstring>

#include "core/files.h"
#include "imgui.h"
#include "studio/cooker.h"

namespace mu::studio {
namespace {

const ImVec4 kGreen(0.52f, 0.80f, 0.52f, 1.0f);
const ImVec4 kRed(0.92f, 0.42f, 0.38f, 1.0f);
const ImVec4 kDim(0.58f, 0.58f, 0.60f, 1.0f);

// The knobs by what they do; a key in no group falls to "the rest".
struct Group {
    const char* title;
    std::vector<const char*> keys;
};
const std::vector<Group>& groups() {
    static const std::vector<Group> kGroups = {
        {"Sun", {"azimuth", "elevation", "sun_colour", "sun_strength", "sun_angle_degrees"}},
        {"Sky and daylight", {"sky_colour", "horizon_paleness", "ground_colour", "ambient_strength", "dust_colour",
                              "dust_density", "probe_diffuse"}},
        {"Shadows", {"shadow_range", "shadow_fit_below", "shadow_bias_metres", "shadow_normal_bias", "ssao_radius",
                     "ssao_strength", "contact_length", "contact_thickness", "contact_strength"}},
        {"Tonemap", {"exposure", "contrast", "saturation", "split", "tint_low", "tint_high", "sharpen"}},
        {"Bloom and glow", {"bloom_threshold", "bloom_knee", "bloom_strength", "lamp_strength", "glow_strength",
                            "flame_strength"}},
    };
    return kGroups;
}

bool grouped(const std::string& key) {
    for (const Group& g : groups()) {
        for (const char* k : g.keys) {
            if (key == k) return true;
        }
    }
    return false;
}

bool isNote(const std::string& key) {
    return key == "note" || key.find("_note") != std::string::npos || key.find("_from") != std::string::npos;
}

std::string number(double v) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.5g", v);
    return text;
}

}  // namespace

void LightPanel::read(const std::string& root) {
    base_ = core::parseJsonFile(root + "/sheets/lighting.json");
    sheet_ = file_ == "content/sheets/lighting.json" ? base_ : core::parseJsonFile(root + "/" + file_);
    stamps_[0] = core::fileModified(root + "/sheets/lighting.json");
    stamps_[1] = core::fileModified(root + "/" + file_);
}

bool LightPanel::write(Cooker& cooker, const std::string& path, const std::string& value, const char* how) {
    std::string answer;
    saidOk_ = cooker.edit(file_, path, value, &answer, how);
    said_ = answer;
    return saidOk_;
}

void LightPanel::knob(Cooker& cooker, const std::string& key) {
    const bool onBase = file_ == "content/sheets/lighting.json";
    const core::Json& own = sheet_[key.c_str()];
    const core::Json& inherited = base_[key.c_str()];
    const core::Json& shown = own.isNull() ? inherited : own;
    if (shown.isNull()) return;
    ImGui::PushID(key.c_str());
    const std::string at = "[\"" + key + "\"]";
    const bool mine = !own.isNull();
    if (!mine) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
    if (shown.type == core::Json::Type::Number) {
        float v = float(shown.number);
        ImGui::SetNextItemWidth(-11.0f * ImGui::GetFontSize());
        ImGui::BeginDisabled(!mine);
        ImGui::DragFloat(key.c_str(), &v, 0.005f, 0.0f, 0.0f, "%.4g");
        if (mine && ImGui::IsItemDeactivatedAfterEdit()) write(cooker, at, number(v), "set");
        ImGui::EndDisabled();
    } else if (shown.type == core::Json::Type::Array && shown.size() == 3) {
        float v[3] = {float(shown.at(0).numberOr(0)), float(shown.at(1).numberOr(0)), float(shown.at(2).numberOr(0))};
        ImGui::SetNextItemWidth(-11.0f * ImGui::GetFontSize());
        ImGui::BeginDisabled(!mine);
        // A colour, mostly (sun_colour, tint_low); dragged as numbers, since some run past 1.
        ImGui::DragFloat3(key.c_str(), v, 0.005f, 0.0f, 0.0f, "%.3g");
        if (mine && ImGui::IsItemDeactivatedAfterEdit()) {
            for (int i = 0; i < 3; ++i) {
                if (float(shown.at(size_t(i)).numberOr(0)) != v[i] &&
                    !write(cooker, "[\"" + key + "\", " + std::to_string(i) + "]", number(v[i]), "set")) {
                    break;
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::ColorButton("##swatch", ImVec4(v[0], v[1], v[2], 1.0f), ImGuiColorEditFlags_NoTooltip);
    } else {
        ImGui::TextColored(kDim, "%s: %s", key.c_str(), shown.type == core::Json::Type::String ? shown.string.c_str()
                                                                                                : "(a list or block)");
    }
    if (!mine) ImGui::PopStyleVar();
    if (!mine && !onBase) {
        ImGui::SameLine();
        if (ImGui::SmallButton("set here")) {
            std::string value;
            if (inherited.type == core::Json::Type::Number) {
                value = number(inherited.number);
            } else if (inherited.type == core::Json::Type::Array) {
                value = "[";
                for (size_t i = 0; i < inherited.size(); ++i) value += (i ? ", " : "") + number(inherited.at(i).numberOr(0));
                value += "]";
            }
            if (!value.empty()) write(cooker, at, value, "insert");
        }
        ImGui::SetItemTooltip("this layer does not set it: it inherits the value shown. Write it here to tune it");
    }
    ImGui::PopID();
}

int LightPanel::draw(Cooker& cooker, const std::string& root, const std::string& world, int time) {
    int asked = -1;
    if (world != world_ || layers_.empty()) {
        world_ = world;
        layers_.clear();
        if (!world.empty()) layers_.push_back({"this map: " + world, "content/sheets/worlds/" + world + ".json"});
        layers_.push_back({"everywhere (the base)", "content/sheets/lighting.json"});
        layers_.push_back({"dusk, over every map", "content/sheets/time/dusk.json"});
        layers_.push_back({"night, over every map", "content/sheets/time/night.json"});
        if (!world.empty() && core::fileExists(root + "/sheets/worlds/" + world + "_rain.json")) {
            layers_.push_back({"this map in rain", "content/sheets/worlds/" + world + "_rain.json"});
        }
        layer_ = 0;
        file_.clear();
    }
    // The time of day it is seen at.
    const char* times[] = {"Noon", "Dusk", "Night"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(times[i], time == i)) asked = i;
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##layer", layers_[size_t(layer_)].label.c_str())) {
        for (size_t i = 0; i < layers_.size(); ++i) {
            if (ImGui::Selectable(layers_[i].label.c_str(), int(i) == layer_)) layer_ = int(i);
        }
        ImGui::EndCombo();
    }
    const std::string file = layers_[size_t(layer_)].file;
    if (file != file_ || core::fileModified(root + "/sheets/lighting.json") != stamps_[0] ||
        core::fileModified(root + "/" + file) != stamps_[1]) {
        file_ = file;
        read(root);
    }
    ImGui::PushTextWrapPos(0.0f);
    if (sheet_.isNull()) {
        ImGui::TextColored(kRed, "%s does not read", file_.c_str());
        ImGui::PopTextWrapPos();
        return asked;
    }
    if (world.empty() && layer_ == 0) {
        ImGui::TextColored(kDim, "the Stage wears the base alone: choose a world as the scene to tune its map");
    }
    ImGui::TextColored(kDim, "%s%s", file_.c_str(),
                       file_ == "content/sheets/lighting.json" ? ": every map's light" : ", over the base");
    // The layer's note is its history, at length (Aida's runs to a page): folded until asked.
    if (!sheet_["note"].isNull() && ImGui::TreeNode("why it is lit so (the sheet's note)")) {
        ImGui::TextColored(kDim, "%s", sheet_["note"].stringOr("").c_str());
        ImGui::TreePop();
    }
    if (!said_.empty()) ImGui::TextColored(saidOk_ ? kGreen : kRed, "%s", said_.c_str());
    ImGui::PopTextWrapPos();

    ImGui::BeginChild("light");
    for (const Group& g : groups()) {
        if (!ImGui::CollapsingHeader(g.title, ImGuiTreeNodeFlags_DefaultOpen)) continue;
        for (const char* k : g.keys) knob(cooker, k);
    }
    if (ImGui::CollapsingHeader("The rest")) {
        // The layer's own keys and the base's, each once: grass, water, metal, the refinement.
        std::vector<std::string> rest;
        for (const core::Json* doc : {&sheet_, &base_}) {
            for (const auto& [k, v] : doc->members) {
                if (grouped(k) || isNote(k)) continue;
                bool seen = false;
                for (const std::string& r : rest) seen |= r == k;
                if (!seen) rest.push_back(k);
            }
        }
        for (const std::string& k : rest) knob(cooker, k);
    }
    ImGui::EndChild();
    return asked;
}

}  // namespace mu::studio
