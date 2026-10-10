// Whose EarthQuake reaches the camera. MU's one global is set by every meteor, crater and wall
// it makes, but MU makes them only for what is in the player's viewport; our mirror plays the
// whole map's spells inside a 32-tile draw range, so a bot's Meteorite twenty tiles off shook
// the camera as if it had landed at his feet (the user, 2026-10-10: "probably meteor which
// happend somewhere, but for some reason also my camera is affected"). **Ours**: the hero's own,
// the world's (id 0: a trap, a raid's rocks) and anything landing within kReach of him.
#pragma once

#include <cstdint>

namespace mu::game {

struct QuakeFor {
    static constexpr float kReach = 5.0f;  // metres from the hero

    uint32_t hero = 0;
    float x = 0.0f, z = 0.0f;

    bool near(float atX, float atZ) const {
        const float dx = atX - x, dz = atZ - z;
        return dx * dx + dz * dz <= kReach * kReach;
    }
    bool shakes(uint32_t who, float atX, float atZ) const {
        return who == 0 || who == hero || near(atX, atZ);
    }
};

}  // namespace mu::game
