#include "studio/checklist.h"

#include <cstdlib>
#include <fstream>

#include "imgui.h"

namespace mu::studio {
namespace {

const ImVec4 kGreen(0.52f, 0.80f, 0.52f, 1.0f);
const ImVec4 kTodo(0.92f, 0.62f, 0.38f, 1.0f);
const ImVec4 kDim(0.58f, 0.58f, 0.60f, 1.0f);

const char* nextState(const std::string& state) {
    if (state == "todo") return "done";
    if (state == "done") return "skip";
    return "todo";
}

ImVec4 colour(const std::string& state) {
    if (state == "done") return kGreen;
    if (state == "skip") return kDim;
    return kTodo;
}

// "MONSTER01_ATTACK1" as MU's action reads in a line: "ATTACK1".
std::string actionWord(const std::string& action) {
    const std::string prefix = "MONSTER01_";
    return action.rfind(prefix, 0) == 0 ? action.substr(prefix.size()) : action;
}

}  // namespace

const std::string& Checklist::code(const std::string& root, const std::string& at, int lines) {
    auto found = codes_.find(at);
    if (found != codes_.end()) return found->second;
    std::string text;
    const size_t colon = at.rfind(':');
    if (colon != std::string::npos) {
        const int first = std::atoi(at.c_str() + colon + 1);
        const int count = lines > 0 ? (lines < 40 ? lines : 40) : 12;
        std::ifstream in(root + "/../LEGACY/reference/" + at.substr(0, colon));
        std::string line;
        for (int n = 1; std::getline(in, line) && n < first + count; ++n) {
            if (n >= first) text += line + "\n";
        }
        if (lines > 40) text += "...\n";
    }
    if (text.empty()) text = "MuMain's source is not at LEGACY/reference here";
    return codes_.emplace(at, text).first->second;
}

void Checklist::draw(Cooker& cooker, const std::string& file, core::Json& list, const std::string& root,
                     std::string* said, bool* ok) {
    int done = 0, skipped = 0;
    for (const core::Json& item : list.items) {
        const std::string state = item["state"].stringOr("todo");
        done += state == "done";
        skipped += state == "skip";
    }
    const int todo = int(list.items.size()) - done - skipped;
    char title[128];
    std::snprintf(title, sizeof(title), "MU checklist: %d done, %d set aside, %d to do###checklist", done, skipped,
                  todo);
    ImGui::PushStyleColor(ImGuiCol_Text, todo == 0 ? kGreen : kTodo);
    const bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("everything MuMain does for this monster (mu research); a state is a click");
    if (!open) return;
    for (size_t i = 0; i < list.items.size(); ++i) {
        core::Json& item = list.items[i];
        ImGui::PushID(int(i));
        const std::string state = item["state"].stringOr("todo");
        ImGui::PushStyleColor(ImGuiCol_Text, colour(state));
        if (ImGui::SmallButton(state.c_str())) {
            const std::string next = nextState(state);
            const std::string path = "[\"mu_checklist\", " + std::to_string(i) + ", \"state\"]";
            std::string answer;
            *ok = cooker.edit(file, path, "\"" + next + "\"", &answer);
            *said = answer;
            if (*ok) {
                for (auto& [key, value] : item.members) {
                    if (key == "state") value.string = next;
                }
            }
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%-9s", item["kind"].stringOr("").c_str());
        ImGui::SameLine();
        std::string line = item["in"].stringOr("");
        const size_t scope = line.rfind("::");
        if (scope != std::string::npos) line = line.substr(scope + 2);
        std::string actions;
        for (const core::Json& a : item["actions"].items) {
            actions += (actions.empty() ? "" : " ") + actionWord(a.stringOr(""));
        }
        if (!actions.empty()) line += "  on " + actions;
        const std::string sound = item["file"].stringOr("");
        if (!sound.empty()) line += "  " + sound;
        ImGui::TextUnformatted(line.c_str());
        if (ImGui::IsItemHovered()) {
            const std::string at = item["at"].stringOr("");
            ImGui::BeginTooltip();
            ImGui::TextColored(kDim, "%s", at.c_str());
            std::string calls;
            for (const core::Json& c : item["calls"].items) calls += (calls.empty() ? "" : ", ") + c.stringOr("");
            if (!calls.empty()) ImGui::TextColored(kDim, "%s", calls.c_str());
            ImGui::TextUnformatted(code(root, at, int(item["lines"].numberOr(1.0))).c_str());
            ImGui::EndTooltip();
        }
        const std::string note = item["note"].stringOr("");
        if (!note.empty()) {
            ImGui::Indent();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kDim, "%s", note.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::Separator();
}

}  // namespace mu::studio
