// The lighting sheet. Every knob the frame's look hangs on, in one json that is re-read
// whenever it changes on disk, so a bench can be tuned with the window open. MU2 kept the
// same file for the same reason; this is its own, in this engine's terms.
#pragma once

#include <string>

namespace mu::core {
class Json;
}

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

    // The dust in the air. Between the eye and a surface hangs sunlit dust of this colour,
    // lit by the same sun and sky as the ground, so it lifts a shadow towards pale sand
    // rather than greying the frame: the haze adds light, it never takes the sun away.
    // `dustDensity` is per metre, so MU's camera, some 20 m from the street, sees about
    // 1 - exp(-20 density) of it. 0 is clear air. Invention: MU has no fog in Lorencia.
    float dustColour[3] = {0.80f, 0.68f, 0.52f};
    float dustDensity = 0.0f;

    // The shadow. `range` is how wide the split is, in metres. MU2 frames one split on the
    // camera's ground point at about this reach; see its shadow_reach.
    float shadowRange = 60.0f;
    // Fit the split to what the camera can see instead of a fixed square on its target. The
    // number is how far below the camera's target the lowest visible ground may lie: every
    // visible surface is between the eye and where the frustum meets that plane, so that
    // hull, seen from the sun, is all the split has to cover. 0 keeps the fixed square above,
    // which is centred on the target and spends half its texels behind the camera.
    // docs/shadow-probe.md measured it. `shadowRange` stays the depth the sun looks back.
    float shadowFitBelow = 0.0f;
    // Both biases are metres, which is the only unit either of them means anything in.
    // An NDC bias hides its own size: 0.0015 over a 120 m split is 18 cm of peter-panning
    // on a D16 map whose quantum is under 2 mm. Not MU2's shadow_bias (0.01) either, since
    // Godot multiplies that by the cascade range, the blur and the filter radius first.
    float shadowBiasMetres = 0.02f;
    float shadowNormalBias = 0.02f;
    // The sun is four degrees across rather than its real half degree, or the penumbra is a
    // line. Invention, carried from MU4, and the number a bench argues about.
    float sunAngleDegrees = 4.0f;

    // The lamps, sprint 8a. `lampStrength` turns MU's luminance into this pass's radiance.
    // Pi is MU's own weight: MU adds a lamp's L straight into the light that multiplies the
    // albedo, and this pass divides the diffuse by pi, so at the middle of a pool a lamp of
    // L = 1 then adds what MU's arithmetic adds -- the light a flat surface takes from the sun
    // at sun_strength pi. `glowStrength` multiplies every BlendMesh drawn in the transparent
    // pass. Both are judged by eye; docs/sprints/08a-the-lamps.md.
    float lampStrength = 3.14159265f;
    float glowStrength = 1.0f;

    // Bloom, sprint 8b: what in the linear HDR frame spills light into the air round it.
    // `threshold` is scene radiance before the exposure, and it sits above what the sun makes of
    // a white wall, so by day it is the flames, the glows and the lamps that bloom and not the
    // town. `knee` softens the edge of the threshold; `strength` is how much of the chain is
    // added back. Invention: MU has no bloom. Judged by eye.
    float bloomThreshold = 1.2f;
    float bloomKnee = 0.6f;
    float bloomStrength = 0.25f;
    // How bright a flame at full heat is, in HDR, before the exposure: fs_flame's ramp. Invention.
    float flameStrength = 6.0f;

    // The present's own two, sprint 8's grade. `sharpen` 0..1 is a contrast-adaptive sharpen
    // (AMD's CAS, in its simple form) on the tonemapped frame: it lifts MU's painted detail
    // where the neighbourhood is flat and backs off at edges that are already hard, so it
    // does not ring. `contrast` 0..1 bends the midtones along a smoothstep, black and white
    // held. Both invention, judged by eye.
    float sharpen = 0.0f;
    float contrast = 0.0f;
    // The tone curve and the grade after it. `tonemap`: 0 Narkowicz's ACES, 1 Hill's ACES, 2
    // AgX, 3 AgX punchy, 4 Khronos Neutral. `saturation` 1 is as rendered. `split` 0..1 moves
    // the shade towards `tintLow` and the light towards `tintHigh`. Invention, judged by eye.
    float tonemap = 0.0f;
    float saturation = 1.0f;
    float split = 0.0f;
    float tintLow[3] = {0.92f, 0.98f, 1.08f};
    float tintHigh[3] = {1.08f, 1.0f, 0.9f};

    float ssaoRadius = 0.5f;
    float ssaoStrength = 1.0f;

    // The grass, docs/grass.md. MU's own painted tufts, cut out and scattered in a disc round
    // the camera; past it the ground texture takes over. These are the disc's knobs. They live
    // in the lighting sheet rather than in one of their own because a field of grass is look,
    // and this is the sheet the look is tuned on with the window open -- which is the only way
    // a green ever gets judged.
    //
    // `grass` 0 grows nothing at all, which is how the field's cost is priced: one run with it
    // and one without, wall frame against wall frame, since the per-view GPU timers cannot
    // price a pass on Metal. docs/budget.md.
    float grass = 1.0f;
    float grassRadius = 11.0f;   // how far out cards stand, metres, from the camera's focus
    float grassFade = 2.5f;      // the last metres of that, where a card shrinks into the turf
    float grassDensity = 1.0f;   // 0..1 of the 49 cards a square metre a patch may keep
    float grassHeight = 0.20f;   // metres of SWARD, before a card's own draws and the rank ones
    float grassAspect = 1.15f;   // a card's width as a multiple of its height; MU's tufts are wide
    // How far a still card lies over, as a fraction of its height.
    //
    // Small. Grass stands UP, and a blade that has lain over is the exception rather than the
    // rule -- this was pitched at 0.42, which with the stiffness spread on it put every card
    // between a third and half of its length sideways, and a field of those reads as a field
    // that has been walked flat. MU's own client leans its one quad half a tile, but MU has one
    // quad a TILE standing on the tile's edge and nothing else; a scattered field of leaning
    // cards is not the same picture and does not want the same number.
    //
    // 0.12, times the stiffness spread (0.74 to 1.30), puts a still card between 9 and 16 per
    // cent of its height sideways: upright, with enough difference between neighbours to see.
    float grassLean = 0.12f;
    // The share of cards that grow rank: tufts near twice the sward's height and narrower with
    // it. One in twelve. It is a step and not a spread on purpose -- a sward whose tops all
    // land near one height reads as mown however much jitter is on it, and what makes a field
    // a field is the few that stand through it.
    float grassRank = 0.085f;
    // How far a dry tuft goes towards straw, 0 keeping the whole field one green. The dryness
    // runs on a four-and-a-half-metre clump field, so it arrives in patches, not per card.
    float grassDry = 0.40f;
    // How far a card may be widened at the far edge of the disc. The thinning with distance and
    // this widening are one mechanism: coverage is held while the card count falls, and a
    // painted blade is kept over a pixel wide where it would otherwise crawl. docs/grass.md.
    float grassWiden = 1.3f;
    // What MU's painted sheet is MULTIPLIED by, root and top. Not a colour: the sheet is the
    // art and carries its own green. This is the gradient laid over it -- cool and dark at the
    // root where no light reaches, pale and warm at the top -- which is what stops one painted
    // tuft used ten thousand times from reading as one painted tuft used ten thousand times.
    // They sit NEAR ONE on purpose. The first pass had the root at 0.55 and an AO floor of
    // 0.30, which together with MU's baked light took the root of every tuft to under a fifth
    // of the turf it grows out of -- the field read as black blotches on the grass rather than
    // as grass. A modulation of painted art is a nudge; a colour is not.
    float grassRootTint[3] = {0.82f, 0.86f, 0.90f};
    float grassTipTint[3] = {1.12f, 1.10f, 0.92f};
    float grassRootAo = 0.62f;      // the height ramp's floor: how dark the root sits
    float grassRoughness = 0.45f;   // at the top; the root is rougher by a fixed 0.35
    // And the wind on top of the lean, not instead of it. At 0.22 a gust moved a card
    // further than its own habit ever did, which put the whole field on its side twice a
    // second. It is a sway now, not a flattening.
    float grassWindStrength = 0.10f;
    float grassWindDegrees = 45.0f;  // which way it blows, turning from +x towards -z

    // The reflection probe, sprint 8c: a cube round the player that metal and water reflect.
    // `probe` 0 puts the closed-form sky back, which is how its cost is priced. `probeView` N
    // draws the probe's mip N - 1 itself where the town is, the check that its faces are the right way
    // round. docs/sprints/08c-the-metal.md.
    float probe = 1.0f;
    float probeView = 0.0f;
    // What a metal's painted albedo is multiplied by to give its reflectance. MU paints metal
    // dark, with its shading in the paint; 1 is the paint as it is. Invention, judged by eye.
    float metalGain = 1.0f;

    // Re-reads `path` when its timestamp has moved. True when something changed, so the
    // caller can log it. The first call always reads.
    bool reloadIfChanged(const std::string& path);

    // Lays a second sheet over the values in hand: the keys it names win and the rest stand.
    // The viewer's times of day are these -- sheets/time/dusk.json is a dozen keys, not a
    // second copy of lighting.json that drifts from it. Reads every time it is asked.
    bool readOverlay(const std::string& path);

    // The unit vector towards the sun, from the two angles.
    void sunDirection(float out[3]) const;

private:
    void apply(const core::Json& doc, const std::string& from);

    int64_t modified_ = 0;
    bool everRead_ = false;
};

}  // namespace mu::gfx
