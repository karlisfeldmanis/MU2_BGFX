#include "app/modes/studio_marks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "app/context.h"
#include "app/modes/studio_place.h"
#include "game/world/world.h"
#include "imgui.h"
#include "studio/cooker.h"

namespace mu::app {
namespace {

const ImVec4 kGold(0.86f, 0.72f, 0.42f, 1.0f);
const ImVec4 kGreen(0.52f, 0.80f, 0.52f, 1.0f);
const ImVec4 kRed(0.92f, 0.42f, 0.38f, 1.0f);
const ImVec4 kDim(0.58f, 0.58f, 0.60f, 1.0f);

const ImU32 kEnter = IM_COL32(232, 150, 64, 220);
const ImU32 kExit = IM_COL32(96, 200, 204, 220);
const ImU32 kFolk = IM_COL32(220, 184, 107, 230);
const ImU32 kPicked = IM_COL32(255, 255, 255, 255);

int whole(const core::Json& j) { return int(std::lround(j.numberOr(0.0))); }

}  // namespace

int placeModeBar(float x, float y, float w, int mode) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    ImGui::Begin("##place what", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    const char* names[] = {"Objects", "Gates", "Townsfolk", "Ground"};
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(names[i], mode == i)) mode = i;
    }
    ImGui::End();
    return mode;
}

void StudioMarks::world(Context& ctx, const std::string& name) {
    if (name == world_) return;
    world_ = name;
    file_ = "source/world/" + name + "/world.json";
    picked_ = -1;
    dragging_ = released_ = false;
    said_.clear();
    load(ctx);
    worlds_.clear();
    std::error_code error;
    for (const auto& dir : std::filesystem::directory_iterator(ctx.paths.root + "/source/world", error)) {
        const std::string other = dir.path().filename().string();
        const core::Json doc = core::parseJsonFile(dir.path().string() + "/world.json");
        if (other != name && !doc["rules"]["arrive"].isNull()) worlds_.push_back(other);
    }
    std::sort(worlds_.begin(), worlds_.end());
    leadsTo_ = 0;
}

void StudioMarks::kind(Context& ctx, Kind k) {
    if (k == kind_) return;
    kind_ = k;
    picked_ = -1;
    dragging_ = released_ = false;
    said_.clear();
    if (!file_.empty()) load(ctx);
}

void StudioMarks::load(Context& ctx) {
    manifest_ = core::parseJsonFile(ctx.paths.root + "/" + file_);
    marks_.clear();
    if (kind_ == Kind::Townsfolk) {
        const core::Json& folk = manifest_["npcs"];
        for (size_t i = 0; i < folk.size(); ++i) {
            const core::Json& row = folk.at(i);
            Mark m;
            m.index = int(i);
            m.number = whole(row["npc"]);
            m.name = row["name"].stringOr("?");
            m.box[0] = m.box[2] = whole(row["at"].at(0));
            m.box[1] = m.box[3] = whole(row["at"].at(1));
            m.faces[0] = whole(row["look"]);
            marks_.push_back(m);
        }
    } else {
        for (const char* list : {"enters", "exits"}) {
            const core::Json& gates = manifest_["gates"][list];
            for (size_t i = 0; i < gates.size(); ++i) {
                const core::Json& row = gates.at(i);
                Mark m;
                m.list = list;
                m.index = int(i);
                m.number = whole(row["number"]);
                for (int k = 0; k < 4; ++k) m.box[k] = whole(row["box"].at(size_t(k)));
                m.level = whole(row["level"]);
                m.target = whole(row["target"]);
                for (int k = 0; k < 2; ++k) m.faces[k] = whole(row["faces"].at(size_t(k)));
                marks_.push_back(m);
            }
        }
    }
    for (Mark& m : marks_) {
        std::copy(m.box, m.box + 4, m.stored);
        m.storedLevel = m.level, m.storedTarget = m.target;
        std::copy(m.faces, m.faces + 2, m.storedFaces);
    }
    if (picked_ >= int(marks_.size())) picked_ = -1;
}

