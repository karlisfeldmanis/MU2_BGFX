#include "studio/inspector.h"

#include <cstdio>
#include <cstring>

#include "imgui.h"

namespace mu::studio {
namespace {

const ImVec4 kGold(0.86f, 0.72f, 0.42f, 1.0f);
const ImVec4 kGreen(0.52f, 0.80f, 0.52f, 1.0f);
const ImVec4 kRed(0.92f, 0.42f, 0.38f, 1.0f);
const ImVec4 kDim(0.58f, 0.58f, 0.60f, 1.0f);

// The prose beside a value, which the recipes carry at length: shown on asking.
bool isNote(const std::string& key) {
    const size_t n = key.size();
    auto ends = [&](const char* tail) {
        const size_t t = std::strlen(tail);
        return n >= t && key.compare(n - t, t, tail) == 0;
    };
    return key == "note" || key == "why" || key == "comment" || key == "from" || ends("_note") ||
           ends("_from") || ends("_why") || key.rfind("why_", 0) == 0;
}

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            out += c;
        }
    }
    return out + "\"";
}

std::string number(double v) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.9g", v);
    return text;
}

std::string extend(const std::string& path, const std::string& step) {
    return path.size() <= 2 ? "[" + step + "]" : path.substr(0, path.size() - 1) + ", " + step + "]";
}

}  // namespace

