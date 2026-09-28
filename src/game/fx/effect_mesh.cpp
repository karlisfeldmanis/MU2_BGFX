#include "game/fx/effect_mesh.h"

#include <algorithm>
#include <cstdio>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

bool loadEffectObj(const std::string& path, float scale, const std::string& groupFilter,
                     std::vector<EffectCorner>& out) {
    const std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) {
        core::logError("effect: no %s", path.c_str());
        return false;
    }
    std::vector<float> at, uv;
    std::string line;
    std::string currentGroup;
    const auto corner = [&](const char* token) {
        int v = 0, t = 0;
        if (std::sscanf(token, "%d/%d", &v, &t) < 1) return;
        if (v <= 0 || size_t(v) * 3 > at.size()) return;
        EffectCorner c;
        c.x = at[size_t(v - 1) * 3 + 0] * scale;
        c.y = at[size_t(v - 1) * 3 + 1] * scale;
        // MU's Y runs south and this engine's Z runs north: docs/conventions.md, and the one
        // negation belongs here at the edge rather than in the motion.
        c.z = -at[size_t(v - 1) * 3 + 2] * scale;
        c.u = t > 0 && size_t(t) * 2 <= uv.size() ? uv[size_t(t - 1) * 2] : 0.5f;
        c.v = t > 0 && size_t(t) * 2 <= uv.size() ? 1.0f - uv[size_t(t - 1) * 2 + 1] : 0.5f;
        out.push_back(c);
    };
    for (size_t i = 0; i <= bytes.size(); ++i) {
        if (i < bytes.size() && bytes[i] != '\n') {
            line.push_back(char(bytes[i]));
            continue;
        }
        float a = 0, b = 0, c = 0;
        char p[3][32];
        if (std::sscanf(line.c_str(), "v %f %f %f", &a, &b, &c) == 3) {
            at.insert(at.end(), {a, b, c});
        } else if (std::sscanf(line.c_str(), "vt %f %f", &a, &b) == 2) {
            uv.insert(uv.end(), {a, b});
        } else if (line.size() > 2 && line[0] == 'g' && line[1] == ' ') {
            currentGroup = line.substr(2);
            while (!currentGroup.empty() &&
                   (currentGroup.back() == '\r' || currentGroup.back() == ' ')) {
                currentGroup.pop_back();
            }
        } else if (std::sscanf(line.c_str(), "f %31s %31s %31s", p[0], p[1], p[2]) == 3) {
            if (groupFilter.empty() || currentGroup == groupFilter) {
                const size_t before = out.size();
                for (auto& token : p) corner(token);
                if (out.size() != before + 3) out.resize(before);
            }
        }
        line.clear();
    }
    return !out.empty();
}

void submitEffectAlong(gfx::Effects& effects, const std::vector<EffectCorner>& tris,
                         bgfx::TextureHandle sheet, gfx::Blend blend, const float at[3],
                         const float x[3], const float y[3], const float z[3], float scale,
                         const float colour[3], float alpha, float uShift) {
    if (tris.empty() || !bgfx::isValid(sheet)) return;
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = sheet;
    sprite.blend = blend;
    for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k];
    sprite.colour[3] = alpha;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        for (int k = 0; k < 4; ++k) {
            const EffectCorner& p = tris[i + size_t(std::min(k, 2))];
            for (int a = 0; a < 3; ++a) {
                sprite.corner[k][a] = at[a] + (p.x * x[a] + p.y * y[a] + p.z * z[a]) * scale;
            }
            sprite.cornerUv[k][0] = p.u + uShift;
            sprite.cornerUv[k][1] = p.v;
        }
        for (int a = 0; a < 3; ++a) {
            sprite.position[a] =
                (sprite.corner[0][a] + sprite.corner[1][a] + sprite.corner[2][a]) / 3.0f;
        }
        effects.add(sprite);
    }
}

}  // namespace mu::game