std::string StudioMarks::path(const Mark& m, const char* field, int element) const {
    std::string p = m.list.empty() ? "[\"npcs\", " + std::to_string(m.index)
                                   : "[\"gates\", \"" + m.list + "\", " + std::to_string(m.index);
    p += std::string(", \"") + field + "\"";
    if (element >= 0) p += ", " + std::to_string(element);
    return p + "]";
}

bool StudioMarks::write(Context& ctx, studio::Cooker& cooker, std::string* answer) {
    if (picked_ < 0) return *answer = "nothing picked", false;
    const Mark m = marks_[size_t(picked_)];
    bool ok = true, wrote = false;
    auto put = [&](const char* field, int element, int was, int now) {
        if (!ok || was == now) return;
        ok = cooker.edit(file_, path(m, field, element), std::to_string(now), answer);
        wrote |= ok;
    };
    if (m.list.empty()) {
        for (int k = 0; k < 2; ++k) put("at", k, m.stored[k], m.box[k]);
        put("look", -1, m.storedFaces[0], m.faces[0]);
    } else {
        for (int k = 0; k < 4; ++k) put("box", k, m.stored[k], m.box[k]);
        if (m.list == "enters") {
            put("level", -1, m.storedLevel, m.level);
            put("target", -1, m.storedTarget, m.target);
        } else {
            for (int k = 0; k < 2; ++k) put("faces", k, m.storedFaces[k], m.faces[k]);
        }
    }
    if (ok && !wrote) *answer = "nothing changed";
    if (ok && wrote) {
        *answer = "written to " + file_ + "; `mu make " + target() + "` makes it the game's";
        cooker.forgetPlan();
    }
    load(ctx);   // what is drawn is what the file says, written or refused
    said_ = *answer;
    saidOk_ = ok;
    return ok;
}

bool StudioMarks::pointer(Context& ctx, game::World& world, const gfx::Camera& camera, bool overPanels) {
    if (manifest_.isNull() || !world.town().isOpen()) return false;
    const float m = world.ground().metresPerTile();
    float g[3];
    if (dragging_) {
        if (ctx.window.held(0)) {
            if (groundUnderPointer(ctx, world, camera, g)) {
                Mark& mark = marks_[size_t(picked_)];
                const int dc = int(std::floor(g[0] / m)) - grab_[0] - mark.box[0];
                const int dr = int(std::floor(-g[2] / m)) - grab_[1] - mark.box[1];
                for (int k = 0; k < 4; k += 2) mark.box[k] += dc;
                for (int k = 1; k < 4; k += 2) mark.box[k] += dr;
            }
            return true;
        }
        dragging_ = false;
        released_ = true;   // written by the panel, which holds the cooker
        return true;
    }
    if (overPanels || !ctx.window.clicked(0) || !groundUnderPointer(ctx, world, camera, g)) return false;
    const int column = int(std::floor(g[0] / m)), row = int(std::floor(-g[2] / m));
    int best = -1;
    float bestScore = 1e30f;
    for (size_t i = 0; i < marks_.size(); ++i) {
        const Mark& mark = marks_[i];
        float score;
        if (kind_ == Kind::Townsfolk) {
            const float dx = g[0] / m - (float(mark.box[0]) + 0.5f), dz = -g[2] / m - (float(mark.box[1]) + 0.5f);
            score = dx * dx + dz * dz;
            if (score > 1.5f * 1.5f) continue;
        } else {
            const int x0 = std::min(mark.box[0], mark.box[2]), x1 = std::max(mark.box[0], mark.box[2]);
            const int y0 = std::min(mark.box[1], mark.box[3]), y1 = std::max(mark.box[1], mark.box[3]);
            if (column < x0 || column > x1 || row < y0 || row > y1) continue;
            score = float((x1 - x0 + 1) * (y1 - y0 + 1));   // the smallest box the tile is in
        }
        if (score < bestScore) bestScore = score, best = int(i);
    }
    picked_ = best;
    if (best < 0) return false;   // the land: a drag there orbits
    said_.clear();
    grab_[0] = column - marks_[size_t(best)].box[0];
    grab_[1] = row - marks_[size_t(best)].box[1];
    dragging_ = true;
    return true;
}

