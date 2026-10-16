// The target frame's portrait: his head and the helm he wears, photographed as WoW's target frame
// shows a face (the user, 2026-10-10: "we want to see char helm with head on thumb like its on
// WoW"; the proposal, claude.ai/artifact/PV9VmdLkN4mqJcoW2tQdzm).
//
// Not a figure of its own: the world's own drawables for him, gathered as the hover ring gathers
// its body (Play::gather, the same transforms and palette row), and drawn again on a small stage
// through a camera stood close in front of his head joint. **A still** (the user, 2026-10-10:
// "charaacter thumb has to be static and zoom on head"): taken once when he is picked, and again
// only when what he wears changes -- a new helm is a new face. A frame he is not drawn on --
// another map, out of the camera's reach, lying dead -- keeps the last picture. Without the
// +7 to +11 shine on what he wears (the user, 2026-10-10: "dont use +11,+9 effect on character
// thumb"): the face reads plain.
#pragma once

#include <bgfx/bgfx.h>

#include <vector>

#include "gfx/interface.h"
#include "gfx/renderer.h"

namespace mu::game {

class Portrait {
public:
    // A face, as the target frame and the party's capsules take one; or `figure`, the whole of him
    // head to foot and his wings as the inspect window shows him (game/ui/inspect.h), on a tall
    // picture. Both without the shine of his plus, which washes a +11 set into one green-gold
    // sheet; the plus is on each slot.
    explicit Portrait(bool figure = false) : figure_(figure) {}
    void shutdown();

    // Photographs `drawables` (his, this frame) from in front of `head`, his head joint in the
    // world's metres, along `yaw`, his facing; `scale` his height against a man's 1.8 m. After Play::gather, which
    // is what put his palette in, and before the scene's draw uploads it.
    void render(gfx::Renderer& renderer, const std::vector<gfx::Drawable>& drawables, const float head[3],
                float yaw, float scale);
    // A new target: the picture is no one's until he is next photographed.
    void forget() {
        taken_ = false;
        worn_.clear();
    }
    // Whether `drawables` are what the picture was taken of: the same meshes, so nothing to take.
    bool same(const std::vector<gfx::Drawable>& drawables) const;

    const gfx::Art& picture() const { return picture_; }
    // Whether the picture is his: taken since the last forget().
    bool taken() const { return taken_ && bgfx::isValid(target_); }

private:
    std::vector<gfx::Drawable> plain_;  // his drawables with the refinement's shine taken off
    std::vector<const void*> worn_;     // the meshes the picture was taken of
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    gfx::Art picture_;
    bool taken_ = false;
    bool figure_ = false;
};

}  // namespace mu::game
