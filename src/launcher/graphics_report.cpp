#include "launcher/graphics_report.h"

#include <cstdint>

#include "core/log.h"
#include "launcher/https.h"

namespace mu::launcher {

namespace {

// Its strings are the game's own words (a preset's name, bgfx's renderer's), never a player's.
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (uint8_t(c) >= 0x20) out += c;
    }
    return out + "\"";
}

}  // namespace

bool sendGraphics(const std::string& site, const std::string& token, const GraphicsReport& r) {
    const auto n = [](const char* key, int value) { return ",\"" + std::string(key) + "\":" + std::to_string(value); };
    const std::string body = "{\"preset\":" + quoted(r.preset) + ",\"renderer\":" + quoted(r.renderer) +
                             n("scale", r.scale) + n("msaa", r.msaa) + n("shadows", r.shadows) +
                             n("ssao", r.ssao) + n("bloom", r.bloom) + n("reflections", r.reflections) +
                             n("grass", r.grass) + n("lights", r.lights) + n("textures", r.textures) +
                             n("cap", r.cap) + n("fps", r.fps) + n("tuned", r.tuned) + n("hand", r.hand) +
                             n("width", r.width) + n("height", r.height) + n("fullscreen", r.fullscreen) + "}";
    const Reply reply = request("POST", site + "/api/graphics", body,
                                {"Content-Type: application/json", "Authorization: Bearer " + token});
    if (reply.status == 200) {
        core::logf("graphics: told the website %s at %d fps", r.preset.c_str(), r.fps);
        return true;
    }
    core::logError("graphics: the website was not told: %s",
                   reply.status ? ("HTTP " + std::to_string(reply.status)).c_str() : reply.error.c_str());
    return false;
}

}  // namespace mu::launcher