bool StudioMarks::pick(int which) {
    picked_ = -1;
    for (size_t i = 0; i < marks_.size(); ++i) {
        if (kind_ == Kind::Gates ? marks_[i].number == which : marks_[i].index == which) picked_ = int(i);
    }
    return picked_ >= 0;
}

bool StudioMarks::budge(Context& ctx, studio::Cooker& cooker, int columns, int rows, std::string* answer) {
    if (picked_ < 0) return *answer = "nothing picked", false;
    Mark& mark = marks_[size_t(picked_)];
    for (int k = 0; k < 4; k += 2) mark.box[k] += columns;
    for (int k = 1; k < 4; k += 2) mark.box[k] += rows;
    return write(ctx, cooker, answer);
}

bool StudioMarks::look(Context& ctx, studio::Cooker& cooker, int look, std::string* answer) {
    if (picked_ < 0 || kind_ != Kind::Townsfolk) return *answer = "no townsperson picked", false;
    if (look < 1 || look > 8) return *answer = "a look is 1 (West) clockwise to 8 (NorthWest)", false;
    marks_[size_t(picked_)].faces[0] = look;
    return write(ctx, cooker, answer);
}

bool StudioMarks::duplicate(Context& ctx, studio::Cooker& cooker, std::string* answer) {
    if (kind_ == Kind::Gates) return *answer = "a gate's number pairs it with another world's: not added here", false;
    if (picked_ < 0) return *answer = "no townsperson picked", false;
    core::Json row = manifest_["npcs"].at(size_t(marks_[size_t(picked_)].index));
    for (auto& [key, value] : row.members) {
        if (key == "at" && !value.items.empty()) value.items[0].number += 1.0;   // a tile east
    }
    const size_t at = manifest_["npcs"].size();
    const bool ok = cooker.edit(file_, "[\"npcs\", " + std::to_string(at) + "]", jsonText(row, false), answer,
                                "insert");
    if (ok) {
        load(ctx);
        pick(int(at));
        *answer = row["name"].stringOr("?") + " duplicated as npcs[" + std::to_string(at) + "]";
        cooker.forgetPlan();
    }
    said_ = *answer;
    saidOk_ = ok;
    return ok;
}

bool StudioMarks::addWay(Context& ctx, studio::Cooker& cooker, const std::string& to, int column, int row,
                         std::string* answer) {
    int number = 0;
    const bool ok = cooker.gate("\"how\": \"add\", \"world\": \"" + world_ + "\", \"to\": \"" + to +
                                    "\", \"column\": " + std::to_string(column) + ", \"row\": " + std::to_string(row),
                                answer, &number);
    if (ok) {
        load(ctx);
        pick(number);
        cooker.forgetPlan();
    }
    said_ = *answer;
    saidOk_ = ok;
    return ok;
}

bool StudioMarks::remove(Context& ctx, studio::Cooker& cooker, std::string* answer) {
    if (kind_ == Kind::Gates) {
        if (picked_ < 0) return *answer = "no gate picked", false;
        const bool ok = cooker.gate("\"how\": \"remove\", \"world\": \"" + world_ + "\", \"number\": " +
                                        std::to_string(marks_[size_t(picked_)].number),
                                    answer);
        if (ok) {
            picked_ = -1;
            load(ctx);
            cooker.forgetPlan();
        }
        said_ = *answer;
        saidOk_ = ok;
        return ok;
    }
    if (picked_ < 0) return *answer = "no townsperson picked", false;
    const int index = marks_[size_t(picked_)].index;
    const bool ok = cooker.edit(file_, "[\"npcs\", " + std::to_string(index) + "]", "", answer, "remove");
    if (ok) {
        picked_ = -1;
        load(ctx);
        *answer = "npcs[" + std::to_string(index) + "] deleted; the rows after it moved up one";
        cooker.forgetPlan();
    }
    said_ = *answer;
    saidOk_ = ok;
    return ok;
}

