#include "game/ui/outline.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <bx/math.h>

#include "content/mesh.h"

namespace mu::game {
namespace {

// `p * M`, the row-vector convention every matrix in this project is written in -- the
// translation is row 3, not column 3. core::mulMatrix is the same rule for two matrices.
void transformPoint(const float* p, const float* m, float* out) {
    for (int j = 0; j < 3; ++j) {
        out[j] = p[0] * m[0 * 4 + j] + p[1] * m[1 * 4 + j] + p[2] * m[2 * 4 + j] + m[3 * 4 + j];
    }
}

// A world point through the SAME view*proj the frame drew with, into a pixel of that
// picture. Returns false for a point behind the eye, where the division would fold the
// point back in front of it and hand back a pixel nowhere near the truth -- Godot's
// Camera3D.IsPositionBehind, which Outline.Bounds guards against for the same reason.
bool toPixel(const float* world, const float* viewProj, int width, int height, float* px,
            float* py) {
    float clip[4];
    for (int j = 0; j < 4; ++j) {
        clip[j] = world[0] * viewProj[0 * 4 + j] + world[1] * viewProj[1 * 4 + j] +
                 world[2] * viewProj[2 * 4 + j] + viewProj[3 * 4 + j];
    }
    if (clip[3] <= 1e-4f) return false;
    const float ndcX = clip[0] / clip[3];
    const float ndcY = clip[1] / clip[3];
    *px = (ndcX * 0.5f + 0.5f) * float(width);
    *py = (0.5f - ndcY * 0.5f) * float(height);
    return true;
}

}  // namespace

void Outline::show(gfx::Renderer& renderer, const gfx::Camera& camera, const float* view,
                   const float* proj, int width, int height,
                   const std::vector<gfx::Drawable>& hovered, bool shadow) {
    if (hovered.empty() || width <= 0 || height <= 0) return;
    gfx::Renderer::OutlineParams params;

    float viewProj[16];
    bx::mtxMul(viewProj, view, proj);

    // The hull of every corner of every piece's own box, in screen pixels -- not just two
    // corners: MU's camera looks down at an angle, so a box's screen rectangle is the hull
    // of all eight, and taking two clips the model at the top or the bottom. One corner
    // behind the eye and the whole thing is skipped rather than guessed at, the same call
    // Outline.Bounds makes.
    float least[2] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    float most[2] = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    bool found = false;
    for (const gfx::Drawable& d : hovered) {
        if (!d.mesh) continue;
        const content::Bounds& b = d.mesh->bounds();
        for (int corner = 0; corner < 8; ++corner) {
            const float local[3] = {
                (corner & 1) ? b.max[0] : b.min[0],
                (corner & 2) ? b.max[1] : b.min[1],
                (corner & 4) ? b.max[2] : b.min[2],
            };
            float world[3];
            transformPoint(local, d.transform, world);
            float px, py;
            if (!toPixel(world, viewProj, width, height, &px, &py)) {
                found = false;
                goto done;
            }
            least[0] = std::min(least[0], px);
            least[1] = std::min(least[1], py);
            most[0] = std::max(most[0], px);
            most[1] = std::max(most[1], py);
            found = true;
        }
    }
done:
    if (!found) return;

    // Room for the ring itself, and the shadow's further reach where one is asked for --
    // Outline.Bounds' own margin, one pixel over so the stroke is never clipped by the box
    // that is meant to hold it.
    const float margin =
        (shadow ? std::max(gfx::Renderer::kOutlineWidth, gfx::Renderer::kOutlineReach)
                : gfx::Renderer::kOutlineWidth) +
        2.0f;
    const int x0 = std::clamp(int(std::floor(least[0] - margin)), 0, width);
    const int y0 = std::clamp(int(std::floor(least[1] - margin)), 0, height);
    const int x1 = std::clamp(int(std::ceil(most[0] + margin)), 0, width);
    const int y1 = std::clamp(int(std::ceil(most[1] + margin)), 0, height);
    if (x1 <= x0 || y1 <= y0) return;

    params.screenX = x0;
    params.screenY = y0;
    params.screenW = x1 - x0;
    params.screenH = y1 - y0;
    params.shadow = shadow;
    renderer.drawOutline(view, camera, params, hovered);
}

}  // namespace mu::game
