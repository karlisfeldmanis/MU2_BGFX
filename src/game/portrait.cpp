#include "game/portrait.h"

#include <bx/math.h>

#include <cmath>

#include "gfx/views.h"

namespace mu::game {
namespace {

// The picture's side, pixels: drawn at about 72 on a 1080-line screen, so twice over for 4K.
constexpr int kSide = 128;
// The camera: a little to his right of straight on (a face square to the lens reads as a mugshot,
// the proposal's renders turned it), a touch above the joint, and close with a narrow lens, so the
// head and helm fill the disc (the user: "zoom on head"). Metres, for a man of 1.8 m.
constexpr float kTurnDegrees = 22.0f;
constexpr float kAway = 1.0f;
constexpr float kLift = 0.06f;
constexpr float kLookUp = 0.08f;  // where the lens points, over the joint: the face, not the neck
constexpr float kFovDegrees = 18.0f;

}  // namespace

void Portrait::shutdown() {
    if (bgfx::isValid(target_)) bgfx::destroy(target_);
    target_ = BGFX_INVALID_HANDLE;
    picture_ = gfx::Art{};
    taken_ = false;
}

bool Portrait::same(const std::vector<gfx::Drawable>& drawables) const {
    if (!taken_ || drawables.size() != worn_.size()) return false;
    for (size_t i = 0; i < drawables.size(); ++i) {
        if (drawables[i].mesh != worn_[i]) return false;
    }
    return true;
}

void Portrait::render(gfx::Renderer& renderer, const std::vector<gfx::Drawable>& drawables,
                      const float head[3], float yaw, float scale) {
    if (drawables.empty()) return;
    if (!bgfx::isValid(target_)) {
        const bgfx::TextureHandle colour = bgfx::createTexture2D(
            kSide, kSide, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT_MSAA_X4 | BGFX_SAMPLER_UVW_CLAMP);
        const bgfx::TextureHandle depth = bgfx::createTexture2D(
            kSide, kSide, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY | BGFX_TEXTURE_RT_MSAA_X4);
        const bgfx::TextureHandle both[2] = {colour, depth};
        target_ = bgfx::createFrameBuffer(2, both, true);
        picture_.handle = colour;
        picture_.width = float(kSide);
        picture_.height = float(kSide);
    }
    // Before him along his facing, turned a little: a figure at yaw 0 faces +z, as the bust's
    // does toward its camera (game/bust.cpp).
    const float turn = yaw + kTurnDegrees * bx::kPi / 180.0f;
    const float s = scale > 0.2f && scale < 5.0f ? scale : 1.0f;
    const bx::Vec3 at(head[0], head[1] + kLookUp * s, head[2]);
    const bx::Vec3 eye(head[0] + std::sin(turn) * kAway * s, head[1] + (kLookUp + kLift) * s,
                       head[2] + std::cos(turn) * kAway * s);
    float view[16], proj[16];
    bx::mtxLookAt(view, eye, at, bx::Vec3(0.0f, 1.0f, 0.0f), bx::Handedness::Right);
    bx::mtxProj(proj, kFovDegrees, 1.0f, 0.05f, 20.0f, bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    plain_.assign(drawables.begin(), drawables.end());
    for (gfx::Drawable& d : plain_) d.refine = 0;
    renderer.drawStage(gfx::ViewStagePortrait, target_, kSide, kSide, view, proj, plain_);
    worn_.clear();
    for (const gfx::Drawable& d : drawables) worn_.push_back(d.mesh);
    taken_ = true;
}

}  // namespace mu::game