void StudioMarks::draw(Context& ctx, game::World& world, const gfx::Camera& camera) const {
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const content::Ground& ground = world.ground();
    const float m = ground.metresPerTile();
    auto at = [&](float column, float row, ImVec2* out) {
        const float p[3] = {column * m, ground.heightAt(column * m, -row * m) + 0.05f, -row * m};
        return toScreen(ctx, camera, p, &out->x, &out->y);
    };
    char label[160];
    for (size_t i = 0; i < marks_.size(); ++i) {
        const Mark& mark = marks_[i];
        const bool picked = int(i) == picked_;
        if (kind_ == Kind::Townsfolk) {
            ImVec2 c;
            if (!at(float(mark.box[0]) + 0.5f, float(mark.box[1]) + 0.5f, &c)) continue;
            list->AddCircleFilled(c, picked ? 7.0f : 5.0f, picked ? kPicked : kFolk);
            list->AddText(ImVec2(c.x + 8.0f, c.y - 8.0f), picked ? kPicked : kFolk, mark.name.c_str());
            continue;
        }
        // The box's edge, a point a tile, so it lies on the land; tiles x1..x2 are [x1, x2 + 1).
        const float x0 = float(std::min(mark.box[0], mark.box[2])), x1 = float(std::max(mark.box[0], mark.box[2]) + 1);
        const float y0 = float(std::min(mark.box[1], mark.box[3])), y1 = float(std::max(mark.box[1], mark.box[3]) + 1);
        std::vector<ImVec2> edge;
        bool seen = true;
        auto walk = [&](float ax, float ay, float bx, float by) {
            const int steps = std::max(1, int(std::max(std::fabs(bx - ax), std::fabs(by - ay))));
            for (int s = 0; s < steps && seen; ++s) {
                ImVec2 p;
                const float t = float(s) / float(steps);
                seen = at(ax + (bx - ax) * t, ay + (by - ay) * t, &p);
                edge.push_back(p);
            }
        };
        walk(x0, y0, x1, y0);
        walk(x1, y0, x1, y1);
        walk(x1, y1, x0, y1);
        walk(x0, y1, x0, y0);
        if (!seen) continue;
        const ImU32 colour = picked ? kPicked : (mark.list == "enters" ? kEnter : kExit);
        list->AddPolyline(edge.data(), int(edge.size()), colour, ImDrawFlags_Closed, picked ? 3.0f : 2.0f);
        ImVec2 c;
        if (!at(0.5f * (x0 + x1), 0.5f * (y0 + y1), &c)) continue;
        if (mark.list == "enters") {
            std::snprintf(label, sizeof(label), "%d > %d, level %d", mark.number, mark.target, mark.level);
        } else {
            std::snprintf(label, sizeof(label), "%d out", mark.number);
        }
        list->AddText(ImVec2(c.x - 20.0f, c.y - 7.0f), colour, label);
    }
}

