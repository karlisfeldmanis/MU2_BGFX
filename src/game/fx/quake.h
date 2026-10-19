// Whose EarthQuake reaches the camera. MU's one global is set by every meteor, crater and wall
// it makes, but MU makes them only for what is in the player's viewport; our mirror plays the
// whole map's spells inside a 32-tile draw range, so a bot's Meteorite twenty tiles off shook
// the camera as if it had landed at his feet (the user, 2026-10-10: "probably meteor which
// happend somewhere, but for some reason also my camera is affected"). **Ours**: the hero's own,
// the world's (id 0: a trap, a raid's rocks) and anything landing within kReach of him.
// And none but his own while he stands on a safe tile: a player fighting a Cursed Wizard at
// the edge of the Lost Tower's safe zone shook the camera of everyone resting in it (the
// user, 2026-10-19: "i am on safezone, some char is doing some other monster ... also my
// camera shakes").
#pragma once

#include <cstdint>

namespace mu::game {

struct QuakeFor {
    static constexpr float kReach = 5.0f;  // metres from the hero

    uint32_t hero = 0;
    float x = 0.0f, z = 0.0f;
    bool safe = false;  // he stands on one of MU's safe tiles

    bool near(float atX, float atZ) const {
        if (safe) return false;
        const float dx = atX - x, dz = atZ - z;
        return dx * dx + dz * dz <= kReach * kReach;
    }
    bool shakes(uint32_t who, float atX, float atZ) const {
        return who == hero || (!safe && (who == 0 || near(atX, atZ)));
    }
};

}  // namespace mu::game
