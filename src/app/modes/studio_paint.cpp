#include "app/modes/studio_paint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/context.h"
#include "app/modes/studio_place.h"
#include "core/json.h"
#include "core/paths.h"
#include "game/world/world.h"
#include "imgui.h"
#include "studio/cooker.h"

namespace mu::app {
namespace {

const ImVec4 kGreen(0.52f, 0.80f, 0.52f, 1.0f);
const ImVec4 kRed(0.92f, 0.42f, 0.38f, 1.0f);
const ImVec4 kDim(0.58f, 0.58f, 0.60f, 1.0f);

// A slot's tint over the land: twelve apart, then round again.
ImVec4 tint(int slot) {
    static const ImVec4 kTints[] = {
        {0.40f, 0.80f, 0.30f, 1}, {0.85f, 0.75f, 0.45f, 1}, {0.60f, 0.60f, 0.65f, 1}, {0.80f, 0.55f, 0.35f, 1},
        {0.90f, 0.40f, 0.60f, 1}, {0.30f, 0.60f, 0.95f, 1}, {0.65f, 0.40f, 0.20f, 1}, {0.45f, 0.45f, 0.45f, 1},
        {0.75f, 0.35f, 0.90f, 1}, {0.95f, 0.90f, 0.30f, 1}, {0.30f, 0.85f, 0.80f, 1}, {0.95f, 0.30f, 0.30f, 1}};
    return kTints[size_t(std::max(slot, 0)) % (sizeof(kTints) / sizeof(kTints[0]))];
}

}  // namespace

void StudioPaint::world(Context& ctx, const std::string& name) {
    if (name == world_) return;
    world_ = name;
    stroke_.clear();
    strokes_.clear();
    said_.clear();
    slotNames_.clear();
    const core::Json doc = core::parseJsonFile(core::folders().content + "/world/" + name + "/" + name + ".json");
    for (const auto& [key, value] : doc["tile_slots"].members) {
        const int slot = std::atoi(key.c_str());
        if (slot < 0 || slot > 255) continue;
        if (int(slotNames_.size()) <= slot) slotNames_.resize(size_t(slot) + 1);
        slotNames_[size_t(slot)] = value.stringOr("?");
    }
    slot_ = 0;
}

void StudioPaint::brush(int slot, bool overlay, int alpha, int radius) {
    slot_ = slot;
    overlay_ = overlay;
    alpha_ = std::clamp(alpha, 0, 255);
    radius_ = std::clamp(radius, 0, 8);
}

void StudioPaint::dab(int column, int row) {
    // tools/mu/paint.py's disc: dx² + dy² <= r² + r.
    for (int dy = -radius_; dy <= radius_; ++dy) {
        for (int dx = -radius_; dx <= radius_; ++dx) {
            if (dx * dx + dy * dy > radius_ * radius_ + radius_) continue;
            stroke_[{column + dx, row + dy}] = overlay_ ? Value{-1, slot_, alpha_} : Value{slot_, -1, 0};
        }
    }
}

bool StudioPaint::write(studio::Cooker& cooker, std::string* answer) {
    if (stroke_.empty()) return *answer = "nothing painted", false;
    std::string tiles;
    auto channel = [](int v) { return v < 0 ? std::string("null") : std::to_string(v); };
    for (const auto& [tile, value] : stroke_) {
        tiles += (tiles.empty() ? "[" : ", [") + std::to_string(tile.first) + ", " + std::to_string(tile.second) +
                 ", " + channel(value[0]) + ", " + channel(value[1]) + ", " + channel(value[2]) + "]";
    }
    const bool ok = cooker.paint("\"world\": \"" + world_ + "\", \"tiles\": [" + tiles + "]", answer);
    if (ok) {
        strokes_.push_back(stroke_);
        cooker.forgetPlan();
    }
    stroke_.clear();
    said_ = *answer;
    saidOk_ = ok;
    return ok;
}

bool StudioPaint::stroke(studio::Cooker& cooker, int column, int row, std::string* answer) {
    stroke_.clear();
    dab(column, row);
    return write(cooker, answer);
}

bool StudioPaint::undo(studio::Cooker& cooker, std::string* answer) {
    const bool ok = cooker.paint("\"how\": \"undo\", \"world\": \"" + world_ + "\"", answer);
    if (ok && !strokes_.empty()) strokes_.pop_back();
    if (ok) cooker.forgetPlan();
    said_ = *answer;
    saidOk_ = ok;
    return ok;
}

bool StudioPaint::pointer(Context& ctx, game::World& world, const gfx::Camera& camera, bool overPanels) {
    if (!world.town().isOpen()) return false;
    const float m = world.ground().metresPerTile();
    float g[3];
    hover_[0] = hover_[1] = -1;
    if (!overPanels && groundUnderPointer(ctx, world, camera, g)) {
        hover_[0] = int(std::floor(g[0] / m));
        hover_[1] = int(std::floor(-g[2] / m));
    }
    if (painting_) {
        if (ctx.window.held(0)) {
            if (hover_[0] >= 0) dab(hover_[0], hover_[1]);
            return true;
        }
        painting_ = false;
        released_ = true;   // written by the panel, which holds the cooker
        return true;
    }
    if (overPanels || !ctx.window.clicked(0) || hover_[0] < 0 || slotNames_.empty()) return false;
    painting_ = true;
    stroke_.clear();
    dab(hover_[0], hover_[1]);
    return true;
}

void StudioPaint::draw(Context& ctx, game::World& world, const gfx::Camera& camera) const {
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const content::Ground& ground = world.ground();
    const float m = ground.metresPerTile();
    auto at = [&](float column, float row, ImVec2* out) {
        const float p[3] = {column * m, ground.heightAt(column * m, -row * m) + 0.04f, -row * m};
        return toScreen(ctx, camera, p, &out->x, &out->y);
    };
    // Every written stroke, the later over the earlier, then the one being dragged.
    std::map<Tile, Value> shown;
    for (const auto& one : strokes_) {
        for (const auto& [tile, value] : one) shown[tile] = value;
    }
    for (const auto& [tile, value] : stroke_) shown[tile] = value;
    for (const auto& [tile, value] : shown) {
        ImVec2 q[4];
        const float c = float(tile.first), r = float(tile.second);
        if (!at(c, r, &q[0]) || !at(c + 1, r, &q[1]) || !at(c + 1, r + 1, &q[2]) || !at(c, r + 1, &q[3])) continue;
        const bool over = value[0] < 0;
        ImVec4 colour = tint(over ? value[1] : value[0]);
        colour.w = over ? 0.25f + 0.45f * float(value[2]) / 255.0f : 0.6f;
        list->AddQuadFilled(q[0], q[1], q[2], q[3], ImGui::ColorConvertFloat4ToU32(colour));
    }
    // The brush: its tiles' outline round the tile under the pointer.
    if (hover_[0] >= 0) {
        const float rad = float(radius_) + 0.5f;
        ImVec2 ring[32];
        bool seen = true;
        for (int i = 0; i < 32 && seen; ++i) {
            const float a = float(i) / 32.0f * 6.2831853f;
            seen = at(float(hover_[0]) + 0.5f + rad * std::cos(a), float(hover_[1]) + 0.5f + rad * std::sin(a), &ring[i]);
        }
        if (seen) list->AddPolyline(ring, 32, IM_COL32(255, 255, 255, 220), ImDrawFlags_Closed, 2.0f);
    }
}

void StudioPaint::panel(Context& ctx, game::World& world, studio::Cooker& cooker, const gfx::Camera& camera,
                        float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Ground", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushTextWrapPos(0.0f);
    std::string answer;
    if (released_) {
        released_ = false;
        write(cooker, &answer);
    }
    if (slotNames_.empty()) {
        ImGui::TextColored(kRed, "content/world/%s/%s.json names no tile_slots: nothing to paint with", world_.c_str(),
                           world_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    ImGui::TextColored(kDim, "content/world/%s/tiles.png: a left drag on the land paints, a right drag turns the "
                             "camera", world_.c_str());
    for (size_t i = 0; i < slotNames_.size(); ++i) {
        if (slotNames_[i].empty()) continue;
        ImGui::ColorButton(("##tint" + std::to_string(i)).c_str(), tint(int(i)),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder);
        ImGui::SameLine();
        char label[96];
        std::snprintf(label, sizeof(label), "%zu  %s", i, slotNames_[i].c_str());
        if (ImGui::RadioButton(label, slot_ == int(i))) slot_ = int(i);
    }
    ImGui::Separator();
    if (ImGui::RadioButton("Base", !overlay_)) overlay_ = false;
    ImGui::SetItemTooltip("the tile's floor (tiles.png red); its overlay cleared");
    ImGui::SameLine();
    if (ImGui::RadioButton("Overlay", overlay_)) overlay_ = true;
    ImGui::SetItemTooltip("painted over the floor (green) as far as the strength (blue)");
    ImGui::BeginDisabled(!overlay_);
    ImGui::SliderInt("strength", &alpha_, 0, 255);
    ImGui::EndDisabled();
    ImGui::SliderInt("radius (tiles)", &radius_, 0, 8);
    if (hover_[0] >= 0) {
        const content::Ground& ground = world.ground();
        const int floor = ground.floorAt(hover_[0], hover_[1]), over = ground.overlayAt(hover_[0], hover_[1]);
        auto name = [&](int s) {
            return s >= 0 && s < int(slotNames_.size()) && !slotNames_[size_t(s)].empty() ? slotNames_[size_t(s)]
                                                                                           : std::string("-");
        };
        ImGui::TextColored(kDim, "(%d, %d) as cooked: %s, overlay %s at %.0f%%", hover_[0], hover_[1],
                           name(floor).c_str(), name(over).c_str(), 100.0f * ground.blendAt(hover_[0], hover_[1]));
    }
    ImGui::BeginDisabled(strokes_.empty());
    if (ImGui::Button("Undo the last stroke")) undo(cooker, &answer);
    ImGui::EndDisabled();
    if (!said_.empty()) ImGui::TextColored(saidOk_ ? kGreen : kRed, "%s", said_.c_str());
    ImGui::TextColored(kDim, "tinted until `mu make ground %s` cooks the land again (the tile sheets, the ground, "
                             "the index, the world's textures and placements, every world's tables)",
                       world_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
    draw(ctx, world, camera);
}

}  // namespace mu::app