void StudioMarks::panel(Context& ctx, game::World& world, studio::Cooker& cooker, const gfx::Camera& camera,
                        float x, float y, float w, float h) {
    const bool folk = kind_ == Kind::Townsfolk;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin(folk ? "Townsfolk" : "Gates", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushTextWrapPos(0.0f);
    std::string answer;
    if (released_) {
        released_ = false;
        write(ctx, cooker, &answer);
    }
    if (manifest_.isNull()) {
        ImGui::TextColored(kRed, "no %s", file_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    ImGui::TextColored(kDim, "%s of %s: click one on the land, drag it by tiles", folk ? "the townsfolk" : "the gates",
                       file_.c_str());
    ImGui::BeginChild("##marks", ImVec2(0.0f, 9.0f * ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders);
    char row[200];
    for (size_t i = 0; i < marks_.size(); ++i) {
        const Mark& mark = marks_[i];
        if (folk) {
            std::snprintf(row, sizeof(row), "%s  (%d, %d)##%zu", mark.name.c_str(), mark.box[0], mark.box[1], i);
        } else if (mark.list == "enters") {
            std::snprintf(row, sizeof(row), "enter %d > %d, level %d##%zu", mark.number, mark.target, mark.level, i);
        } else {
            std::snprintf(row, sizeof(row), "exit %d##%zu", mark.number, i);
        }
        if (ImGui::Selectable(row, int(i) == picked_)) picked_ = int(i), said_.clear();
    }
    ImGui::EndChild();

    if (picked_ >= 0) {
        Mark& mark = marks_[size_t(picked_)];
        bool done = false;
        if (folk) {
            ImGui::TextColored(kGold, "%s (npc %d), npcs[%d]", mark.name.c_str(), mark.number, mark.index);
            if (ImGui::DragInt2("column, row", mark.box, 0.1f)) mark.box[2] = mark.box[0], mark.box[3] = mark.box[1];
            done |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SliderInt("look", &mark.faces[0], 1, 8);
            done |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("1 West, clockwise, to 8 NorthWest");
        } else {
            ImGui::TextColored(kGold, "%s %d, gates.%s[%d]", mark.list == "enters" ? "enter" : "exit", mark.number,
                               mark.list.c_str(), mark.index);
            ImGui::DragInt4("box x1 y1 x2 y2", mark.box, 0.1f);
            done |= ImGui::IsItemDeactivatedAfterEdit();
            if (mark.list == "enters") {
                ImGui::DragInt("level", &mark.level, 1.0f, 0, 400);
                done |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::InputInt("target gate", &mark.target, 0);
                done |= ImGui::IsItemDeactivatedAfterEdit();
            } else {
                ImGui::SliderInt2("faces", mark.faces, -1, 1);
                done |= ImGui::IsItemDeactivatedAfterEdit();
            }
        }
        if (done) write(ctx, cooker, &answer);
        if (folk && picked_ >= 0) {
            if (ImGui::Button("Duplicate")) duplicate(ctx, cooker, &answer);
            ImGui::SetItemTooltip("a copy a tile east, added to the manifest");
            ImGui::SameLine();
            if (ImGui::Button("Delete")) remove(ctx, cooker, &answer);
            ImGui::SetItemTooltip("taken out of the manifest");
        }
        if (!folk && picked_ >= 0) {
            if (ImGui::Button("Delete with its pair")) remove(ctx, cooker, &answer);
            ImGui::SetItemTooltip("a way in takes its way out (unless another leads there); a way out takes "
                                  "every way in to it -- in both worlds' manifests");
        }
        if (picked_ >= 0 && ImGui::SmallButton("Drop the selection")) picked_ = -1;
    }
    if (!folk && !worlds_.empty()) {
        ImGui::Separator();
        leadsTo_ = std::min(leadsTo_, int(worlds_.size()) - 1);
        ImGui::SetNextItemWidth(-150.0f);
        if (ImGui::BeginCombo("##to", worlds_[size_t(leadsTo_)].c_str())) {
            for (size_t i = 0; i < worlds_.size(); ++i) {
                if (ImGui::Selectable(worlds_[i].c_str(), int(i) == leadsTo_)) leadsTo_ = int(i);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add a way there")) {
            const float m = world.ground().metresPerTile();
            addWay(ctx, cooker, worlds_[size_t(leadsTo_)], int(std::floor(camera.target[0] / m)),
                   int(std::floor(-camera.target[2] / m)), &answer);
        }
        ImGui::SetItemTooltip("a way in at the view's middle, level 0; its way out on that world's arrival tile, "
                              "to be moved there");
    }
    if (!said_.empty()) ImGui::TextColored(saidOk_ ? kGreen : kRed, "%s", said_.c_str());
    ImGui::TextColored(kDim, folk ? "the game stands them after `mu make tables <world>`"
                                  : "the game's gates are compiled: `mu make realm`, then a rebuild of the game and "
                                    "the server");
    ImGui::PopTextWrapPos();
    ImGui::End();
    draw(ctx, world, camera);
}

}  // namespace mu::app