int Inspector::draw(Cooker& cooker, const std::string& target, int plus, float x, float y, float w,
                    float h, const std::string& root, const std::string& world, int* time) {
    int asked = -1;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Inspector", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    // The view's own knob first: not written anywhere.
    int shown = plus;
    ImGui::SetNextItemWidth(10.0f * ImGui::GetFontSize());
    if (ImGui::SliderInt("refinement", &shown, 0, 11, "+%d")) asked = shown;
    ImGui::SetItemTooltip("the shine at +N, as the game draws it: a view, not written");
    if (!said_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(saidOk_ ? kGreen : kRed, "%s", said_.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::BeginTabBar("inspect")) {
        if (ImGui::BeginTabItem("Recipe")) {
            tab(cooker, recipe_, target, false);
            ImGui::EndTabItem();
        }
        const ImGuiTabItemFlags lightFlags = showLight_ ? ImGuiTabItemFlags_SetSelected : 0;
        showLight_ = false;
        if (ImGui::BeginTabItem("Light", nullptr, lightFlags)) {
            const int asked = light_.draw(cooker, root, world, *time);
            if (asked >= 0) *time = asked;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    return asked;
}

void Inspector::tab(Cooker& cooker, RecipeView& view, const std::string& target, bool light) {
    if (target.empty()) {
        ImGui::TextColored(kDim, "no recipe for what is on the bench");
        return;
    }
    // Asked once a subject, and kept here: the copy is edited in place after each write
    // rather than read again, so a typed value does not flicker back while the file settles.
    if (view.target != target || !view.answered) {
        if (cooker.connected()) cooker.askRecipe(target);
        RecipeView fresh = cooker.recipe();
        if (fresh.target == target && fresh.answered) {
            view = std::move(fresh);
            if (!light) cooker.forgetRecipe();   // the light's tab asks for its own next
        } else {
            view.target.clear();
            ImGui::TextColored(kDim, cooker.connected() ? "reading..." : "mu serve is not running");
            return;
        }
    }
    if (!view.error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kRed, "%s", view.error.c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    if (!light) ImGui::Checkbox("notes", &notes_);
    ImGui::BeginChild(light ? "light" : "recipe");
    for (size_t f = 0; f < view.files.size(); ++f) {
        RecipeView::File& file = view.files[f];
        ImGui::PushID(int(f));
        const bool open = view.files.size() == 1 ||
                          ImGui::TreeNodeEx(file.path.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (view.files.size() == 1) ImGui::TextColored(kDim, "%s", file.path.c_str());
        if (open) {
            for (auto& [key, value] : file.doc.members) {
                node(cooker, view, f, value, key, extend("[]", quote(key)), light);
            }
            if (view.files.size() > 1) ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

bool Inspector::commit(Cooker& cooker, const std::string& file, const std::string& path,
                       const std::string& value) {
    std::string answer;
    saidOk_ = cooker.edit(file, path, value, &answer);
    said_ = answer;
    if (saidOk_) cooker.forgetPlan();   // what the edit costs, on the card at once
    return saidOk_;
}

void Inspector::node(Cooker& cooker, RecipeView& view, size_t file, core::Json& value,
                     const std::string& key, const std::string& path, bool light) {
    using Type = core::Json::Type;
    const std::string& where = view.files[file].path;
    if (value.type == Type::String && isNote(key)) {
        if (!notes_) return;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kDim, "%s: %s", key.c_str(), value.string.c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::PushID(path.c_str());
    if (value.type == Type::Object) {
        if (ImGui::TreeNode(key.c_str())) {
            for (auto& [k, v] : value.members) node(cooker, view, file, v, k, extend(path, quote(k)), light);
            ImGui::TreePop();
        }
    } else if (value.type == Type::Array) {
        bool numbers = !value.items.empty() && value.items.size() <= 4;
        for (const core::Json& item : value.items) numbers = numbers && item.type == Type::Number;
        if (numbers) {
            // A colour or a vector: its numbers on one line, each written alone.
            ImGui::TextUnformatted(key.c_str());
            const float each = 4.5f * ImGui::GetFontSize();
            for (size_t i = 0; i < value.items.size(); ++i) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(each);
                ImGui::PushID(int(i));
                const std::string at = extend(path, std::to_string(i));
                double v = numbers_.count(at) ? numbers_[at] : value.items[i].number;
                if (ImGui::DragScalar("##n", ImGuiDataType_Double, &v, light ? 0.005f : 0.01f, nullptr,
                                      nullptr, "%.3f")) {
                    numbers_[at] = v;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && commit(cooker, where, at, number(v))) {
                    value.items[i].number = v;
                }
                if (!ImGui::IsItemActive()) numbers_.erase(at);
                ImGui::PopID();
            }
        } else if (ImGui::TreeNode(key.c_str(), "%s  [%zu]", key.c_str(), value.items.size())) {
            for (size_t i = 0; i < value.items.size(); ++i) {
                core::Json& item = value.items[i];
                // An island reads by its number and its material, not by its index.
                std::string label = std::to_string(i);
                if (item.type == Type::Object && !item["material"].isNull()) {
                    label += "  " + item["material"].stringOr("");
                }
                const std::string at = extend(path, std::to_string(i));
                if (item.type == Type::Object || item.type == Type::Array) {
                    ImGui::PushID(int(i));
                    if (ImGui::TreeNode(label.c_str())) {
                        if (item.type == Type::Object) {
                            for (auto& [k, v] : item.members) {
                                node(cooker, view, file, v, k, extend(at, quote(k)), light);
                            }
                        } else {
                            node(cooker, view, file, item, label, at, light);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                } else {
                    node(cooker, view, file, item, label, at, light);
                }
            }
            ImGui::TreePop();
        }
    } else if (value.type == Type::Bool) {
        bool on = value.boolean;
        if (ImGui::Checkbox(key.c_str(), &on) && commit(cooker, where, path, on ? "true" : "false")) {
            value.boolean = on;
        }
    } else if (value.type == Type::Number) {
        ImGui::SetNextItemWidth(8.0f * ImGui::GetFontSize());
        double v = numbers_.count(path) ? numbers_[path] : value.number;
        const bool changed =
            light ? ImGui::DragScalar(key.c_str(), ImGuiDataType_Double, &v, 0.01f, nullptr, nullptr, "%.4g")
                  : ImGui::InputDouble(key.c_str(), &v, 0.0, 0.0, "%.6g");
        if (changed) numbers_[path] = v;
        if (ImGui::IsItemDeactivatedAfterEdit() && commit(cooker, where, path, number(v))) {
            value.number = v;
        }
        if (!ImGui::IsItemActive()) numbers_.erase(path);
    } else if (value.type == Type::String && key == "material" && !view.materials.empty()) {
        // An island's material: one of the library's, which a rebuild bakes in.
        ImGui::SetNextItemWidth(12.0f * ImGui::GetFontSize());
        if (ImGui::BeginCombo(key.c_str(), value.string.c_str())) {
            for (const std::string& m : view.materials) {
                if (ImGui::Selectable(m.c_str(), m == value.string) && m != value.string &&
                    commit(cooker, where, path, quote(m))) {
                    value.string = m;
                }
            }
            ImGui::EndCombo();
        }
    } else if (value.type == Type::String && (key == "item" || key == "mesh")) {
        // A name other files point at: renaming it here would leave them pointing at nothing.
        ImGui::TextColored(kDim, "%s  %s", value.string.c_str(), key.c_str());
    } else if (value.type == Type::String) {
        std::string& word = words_.count(path) ? words_[path] : (words_[path] = value.string);
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "%s", word.c_str());
        ImGui::SetNextItemWidth(14.0f * ImGui::GetFontSize());
        if (ImGui::InputText(key.c_str(), buffer, sizeof(buffer))) word = buffer;
        if (ImGui::IsItemDeactivatedAfterEdit() && commit(cooker, where, path, quote(word))) {
            value.string = word;
        }
        if (!ImGui::IsItemActive()) words_.erase(path);
    } else {
        ImGui::TextColored(kDim, "%s: null", key.c_str());
    }
    ImGui::PopID();
}

}  // namespace mu::studio
