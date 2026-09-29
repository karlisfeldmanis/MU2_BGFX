#include "app/context.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::app {

namespace {
const char* kTimes[] = {"noon", "dusk", "night"};
}

void TimeOfDay::open(const Paths* paths, gfx::Lighting* lighting, int which, bool announce) {
    paths_ = paths;
    lighting_ = lighting;
    announce_ = announce;
    set(which);
}

const char* TimeOfDay::name() const { return kTimes[which_]; }

std::string TimeOfDay::overlayPath(int which) const {
    return paths_->under(paths_->sheets, std::string("time/") + kTimes[which] + ".json");
}

void TimeOfDay::set(int which) {
    which_ = which;
    // Rebuilt from the sheet rather than patched. See the header.
    *lighting_ = gfx::Lighting();
    lighting_->reloadIfChanged(paths_->sheet);
    overlayStamp_ = 0;
    if (which_ > 0) {
        const std::string overlay = overlayPath(which_);
        overlayStamp_ = core::fileModified(overlay);
        lighting_->readOverlay(overlay);
    }
    sceneStamp_ = 0;
    if (!scene_.empty()) {
        sceneStamp_ = core::fileModified(scene_);
        lighting_->readOverlay(scene_);
    }
    dry_ = *lighting_;
    wet_ = dry_;
    wetStamp_ = 0;
    if (!wetPath_.empty()) {
        wetStamp_ = core::fileModified(wetPath_);
        wet_.readOverlay(wetPath_);
    }
    rain(share_, flash_);
    if (which_ > 0 || announce_) core::logf("time of day: %s", kTimes[which_]);
}

void TimeOfDay::reloadIfChanged() {
    // The overlay is watched as well as the sheet, so dusk can be tuned live too.
    if (lighting_->reloadIfChanged(paths_->sheet) ||
        (which_ > 0 && core::fileModified(overlayPath(which_)) != overlayStamp_) ||
        (!scene_.empty() && core::fileModified(scene_) != sceneStamp_) ||
        (!wetPath_.empty() && core::fileModified(wetPath_) != wetStamp_)) {
        set(which_);
    }
}

void TimeOfDay::setScene(const std::string& path) {
    if (path == scene_) return;
    scene_ = path;
    set(which_);
}

void TimeOfDay::setWet(const std::string& path) {
    if (path == wetPath_) return;
    wetPath_ = path;
    set(which_);
}

void TimeOfDay::rain(float share, float flash) {
    share_ = std::clamp(share, 0.0f, 1.0f);
    flash_ = std::clamp(flash, 0.0f, 1.0f);
    if (lighting_ == nullptr || wetPath_.empty()) return;
    // Only what a wet sheet is for: the light, the air and the grade. Everything else -- the
    // shadow's fit, the grass, the metal -- stands as the dry light has it.
    *lighting_ = dry_;
    if (share_ > 0.0f) wetten();
    if (flash_ > 0.0f) lightning();
}

// The lightning: the whole sky lit at once for the moment of a strike, so it comes in as
// ambient -- the hemisphere every surface is lit by -- and not as a sun, and casts nothing.
// The zenith and the bounce go blue-white and several times stronger, the air's haze lights up
// with them only a little -- lit fully it was a white fog over the square -- and the colour
// drains, as a flash bleaches what it shows. Invention, judged by eye on Lorencia's night,
// the same in both worlds.
void TimeOfDay::lightning() {
    constexpr float kSky[3] = {0.72f, 0.8f, 1.0f};
    constexpr float kBounce[3] = {0.16f, 0.17f, 0.21f};
    constexpr float kAir[3] = {0.36f, 0.4f, 0.5f};
    const float t = flash_;
    gfx::Lighting& l = *lighting_;
    for (int k = 0; k < 3; ++k) {
        l.skyColour[k] += (kSky[k] - l.skyColour[k]) * t;
        l.groundColour[k] += (kBounce[k] - l.groundColour[k]) * t;
        l.dustColour[k] += (kAir[k] - l.dustColour[k]) * t * 0.5f;
    }
    l.ambientStrength += 1.1f * t;
    l.saturation *= 1.0f - 0.25f * t;
}

void TimeOfDay::wetten() {
    const float t = share_;
    const auto mix = [t](float& to, float a, float b) { to = a + (b - a) * t; };
    gfx::Lighting& l = *lighting_;
    for (int k = 0; k < 3; ++k) {
        mix(l.sunColour[k], dry_.sunColour[k], wet_.sunColour[k]);
        mix(l.skyColour[k], dry_.skyColour[k], wet_.skyColour[k]);
        mix(l.groundColour[k], dry_.groundColour[k], wet_.groundColour[k]);
        mix(l.dustColour[k], dry_.dustColour[k], wet_.dustColour[k]);
        mix(l.tintLow[k], dry_.tintLow[k], wet_.tintLow[k]);
        mix(l.tintHigh[k], dry_.tintHigh[k], wet_.tintHigh[k]);
    }
    mix(l.sunStrength, dry_.sunStrength, wet_.sunStrength);
    mix(l.ambientStrength, dry_.ambientStrength, wet_.ambientStrength);
    mix(l.horizonPaleness, dry_.horizonPaleness, wet_.horizonPaleness);
    mix(l.exposure, dry_.exposure, wet_.exposure);
    mix(l.dustDensity, dry_.dustDensity, wet_.dustDensity);
    mix(l.saturation, dry_.saturation, wet_.saturation);
    mix(l.split, dry_.split, wet_.split);
    mix(l.contrast, dry_.contrast, wet_.contrast);
    mix(l.bloomStrength, dry_.bloomStrength, wet_.bloomStrength);
    mix(l.lampStrength, dry_.lampStrength, wet_.lampStrength);
    mix(l.glowStrength, dry_.glowStrength, wet_.glowStrength);
}

float daylightOf(const gfx::Lighting& lighting) {
    const float sky = lighting.ambientStrength +
                      lighting.sunStrength * std::sin(lighting.elevation * 3.14159265f / 180.0f) /
                          3.14159265f;
    return std::clamp(sky / 1.45f, 0.08f, 1.0f);
}

}  // namespace mu::app
