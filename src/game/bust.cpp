#include "game/bust.h"

#include <bx/math.h>

#include <cmath>

#include "core/log.h"
#include "gfx/views.h"

namespace mu::game {
namespace {

struct Pose {
    const char* model;
    float turnDegrees;  // MU's angle[2]
    float scale;
    float nudge;        // MU units aside
    float lift = 0.0f;  // MU units up, positionOffsetZ
};

// GetRenderParameters, CharMakeWin.cpp:97. MU's angles are for the IDLE, action 0: the
// greeting, action 1, is a turn of the head to one side, and judged on a greeting frame every one
// of these looked wrong -- which is how a quarter turn and a -70 for the elf were once added and
// taken out again. Judge a bust a few seconds after choosing its class. The elf's eight-degree
// tilt (angle[0]) is not carried: a figure is drawn upright (Figure::gather), and eight degrees
// on a bust cropped at the shoulders is a nod nobody would miss.
Pose poseOf(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return {"NewFace01", -40.0f, 5.9f, 0.0f};
        case sim::Kin::FairyElf: return {"NewFace03", 5.0f, 9.1f, 4.8f};
        case sim::Kin::DarkKnight: return {"NewFace02", -12.0f, 6.05f, 0.0f};
        // CLASS_DARK's, and its lift (CharMakeWin.cpp:107).
        case sim::Kin::MagicGladiator: return {"NewFace04", -13.0f, 6.0f, 0.0f, 1.8f};
    }
    return {"NewFace02", -12.0f, 6.05f, 0.0f};
}

// MU's camera, (10, -500, 48) units off the model and level, in metres on our axes: aside,
// up, and toward the viewer. MU2's BustEye.
constexpr float kEye[3] = {0.10f, 0.48f, 5.00f};
constexpr float kFovDegrees = 10.0f;

}  // namespace

void Bust::shutdown() {
    if (bgfx::isValid(target_)) bgfx::destroy(target_);
    target_ = BGFX_INVALID_HANDLE;
    picture_ = gfx::Art{};
    width_ = height_ = 0;
    figure_ = Figure{};
    standing_ = shown_ = false;
}

void Bust::show(sim::Kin kin) {
    shown_ = true;
    if (standing_ && kin == kin_) return;
    kin_ = kin;
    standing_ = true;
    const Pose pose = poseOf(kin);
    const FigureBody* body = figures_ ? figures_->body(pose.model) : nullptr;
    figure_ = Figure{};
    if (!body) {
        core::logError("lobby: no bust %s in the cooked figures (tools/cook_one.py %s)",
                       pose.model, pose.model);
        return;
    }
    // Facing the camera, which stands down +z from it, turned by MU's own angle.
    const float at[3] = {pose.nudge / 100.0f, pose.lift / 100.0f, 0.0f};
    figure_.stand(body, at, pose.turnDegrees * bx::kPi / 180.0f, body->scale * pose.scale, false);
    idle_ = body->library ? body->library->find(0) : -1;
    greeting_ = body->library ? body->library->find(1) : -1;
    // SelectCreateCharacter: the greeting, played through once and held on its last key. MU
    // loops the idle after it (UpdateCreateCharacter); ours stays where the greeting ends (the
    // user, 2026-10-07: "it has to remain on last motion without relooping"). The idle only
    // stands a bust that has no greeting.
    if (greeting_ >= 0) {
        figure_.play(greeting_, true, 0.0f, true);
    } else if (idle_ >= 0) {
        figure_.play(idle_, true, 0.0f);
    }
}

void Bust::resize(int width, int height) {
    if (width == width_ && height == height_ && bgfx::isValid(target_)) return;
    if (bgfx::isValid(target_)) bgfx::destroy(target_);
    width_ = width;
    height_ = height;
    const bgfx::TextureHandle colour =
        bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
                              bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_RT_MSAA_X4 | BGFX_SAMPLER_UVW_CLAMP);
    const bgfx::TextureHandle depth = bgfx::createTexture2D(
        uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::D24S8,
        BGFX_TEXTURE_RT_WRITE_ONLY | BGFX_TEXTURE_RT_MSAA_X4);
    const bgfx::TextureHandle both[2] = {colour, depth};
    target_ = bgfx::createFrameBuffer(2, both, true);
    picture_.handle = colour;
    picture_.width = float(width);
    picture_.height = float(height);
}

void Bust::render(gfx::Renderer& renderer, float seconds, int width, int height) {
    if (!shown() || width < 8 || height < 8) return;
    figure_.update(seconds);
    resize(width, height);

    const size_t need = figure_.body()->boneCount() * 12;
    if (scratch_.size() < need) scratch_.resize(need);
    const int bones = figure_.pose(scratch_.data());
    const int row = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
    drawables_.clear();
    figure_.gather(row, drawables_);

    float view[16], proj[16];
    bx::mtxLookAt(view, bx::Vec3(kEye[0], kEye[1], kEye[2]), bx::Vec3(kEye[0], kEye[1], 0.0f),
                  bx::Vec3(0.0f, 1.0f, 0.0f), bx::Handedness::Right);
    bx::mtxProj(proj, kFovDegrees, float(width) / float(height), 0.05f, 50.0f,
                bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    renderer.drawStage(gfx::ViewStageBust, target_, uint16_t(width), uint16_t(height), view, proj,
                       drawables_);
}

}  // namespace mu::game
