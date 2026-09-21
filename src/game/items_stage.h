// The item models, photographed into a window's picture: the Stage for the bag and the shelf.
//
// MU2's Panel.Stage and Panel.Fit, and MU's RenderObjectScreen before them. Each window gets
// one of these, the window's own size, and every item stands on it at its cell with an
// orthographic camera, so one pass draws every picture and they line up with the cells.
//
// What is kept of Panel.Fit, and what is not:
//   * face on: a model's longest side up, its middle side across, its thinnest into the
//     screen -- a permutation of its axes, so a shield stays square to its cell;
//   * scaled against each of the box's sides in turn, at MU2's 0.82;
//   * a worn piece (index.json's "armor") is drawn as it is posed, in its bind pose, not
//     turned -- turning two gloves a shoulder apart puts them side by side edge on;
//   * the one under the pointer turns about the vertical at MU's `WorldTime * 0.45`, from a
//     rest yaw of eight degrees.
// Not kept: MU2's Bulge (which way along the thin axis faces the viewer) and Upended (a
// quiver stood on its other end). Both are marked in the sprint file as owed.
#pragma once

#include <cstdint>
#include <vector>

#include <bgfx/bgfx.h>

#include "game/item_models.h"
#include "game/stage.h"
#include "gfx/renderer.h"

namespace mu::game {

class ItemStage : public Stage {
public:
    // `models` is shared with the other stage and the drops on the ground, and outlives this.
    void open(ItemModels* models, bgfx::ViewId view) {
        models_ = models;
        view_ = view;
    }
    void shutdown();

    void stand(const std::vector<Standing>& items, float unitsW, float unitsH) override;
    gfx::Art picture() const override { return picture_; }

    // Photographs what stands on it, if it changed or something on it turns. Called once a
    // frame, before the renderer's frame is submitted; `pixelsPerUnit` is the window's own
    // scale, so the picture is the window's size on screen and nothing is resampled.
    void render(gfx::Renderer& renderer, float pixelsPerUnit, double seconds);
    uint64_t renders() const { return renders_; }

private:
    void resize(int width, int height);

    ItemModels* models_ = nullptr;
    bgfx::ViewId view_ = 0;
    std::vector<Standing> standing_;
    std::vector<gfx::Drawable> drawables_;
    float unitsW_ = 0.0f, unitsH_ = 0.0f;
    bool dirty_ = true;
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    int width_ = 0, height_ = 0;
    gfx::Art picture_;
    double clock_ = 0.0;
    uint64_t renders_ = 0;
};

}  // namespace mu::game
