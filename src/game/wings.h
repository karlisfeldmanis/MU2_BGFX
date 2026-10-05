// A worn wing, drawn: the 1st level wings on the bearer's back (docs/wings.md). Drawing only --
// whether he wears one and whether he flies are the realm's (sim::kWings, sim::Body::flying).
//
// MuMain draws c->Wing as its own model playing its own action, linked to player bone 47 --
// Bone05, between the shoulders, the bone a slung weapon hangs on (FigureBody::backBone) -- at
// (0, 0, 15) in the bone's frame, in the bone's rotation, unlinked as the Imp is, at the
// character's scale (RenderLinkObject, ZzzCharacter.cpp:15400-15430). Its one action, the flap,
// plays at PlaySpeed 0.25 while he stands or walks and at 1 while he flies (PLAYER_FLY and
// PLAYER_FLY_CROSSBOW): a slow stir at rest, a beat in the air. The cook exported the clip at
// 0.25 (the recipe's play_speed), so those are rates 1 and 4 here.
//
// No effect rides on the 1st level wings in MuMain: no sprite, no particle, no light. The Wings
// of Elf are drawn added (BlendMesh 0, ZzzObject.cpp:5332-5340), which their cook carries, and
// no wing shows its plus (RenderPartObjectEffect's Level forced to 0, ZzzObject.cpp:9566-9569).
//
// One drawer for the game's hero (Play) and the wardrobe's Wings tab (ModelBench).
#pragma once

#include <vector>

#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/renderer.h"

namespace mu::game {

// The flap's rates against the clip as cooked (PlaySpeed 0.25): standing and flying.
constexpr float kWingRestRate = 1.0f;
constexpr float kWingFlyRate = 4.0f;

class WingLook {
public:
    // The wing's own body (Wing01..03 out of a figures table), or null for none.
    void wear(const FigureBody* wing, const Figure& bearer);
    const FigureBody* worn() const { return wing_; }
    void update(float seconds, bool flying);
    // After the bearer is posed this frame: the wing takes his Bone05 as it is now.
    void gather(gfx::Renderer& renderer, const Figure& bearer, std::vector<float>& scratch,
                std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters,
                float fade = 1.0f);
    // Its tips in world metres as last posed, for fx/wing_motes: the ends of its bone chains,
    // at most `most`, spread along them. 0 before its first pose or with none worn.
    int tips(float out[][3], int most) const;

private:
    const FigureBody* wing_ = nullptr;
    Figure figure_;
    std::vector<int> tipBones_;  // the chains' last bones, chosen on wear
    bool posed_ = false;
};

// The wing body a worn row draws as: group 12 numbers 0-2 are Wing01-03 (ZzzOpenData.cpp:1019,
// MODEL_WING + i from Wing(i+1).bmd). Null for anything else or a table without it.
const FigureBody* wingBody(const Figures& figures, int group, int number);

}  // namespace mu::game
