// The quest on screen: the tracker at the right, the banners when a quest is taken and handed in,
// and the pointer at the frame's edge to the giver when he is the next step and out of sight.
//
// The proposal of 2026-09-28 (claude.ai/artifact/8nQVewJ3f2VkKd74T2ktn2), in Sanctuary's own
// tokens: no box, text on the world over a scrim felt rather than seen, a ring for a live step, a
// check for a done one, a blood dot for the one to act on now, and nothing moving unless the
// quest did. It reads the realm and nothing else, and it rebuilds when what it shows changes --
// a count, a state, a banner's fade -- as every window here does (interface is a mirror).
//
// Ours: MU 0.75 has no quests and so no tracker (sim/quests.h).
#pragma once

#include <cstdint>
#include <string>

#include "gfx/interface.h"
#include "sim/quests.h"

namespace mu::game {

class Play;

namespace quest_marks {
enum class StepMark : uint8_t { Waiting, Live, Done, Ready };
// A step's 14u mark, centred on (cx, cy): an iron-low ring, a lit iron ring, an ash check, a
// solid blood dot. Shared with the dialog, which lists the same steps.
void mark(gfx::Canvas& canvas, StepMark kind, float cx, float cy, float u, float alpha = 1.0f);
// A packed colour with its alpha multiplied.
uint32_t faded(uint32_t abgr, float alpha);
// "11h 42m", "42m", "under a minute".
std::string wait(int64_t seconds);
}  // namespace quest_marks

class Tracker {
public:
    void open(const gfx::Interface& interface);
    void close();
    // One frame. `hidden` is a window open on the right (the bag, the character, a shelf, the
    // quest dialog itself): the tracker fades out of its way. `viewProj` places the edge pointer.
    void update(float seconds, const Play& play, bool hidden, const float* viewProj, int width,
                int height);
    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& banner() const { return banner_; }
    bool showing() const { return !canvas_.empty(); }
    bool announcing() const { return !banner_.empty(); }

private:
    void rebuild(const Play& play, int width, int height);
    void rebuildBanner(int width, int height);

    gfx::Canvas canvas_;
    gfx::Canvas banner_;
    bool opened_ = false;

    // What the realm said last frame, to see a quest move.
    sim::QuestProgress last_[sim::kQuests];
    bool seen_ = false;

    // The eased pieces: the tracker's own fade, each step's shown count, each step's ember.
    float shown_ = 0.0f;
    float counts_[sim::kQuestSteps] = {};
    float ember_[sim::kQuestSteps] = {};
    int quest_ = -1;  // the quest the tracker follows

    // The banner: what it says, and where it is in its life.
    std::string bannerKicker_, bannerTitle_, bannerLine_, bannerZen_;
    float bannerAge_ = -1.0f;
    float bannerHold_ = 2.2f;

    // The edge pointer, when the giver is the next step and off the frame.
    bool pointing_ = false;
    float pointX_ = 0.0f, pointY_ = 0.0f, pointAngle_ = 0.0f;
    int pointMetres_ = 0;

    struct Drawn {
        int quest = -1;
        sim::QuestProgress progress;
        int counts[sim::kQuestSteps] = {};
        int embers[sim::kQuestSteps] = {};
        int shown = 0;
        int width = 0, height = 0;
        int64_t minutesLeft = 0;
        bool pointing = false;
        int pointX = 0, pointY = 0, pointMetres = 0;
        bool operator==(const Drawn& o) const;
    };
    Drawn drawn_;
    bool built_ = false;
    int bannerDrawn_ = -1;  // the banner's last alpha step, 0..64, or -1 for none
};

}  // namespace mu::game
