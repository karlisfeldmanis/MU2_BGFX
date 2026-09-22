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
    if (which_ > 0 || announce_) core::logf("time of day: %s", kTimes[which_]);
}

void TimeOfDay::reloadIfChanged() {
    // The overlay is watched as well as the sheet, so dusk can be tuned live too.
    if (lighting_->reloadIfChanged(paths_->sheet) ||
        (which_ > 0 && core::fileModified(overlayPath(which_)) != overlayStamp_)) {
        set(which_);
    }
}

float daylightOf(const gfx::Lighting& lighting) {
    const float sky = lighting.ambientStrength +
                      lighting.sunStrength * std::sin(lighting.elevation * 3.14159265f / 180.0f) /
                          3.14159265f;
    return std::clamp(sky / 1.45f, 0.08f, 1.0f);
}

}  // namespace mu::app
