// The five figures on the character screen, stood where MU stands them and seen through MU's
// lens. MU2's Pedestals.cs, carried over number for number.
//
// Season 5.2's CharacterScene: ReceiveCharacterList puts a CreateHero at five fixed positions in
// a curve receding from a fixed camera, each turned a little more toward it (WSclient.cpp:687);
// every figure is scaled 1.2 whatever its class (ZzzCharacter.cpp:12131). They stand on the map
// the scene owns -- world 74, folder World75, cooked here as `charscene` -- on its ground rather
// than at MU's fixed 163.
//
// **The lens is most of the look.** A 40-degree horizontal field against MU's fixed 4:3, which is
// 30.75 degrees vertical (CameraConfig::ForCharacterScene, HFovToVFov), from a camera seventeen
// tiles off the row and a fraction above it: MU's view is Rx(-84.5)*Rz(-75) on a Z-up world, a
// level look turned seventy-five degrees and tipped five and a half down. MU drew the scene into
// a 430-tall band of its 480; the field here is MU's over the whole window, as MU2 decided, so a
// wide window sees more of the set to the sides and never less of it above.
//
// **The pick** (RenderSelectedCharacterEffects, CharacterScene.cpp:275-351): two counter-rotating
// auroras at the feet, 1.8 and 1.2 tiles across, turning a hundredth of a degree a millisecond at
// a luminance of sin(t*1.5)*0.3+0.5; and two streams of particles, one of each a MU frame --
// `chasellight` streaks that hang and `Impack03` sparks that rise, both starting at 0.15 of light
// and brightening by 1.16 a frame for ten frames, then dimming (ZzzEffectParticle.cpp:88-146).
// MU lights the picked figure at 1.4 and pools white terrain light one tile round its feet
// (ApplySelectedCharacterLighting); here that is one moving point light over the ring, since
// the figures are lit by the scene's own light and not flat at MU's 0.4.
//
// Every figure stands as a character stands in town: weapon slung and the bare idle, because the
// scene is a safe place -- the arrangement MU2's Crowd.Dress draws on crossing into a safe zone.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/tables.h"
#include "content/texture.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "game/roster.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Pedestals {
public:
    // MU's world 74, the pipeline's `charscene`.
    static constexpr const char* kMap = "charscene";

    bool open(Figures* figures, const content::Ground* ground, const content::Tables* tables,
              const std::string& assetDir, content::Textures& textures);
    void shutdown();

    // Stands the roster up, replacing whoever was there. The pick is kept when its slot is
    // still somebody's.
    void raise(const std::vector<Seat>& roster);
    // A class standing on an empty slot, for the create window -- ours: MU shows the class as a
    // bust inside the window (CharMakeWin.cpp), and here the class the player is choosing stands
    // on the pedestal it will take, in the scene's light, picked. -1 takes it down.
    void preview(int slot, sim::Kin kin);

    // Where the scene keeps its camera, and its lens.
    void aim(gfx::Camera& camera) const;

    // The slot under the pointer, or -1. MU's pick box for this scene (BuildCharacterScenePickOBB):
    // 144 units square at the feet and at least 300 tall, projected; nearest wins.
    int hover(const float* viewProj, float width, float height, float x, float y) const;
    // Where a slot's name plate hangs, in pixels: a fixed clearance over the crown (MU2's
    // BalloonClear, since the built body is taller than the one MU measured its 350 against).
    // False when the slot is empty or behind the eye.
    bool crown(int slot, const float* viewProj, float width, float height, float* x,
               float* y) const;

    int picked() const { return picked_; }
    // A figure newly picked greets the camera once, its class's own gesture, and goes back to
    // its idle (see kGreeting in pedestals.cpp).
    void pick(int slot);
    bool standing(int slot) const;
    // The first pedestal nobody stands on, or -1.
    int freeSlot() const {
        for (int s = 0; s < kRosterSlots; ++s) {
            if (!stands_[s].up) return s;
        }
        return -1;
    }

    void update(float seconds);
    void gather(gfx::Renderer& renderer, std::vector<gfx::Drawable>& out,
                std::vector<gfx::Drawable>* casters);
    // The rings and the two streams, into the transparent pass.
    void gatherEffects(gfx::Effects& effects) const;
    // The light over the pick, or 0 when nobody is picked.
    uint32_t lights(gfx::PointLight* out, uint32_t room) const;

private:
    struct Stand {
        bool up = false;
        Figure figure;
        float height = 2.0f;  // the body's own height, metres, at the scene's scale
        sim::Kin kin = sim::Kin::DarkKnight;
        int idle = -1;           // the clip it stands in
        float greeting = 0.0f;   // seconds of its greeting left, 0 when it stands idle
    };
    struct Mote {
        float at[3];
        float life, light, rise, half[2];
        bool blob;
    };
    void standAt(int slot, const FigureBody* body, sim::Kin kin);
    // Who the rings, the streams and the light are on: the create window's class while it
    // stands on its pedestal, and the pick otherwise.
    const Stand* subject() const;
    const FigureBody* dressed(int slot, sim::Kin kin, bool second,
                              const std::vector<Saved::Item>& items);
    void spawn(const float feet[3], bool blob);
    float roll(int n);  // 0 .. n-1, MU's rand() % n

    Figures* figures_ = nullptr;
    const content::Ground* ground_ = nullptr;
    const content::Tables* tables_ = nullptr;
    Stand stands_[kRosterSlots + 1];  // the last is the create window's preview
    int previewSlot_ = -1;
    int picked_ = -1;
    std::vector<float> scratch_;
    bgfx::TextureHandle aurora_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blob_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    std::vector<Mote> motes_;
    float clock_ = 0.0f, owed_ = 0.0f;
    uint32_t dice_ = 0x2545F491u;
};

}  // namespace mu::game
