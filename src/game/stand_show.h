// A figure's show while it stands: the studio's Stage (docs/order-plan.md §9c, after F7).
//
// Play draws a monster's show -- its aura, its stars, its eye trails, its flames -- for the
// bodies of a played world, its rules resolved per body in Play::fit (play_open.cpp) and fed
// each frame in play_show.cpp. The bench had none of it, so a monster on the Stage stood bare
// (the user, 2026-10-12, asked for it there). This is the same feed for one standing figure:
// the same effect classes (fx/shadow_stars, fx/eye_trails, fx/body_flames), the same recipe
// data (FigureBody's show), the same tuning (play_tuning.h), each line traced to Play's.
//
// What a STANDING body shows, and no more: no blow, no death, no fade. Not yet: the Zaikan's
// foot fires (Play's breath), a Hydra's heads, the Vepar's and the Devil's beams, the claws --
// each a blow's or Play's own. Sprint H, the effect registry, is where Play and this become one
// feed; until then a change to one of Play's lines here is a change to make in both.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/fx/body_flames.h"
#include "game/fx/eye_trails.h"
#include "game/fx/shadow_stars.h"

namespace mu::content {
class Textures;
struct Showing;
}

namespace mu::game {

class Figure;
struct FigureBody;

class StandShow {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();
    bool isOpen() const { return open_; }

    // The bones the figure's show hangs on, read off its body as Play::fit reads them.
    void fit(const Figure& figure);
    // A frame of it, at the figure's pose.
    void update(float seconds, const Figure& figure);
    void gather(gfx::Effects& effects, const float* eye) const;
    // Its lights (the aura's, the stars'), the nearest first, for the Stage's transient lights.
    uint32_t lights(gfx::PointLight* out, uint32_t max, const float near[3]) const;

private:
    uint32_t dice();

    bool open_ = false;
    ShadowStars stars_;
    EyeTrails trails_;
    BodyFlames flames_;
    const FigureBody* body_ = nullptr;

    int aura_ = -1;
    float auraColour_[3] = {0, 0, 0};
    std::vector<int> starBones_;
    const float* starTint_ = nullptr;
    std::vector<int> arcBones_;
    int arcsAFrame_ = 0;
    float arcHalf_ = 0.0f, arcStep_ = 0.0f;
    uint32_t arcLit_ = 0;
    std::vector<int> shadeBones_;
    bool shadePoison_ = false;
    bool embers_ = false;
    int emberBone_ = -1;
    float emberEvery_ = 1.0f, emberOwed_ = 0.0f;
    int trail_[2] = {-1, -1};
    int handFlame_[4] = {-1, -1, -1, -1};
    float handOwed_ = 0.0f;
    int burnBones_[35] = {};
    bool burns_ = false;
    float burnOwed_ = 0.0f;
    int burnFrame_ = 0;
    float clock_ = 0.0f;
    uint32_t dice_ = 0x9e3779b9u;
};

}  // namespace mu::game
