#include "studio/panels.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "imgui.h"
#include "studio/cooker.h"

namespace mu::studio {
namespace {

constexpr ImGuiWindowFlags kFixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

const ImVec4 kGold(0.86f, 0.72f, 0.42f, 1.0f);
const ImVec4 kGreen(0.52f, 0.80f, 0.52f, 1.0f);
const ImVec4 kRed(0.92f, 0.42f, 0.38f, 1.0f);
const ImVec4 kDim(0.58f, 0.58f, 0.60f, 1.0f);

ImVec4 stateColour(const std::string& state) {
    if (state == "done") return kGreen;
    if (state == "failed") return kRed;
    if (state == "running") return kGold;
    return kDim;
}

std::string clock(double seconds) {
    if (seconds < 0.0) return "?";
    const int s = int(seconds + 0.5);
    char text[32];
    if (s >= 60) {
        std::snprintf(text, sizeof(text), "%d:%02d", s / 60, s % 60);
    } else {
        std::snprintf(text, sizeof(text), "%d s", s);
    }
    return text;
}

bool contains(const std::string& text, const char* needle) {
    if (!needle[0]) return true;
    std::string a = text, b = needle;
    for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : b) c = char(std::tolower(static_cast<unsigned char>(c)));
    return a.find(b) != std::string::npos;
}

// How far a job is, 0 to 1: by the steps' estimates when every one has been timed, by their
// count when not, the running step counted by its bar.
float fraction(const JobView& job) {
    if (job.state == "done") return 1.0f;
    if (job.steps.empty()) return 0.0f;
    bool timed = true;
    for (const StepView& s : job.steps) timed = timed && s.estimate > 0.0;
    double whole = 0.0, done = 0.0;
    for (size_t i = 0; i < job.steps.size(); ++i) {
        const StepView& s = job.steps[i];
        const double weight = timed ? s.estimate : 1.0;
        whole += weight;
        if (s.state == "done" || s.state == "skipped") {
            done += weight;
        } else if (int(i) == job.at && job.of > 0) {
            done += weight * double(job.n) / double(job.of);
        }
    }
    return whole > 0.0 ? float(done / whole) : 0.0f;
}

}  // namespace

Asked Panels::draw(const Shelf& shelf, Cooker& cooker, float width, float height, double now) {
    asked_ = Asked{};
    Asked& asked = asked_;
    // In ems, so the panels keep their shape on a Retina display, where the font is doubled.
    const float em = ImGui::GetFontSize();
    const float pad = 0.7f * em;
    const float jobsH = std::max(12.0f * em, height * 0.26f);
    const float leftW = std::max(17.0f * em, width * 0.18f);
    const float cardW = std::max(21.0f * em, width * 0.22f);
    browser(shelf, asked, pad, pad, leftW, height - jobsH - 3.0f * pad);
    subject(shelf, cooker, width - cardW - pad, pad, cardW);
    jobs(cooker, pad, height - jobsH - pad, width - 2.0f * pad, jobsH, now);
    return asked_;
}

