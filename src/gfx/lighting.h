// The lighting sheet. Every knob the frame's look hangs on, in one json that is re-read
// whenever it changes on disk, so a bench can be tuned with the window open. MU2 kept the
// same file for the same reason; this is its own, in this engine's terms.
#pragma once

#include <string>

namespace mu::gfx {

// Everything here is in metres, which is MU2's own scene scale: one tile, one metre.
// docs/conventions.md.
struct Lighting {
    // The sun. `azimuth` and `elevation` are degrees; the direction is worked out from them
    // so a sheet can be written by eye rather than as a vector.
    //
    // **These two are MU2's and must stay so.** MU2/lighting.json says `sun_pitch: -52` and
    // `sun_yaw: 90`. A Godot DirectionalLight3D shines down its local -z under a YXZ basis,
    // so that light travels along (-0.616, -0.788, 0) and the direction towards the sun is
    // (0.616, 0.788, 0): elevation 52 and, in this engine's terms, azimuth 0. The rest of
    // the look is matched by eye in sprint 8; the angle is arithmetic and is settled here.
    float azimuth = 0.0f;
    float elevation = 52.0f;
    float sunColour[3] = {1.00f, 0.95f, 0.88f};  // MU2's sun_color
    // Not MU2's sun_energy (1.7): that is Godot's own energy unit against Godot's own
    // diffuse, and this pass divides by pi where Godot does not. Matched by eye, not by
    // copying the number.
    float sunStrength = 3.0f;

    float skyColour[3] = {0.385f, 0.454f, 0.55f};    // MU2's sky_top
    float horizonPaleness = 0.7f;
    float groundColour[3] = {0.20f, 0.169f, 0.133f}; // MU2's sky_ground_bottom
    float ambientStrength = 0.7f;                    // MU2's ambient_energy

    float exposure = 1.25f;  // MU2's exposure

    // The shadow. `range` is how wide the split is, in metres. MU2 frames one split on the
    // camera's ground point at about this reach; see its shadow_reach.
    float shadowRange = 60.0f;
    // Not MU2's shadow_bias (0.01): Godot multiplies that by the cascade range, the blur and
    // the filter radius before it reaches the depth test, so the number means nothing here.
    float shadowBias = 0.0015f;
    float shadowNormalBias = 0.02f;
    // The sun is four degrees across rather than its real half degree, or the penumbra is a
    // line. Invention, carried from MU4, and the number a bench argues about.
    float sunAngleDegrees = 4.0f;

    float ssaoRadius = 0.5f;
    float ssaoStrength = 1.0f;

    // Re-reads `path` when its timestamp has moved. True when something changed, so the
    // caller can log it. The first call always reads.
    bool reloadIfChanged(const std::string& path);

    // The unit vector towards the sun, from the two angles.
    void sunDirection(float out[3]) const;

private:
    int64_t modified_ = 0;
    bool everRead_ = false;
};

}  // namespace mu::gfx
