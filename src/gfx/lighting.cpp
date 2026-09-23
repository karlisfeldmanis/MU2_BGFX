#include "gfx/lighting.h"

#include <cmath>

#include "core/files.h"
#include "core/json.h"
#include "core/log.h"

namespace mu::gfx {

void Lighting::sunDirection(float out[3]) const {
    const float az = azimuth * 3.14159265f / 180.0f;
    const float el = elevation * 3.14159265f / 180.0f;
    const float cosEl = std::cos(el);
    // Towards the sun: y is up, and the azimuth turns from +x towards -z, which is the way
    // a row runs on MU's grid. docs/conventions.md.
    out[0] = cosEl * std::cos(az);
    out[1] = std::sin(el);
    out[2] = -cosEl * std::sin(az);
}

void Lighting::apply(const core::Json& doc, const std::string& from) {
    // Each key is optional and overrides one value, so a sheet naming three things is three
    // lines long. A key nobody reads is a key nobody notices, so unknown ones are said.
    doc.readInto("azimuth", &azimuth);
    doc.readInto("elevation", &elevation);
    doc.readVec3Into("sun_colour", sunColour);
    doc.readInto("sun_strength", &sunStrength);
    doc.readVec3Into("sky_colour", skyColour);
    doc.readInto("horizon_paleness", &horizonPaleness);
    doc.readVec3Into("ground_colour", groundColour);
    doc.readInto("ambient_strength", &ambientStrength);
    doc.readInto("exposure", &exposure);
    doc.readVec3Into("dust_colour", dustColour);
    doc.readInto("dust_density", &dustDensity);
    doc.readInto("shadow_range", &shadowRange);
    doc.readInto("shadow_fit_below", &shadowFitBelow);
    doc.readInto("shadow_bias_metres", &shadowBiasMetres);
    doc.readInto("shadow_normal_bias", &shadowNormalBias);
    doc.readInto("sun_angle_degrees", &sunAngleDegrees);
    doc.readInto("lamp_strength", &lampStrength);
    doc.readInto("glow_strength", &glowStrength);
    doc.readInto("bloom_threshold", &bloomThreshold);
    doc.readInto("bloom_knee", &bloomKnee);
    doc.readInto("bloom_strength", &bloomStrength);
    doc.readInto("flame_strength", &flameStrength);
    doc.readInto("sharpen", &sharpen);
    doc.readInto("contrast", &contrast);
    doc.readInto("tonemap", &tonemap);
    doc.readInto("saturation", &saturation);
    doc.readInto("split", &split);
    doc.readVec3Into("tint_low", tintLow);
    doc.readVec3Into("tint_high", tintHigh);
    doc.readInto("ssao_radius", &ssaoRadius);
    doc.readInto("ssao_strength", &ssaoStrength);
    doc.readInto("probe", &probe);
    doc.readInto("probe_view", &probeView);
    doc.readInto("metal_gain", &metalGain);
    doc.readInto("grass", &grass);
    doc.readInto("grass_radius", &grassRadius);
    doc.readInto("grass_fade", &grassFade);
    doc.readInto("grass_thin", &grassThin);
    doc.readInto("grass_mip_bias", &grassMipBias);
    doc.readInto("grass_density", &grassDensity);
    doc.readInto("grass_height", &grassHeight);
    doc.readInto("grass_aspect", &grassAspect);
    doc.readInto("grass_lean", &grassLean);
    doc.readInto("grass_rank", &grassRank);
    doc.readInto("grass_dry", &grassDry);
    doc.readInto("grass_meadow", &grassMeadow);
    doc.readInto("grass_meadow_height", &grassMeadowHeight);
    doc.readInto("grass_widen", &grassWiden);
    doc.readVec3Into("grass_root_colour", grassRootColour);
    doc.readVec3Into("grass_tip_colour", grassTipColour);
    doc.readInto("grass_colour", &grassColour);
    doc.readInto("grass_cutout", &grassCutout);
    doc.readInto("grass_painted", &grassPainted);
    doc.readInto("grass_root_ao", &grassRootAo);
    doc.readInto("grass_roughness", &grassRoughness);
    doc.readInto("grass_wind_strength", &grassWindStrength);
    doc.readInto("grass_wind_degrees", &grassWindDegrees);

    static const char* kKnown[] = {
        "azimuth", "elevation", "sun_colour", "sun_strength", "sky_colour", "horizon_paleness",
        "ground_colour", "ambient_strength", "exposure", "dust_colour", "dust_density", "shadow_range", "shadow_fit_below", "shadow_bias_metres",
        "shadow_normal_bias", "sun_angle_degrees", "lamp_strength", "glow_strength", "bloom_threshold", "bloom_knee", "bloom_strength", "flame_strength", "sharpen", "contrast", "tonemap", "saturation", "split", "tint_low", "tint_high", "ssao_radius", "ssao_strength", "probe", "probe_view", "metal_gain",
        "grass", "grass_radius", "grass_fade", "grass_thin", "grass_mip_bias", "grass_density", "grass_height", "grass_aspect",
        "grass_lean", "grass_rank", "grass_dry", "grass_meadow", "grass_meadow_height", "grass_widen", "grass_root_colour", "grass_tip_colour", "grass_colour", "grass_cutout", "grass_painted", "grass_root_ao",
        "grass_roughness", "grass_wind_strength", "grass_wind_degrees", "note"};
    for (const auto& [key, value] : doc.members) {
        bool known = false;
        for (const char* k : kKnown) known = known || key == k;
        if (!known) core::logError("%s names '%s', which nothing reads", from.c_str(), key.c_str());
    }
}

bool Lighting::reloadIfChanged(const std::string& path) {
    const int64_t stamp = core::fileModified(path);
    if (stamp == 0) {
        // No sheet is not an error: the defaults in the header are a working rig, and this
        // is what a fresh checkout with no sheets/ does.
        if (!everRead_) {
            everRead_ = true;
            core::logf("no lighting sheet at %s; the engine's own defaults stand", path.c_str());
            return true;
        }
        return false;
    }
    if (everRead_ && stamp == modified_) return false;
    modified_ = stamp;
    everRead_ = true;

    core::Json doc = core::parseJsonFile(path);
    if (doc.isNull()) {
        core::logError("the lighting sheet did not parse; the values in hand still stand");
        return false;
    }

    apply(doc, path);

    float dir[3];
    sunDirection(dir);
    core::logf("lighting sheet read: sun %.0f/%.0f (%.2f %.2f %.2f) x%.2f, exposure %.2f, "
               "ssao r%.2f x%.2f, shadow %.0f m",
               azimuth, elevation, dir[0], dir[1], dir[2], sunStrength, exposure, ssaoRadius,
               ssaoStrength, shadowRange);
    return true;
}

bool Lighting::readOverlay(const std::string& path) {
    // Unconditional, unlike the sheet: an overlay is laid on whatever the sheet just gave, so
    // it has to be laid again every time the sheet is re-read, changed or not.
    core::Json doc = core::parseJsonFile(path);
    if (doc.isNull()) {
        core::logError("%s did not parse; the sheet's values stand", path.c_str());
        return false;
    }
    apply(doc, path);
    return true;
}

}  // namespace mu::gfx