void Panels::browser(const Shelf& shelf, Asked& asked, float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Browser", nullptr, kFixed);
    if (ImGui::BeginCombo("##category", shelf.categories.empty()
                                            ? ""
                                            : shelf.categories[size_t(shelf.category)].c_str())) {
        for (size_t i = 0; i < shelf.categories.size(); ++i) {
            if (ImGui::Selectable(shelf.categories[i].c_str(), int(i) == shelf.category)) {
                asked.category = int(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "filter", filter_, sizeof(filter_));
    ImGui::BeginChild("names");
    for (size_t i = 0; i < shelf.names.size(); ++i) {
        if (!contains(shelf.names[i], filter_)) continue;
        const bool on = int(i) == shelf.selected;
        if (ImGui::Selectable(shelf.names[i].c_str(), on)) asked.pick = int(i);
        if (on && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
    }
    ImGui::EndChild();
    ImGui::End();
}

void Panels::subject(const Shelf& shelf, Cooker& cooker, float x, float y, float w) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    ImGui::Begin("Subject", nullptr, kFixed | ImGuiWindowFlags_AlwaysAutoResize);
    const std::string name = shelf.selected >= 0 && shelf.selected < int(shelf.names.size())
                                 ? shelf.names[size_t(shelf.selected)]
                                 : std::string("nothing on the bench");
    ImGui::TextColored(kGold, "%s", name.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    if (!shelf.clip.empty()) ImGui::TextUnformatted(shelf.clip.c_str());
    if (!shelf.time.empty()) ImGui::TextUnformatted(shelf.time.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    if (shelf.target.empty()) {
        ImGui::TextColored(kDim, "no recipe to cook it from");
        ImGui::End();
        return;
    }
    ImGui::TextColored(kDim, "mu make %s", shelf.target.c_str());

    // A finished job may have cooked this one: the plan is asked again.
    size_t finished = 0;
    for (const JobView& j : cooker.jobs()) finished += j.finished() ? 1 : 0;
    if (finished != finishedSeen_) {
        finishedSeen_ = finished;
        cooker.forgetPlan();
    }
    if (cooker.connected()) cooker.askPlan(shelf.target);
    const PlanView plan = cooker.plan();

    ImGui::Separator();
    if (!cooker.connected()) {
        ImGui::TextColored(kDim, "mu serve is not running");
    } else if (!plan.answered) {
        ImGui::TextColored(kDim, "asking what a make would run...");
    } else if (!plan.error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kRed, "%s", plan.error.c_str());
        ImGui::PopTextWrapPos();
    } else if (!plan.stale()) {
        ImGui::TextColored(kGreen, "cooked: nothing is newer than it");
    } else {
        int count = 0;
        for (const PlanView::Step& s : plan.steps) count += s.needed ? 1 : 0;
        ImGui::TextColored(kGold, "stale: %d of %zu steps would run", count, plan.steps.size());
        ImGui::PushTextWrapPos(0.0f);
        // Every world's tables for one reason are one line, not thirteen.
        int tables = 0;
        std::string tablesWhy;
        for (const PlanView::Step& s : plan.steps) {
            if (!s.needed) continue;
            if (s.name.rfind("tables ", 0) == 0 && (tables == 0 || s.reason == tablesWhy)) {
                ++tables;
                tablesWhy = s.reason;
                continue;
            }
            ImGui::BulletText("%s", s.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kDim, "%s", s.reason.c_str());
        }
        if (tables > 0) {
            ImGui::BulletText("tables, %d world%s", tables, tables == 1 ? "" : "s");
            ImGui::SameLine();
            ImGui::TextColored(kDim, "%s", tablesWhy.c_str());
        }
        ImGui::PopTextWrapPos();
    }
    if (!shelf.swapped.empty()) {
        ImGui::TextColored(kGreen, "swapped in from disk");
        ImGui::SameLine();
        if (ImGui::SmallButton("Before / after")) asked_.openSheet = true;
        ImGui::SetItemTooltip("%s", shelf.swapped.c_str());
    }
    if (ImGui::SmallButton("Read again")) asked_.reread = true;
    ImGui::SetItemTooltip("what is on disk now, swapped in with a before and after: for a cook "
                          "run by hand or by another session");
    ImGui::BeginDisabled(!cooker.connected() || !plan.answered || !plan.error.empty());
    if (ImGui::Button(plan.answered && plan.error.empty() && !plan.stale() ? "Cook anyway" : "Cook")) {
        cooker.make(shelf.target);
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void Panels::jobs(Cooker& cooker, float x, float y, float w, float h, double now) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Jobs", nullptr, kFixed);
    if (cooker.connected()) {
        ImGui::TextColored(kGreen, "mu serve");
        ImGui::SameLine();
        ImGui::TextColored(kDim, "one job at a time; closing the studio leaves a cook running");
    } else {
        ImGui::TextColored(kDim, "mu serve is not running");
        ImGui::SameLine();
        if (ImGui::SmallButton("Start it")) cooker.startServer();
        if (!cooker.lastError().empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kRed, "%s", cooker.lastError().c_str());
        }
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                         5.5f * ImGui::GetFontSize());
    ImGui::BeginDisabled(!cooker.connected());
    if (ImGui::SmallButton("Rehearsal")) cooker.make("rehearsal");
    ImGui::SetItemTooltip("three steps that print bars and cook nothing");
    ImGui::EndDisabled();

    ImGui::BeginChild("rows");
    const std::vector<JobView>& all = cooker.jobs();
    if (all.empty()) ImGui::TextColored(kDim, "no jobs yet: pick something and Cook it");
    if (ImGui::BeginTable("jobs", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        const float em = ImGui::GetFontSize();
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 2.0f * em);
        ImGui::TableSetupColumn("job", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("step", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("progress", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("time", ImGuiTableColumnFlags_WidthFixed, 7.0f * em);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 7.0f * em);
        // Newest first: what is cooking now is at the top.
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            const JobView& job = *it;
            ImGui::PushID(job.number);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%d", job.number);
            ImGui::TableNextColumn();
            const bool open = ImGui::TreeNodeEx(job.target.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::SameLine();
            ImGui::TextColored(stateColour(job.state), "%s", job.state.c_str());
            ImGui::TableNextColumn();
            if (job.state == "running" && job.at >= 0 && job.at < int(job.steps.size())) {
                ImGui::Text("%d/%zu %s", job.at + 1, job.steps.size(),
                            job.steps[size_t(job.at)].name.c_str());
            } else if (!job.error.empty()) {
                ImGui::TextColored(kRed, "%s", job.error.c_str());
                ImGui::SetItemTooltip("%s", job.error.c_str());
            } else {
                ImGui::TextColored(kDim, "%zu steps", job.steps.size());
            }
            ImGui::TableNextColumn();
            {
                // The job's bar; the step's own bar and latest word written over it.
                std::string over;
                if (job.state == "running") {
                    if (job.of > 0) {
                        char text[160];
                        std::snprintf(text, sizeof(text), "%d/%d %s", job.n, job.of, job.label.c_str());
                        over = text;
                    } else {
                        over = job.line;
                    }
                }
                ImGui::ProgressBar(fraction(job), ImVec2(-1.0f, 0.0f), over.c_str());
            }
            ImGui::TableNextColumn();
            if (job.state == "running") {
                ImGui::Text("~%s left", clock(job.eta(now)).c_str());
            } else if (job.finished() && job.started > 0.0) {
                ImGui::TextColored(kDim, "%s", clock(job.ended - job.started).c_str());
            } else {
                ImGui::TextColored(kDim, "waiting");
            }
            ImGui::TableNextColumn();
            if (!job.finished()) {
                if (ImGui::SmallButton("Stop")) cooker.cancel(job.number, false);
                ImGui::SetItemTooltip("after the step it is in: a cook stopped between its "
                                      "textures and its meshes draws white");
                if (job.state == "running") {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Kill")) cooker.cancel(job.number, true);
                    ImGui::SetItemTooltip("the running step now, Blender and all");
                }
            }
            if (open) {
                for (const StepView& s : job.steps) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(stateColour(s.state), "  %s", s.state.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", s.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    if (s.seconds >= 0.0) {
                        ImGui::TextColored(kDim, "%s", clock(s.seconds).c_str());
                    } else if (s.estimate >= 0.0) {
                        ImGui::TextColored(kDim, "~%s", clock(s.estimate).c_str());
                    }
                    ImGui::TableNextColumn();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace mu::studio
