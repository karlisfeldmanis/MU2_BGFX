// The ear, with no device: `build/sound_test`.
//
// Foundation 9 -- what can be checked without a window is checked without one, and sound
// cannot be looked at at all. Everything docs/spatial-sound.md's steps A to D promise is a
// number in the mix: which ear a thing on the right of the screen is in, how quiet it is at
// the frame's edge, how dull it is far off, how loud six plays on one tick are, and how many
// voices a crowd gets. So the game's own Sound is opened offline, over the cooked showing,
// given a shot, played at, and its mix pulled and measured.
//
// The shot is a camera looking straight down, `kScale` of clip space to the metre: x is the
// screen's x and z is its y, and there is no perspective to think through. What the rules ask
// of it is only "where on the screen is this".
//
// It also writes `sound_test.wav` beside itself: one sound walked across the screen from left
// to right, for the ear the assertions stand in for.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "content/showing.h"
#include "core/files.h"
#include "game/sound.h"

using mu::game::Sound;

namespace {

int failures = 0;

void check(bool ok, const char* what, double got = 0.0) {
    std::printf("  %s %s (%.3f)\n", ok ? "ok  " : "FAIL", what, got);
    if (!ok) ++failures;
}

constexpr uint32_t kHero = 1;
constexpr uint32_t kRate = 48000;

struct Ears {
    double left = 0.0, right = 0.0;
    double both() const { return std::sqrt(left * left + right * right); }
};

struct Bench {
    Sound sound;
    float shot[16] = {};
    const float at[3] = {0.0f, 0.0f, 0.0f};

    void look(float scale) {
        std::fill(shot, shot + 16, 0.0f);
        shot[0] = scale;  // clip x = world x
        shot[9] = scale;  // clip y = world z
        shot[15] = 1.0f;
        sound.listen(kHero, at, shot);
    }
    // A new frame: plays on it are not merged with the last one's.
    void frame() { sound.listen(kHero, at, shot); }

    std::vector<float> pull(float seconds) {
        std::vector<float> out(size_t(seconds * kRate) * 2, 0.0f);
        const uint64_t got = sound.render(out.data(), out.size() / 2);
        out.resize(size_t(got) * 2);
        return out;
    }
    // Into a room, and long enough for its ease to finish and the last room's tail to die.
    void settle(mu::game::Sound::Room room) {
        sound.room(room);
        for (int i = 0; i < 40; ++i) {
            frame();
            pull(0.05f);
        }
    }
    // Until nothing placed sounds, so one measurement does not hear the last.
    void hush() {
        for (int i = 0; i < 200 && sound.tally().sounding > 0; ++i) pull(0.05f);
    }
};

Ears rms(const std::vector<float>& stereo) {
    Ears e;
    const size_t frames = stereo.size() / 2;
    for (size_t i = 0; i < frames; ++i) {
        e.left += double(stereo[i * 2]) * stereo[i * 2];
        e.right += double(stereo[i * 2 + 1]) * stereo[i * 2 + 1];
    }
    if (frames > 0) {
        e.left = std::sqrt(e.left / double(frames));
        e.right = std::sqrt(e.right / double(frames));
    }
    return e;
}

// How much of a signal's energy is in its edges: the first difference against the signal. A
// low-pass lowers it whatever the level.
double brightness(const std::vector<float>& stereo) {
    double diff = 0.0, all = 0.0;
    for (size_t i = 2; i + 1 < stereo.size(); i += 2) {
        const double m = 0.5 * (double(stereo[i]) + stereo[i + 1]);
        const double p = 0.5 * (double(stereo[i - 2]) + stereo[i - 1]);
        diff += (m - p) * (m - p);
        all += m * m;
    }
    return all > 0.0 ? diff / all : 0.0;
}

Ears playAndHear(Bench& b, int event, float x, float z, int times = 1) {
    b.hush();
    b.frame();
    for (int i = 0; i < times; ++i) b.sound.playAt(event, x, 0.0f, z);
    b.frame();
    b.sound.follow(nullptr, nullptr);
    return rms(b.pull(0.4f));
}

void writeWav(const std::string& path, const std::vector<float>& stereo) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return;
    const uint32_t frames = uint32_t(stereo.size() / 2);
    const uint32_t bytes = frames * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(kRate);
    u32(kRate * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    for (float s : stereo) {
        const int16_t v = int16_t(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("sound_test: the mix, with no device\n");
    const std::string assets = MU2_ASSET_DIR;
    mu::content::Showing showing;
    std::string error;
    if (!mu::content::loadShowing(assets + "/cooked/showing/showing.mus", showing, error)) {
        std::printf("sound_test: %s -- run tools/cook.py --only showing\n", error.c_str());
        return 1;
    }
    Bench b;
    if (!b.sound.open(assets, showing, false, true)) {
        std::printf("sound_test: the offline engine would not open\n");
        return 1;
    }

    // A one-file event, so two plays of it are the same samples and can be compared, and one
    // long enough that 0.4 s of it is mostly sound. The explosion is both.
    const int blast = b.sound.load("explosion", true);
    if (blast < 0 || showing.event("explosion")->files.size() != 1) {
        std::printf("sound_test: explosion is not a one-file event any more; pick another\n");
        return 1;
    }

    b.look(0.1f);  // the frame is ten metres each way
    // Dry for the pan: a reverb's tail is in both ears alike and outlasts the voice, and
    // either would blur what is measured. The room has its own checks below.
    b.settle(mu::game::Sound::Room::Dry);

    // A. The pan is the screen.
    const Ears centre = playAndHear(b, blast, 0.0f, 0.0f);
    check(std::fabs(centre.left - centre.right) / centre.both() < 0.02,
          "at the hero it is in both ears alike", (centre.left - centre.right) / centre.both());
    const Ears right = playAndHear(b, blast, 5.0f, 0.0f);
    check(right.right > right.left, "half way to the right edge it is on the right",
          right.left / right.right);
    // Balance at 0.5 x kPanWidth leaves the far ear at 0.65.
    check(std::fabs(right.left / right.right - 0.65) < 0.03,
          "and the far ear is at 0.65, not gone", right.left / right.right);
    const Ears left = playAndHear(b, blast, -5.0f, 0.0f);
    check(std::fabs(left.right / left.left - right.left / right.right) < 0.01,
          "the left is the right's mirror", left.right / left.left);
    // Up the screen is not a side.
    const Ears up = playAndHear(b, blast, 0.0f, 5.0f);
    check(std::fabs(up.left - up.right) / up.both() < 0.02, "up the screen is in the middle",
          (up.left - up.right) / up.both());

    // A. The edge is a fade. 8 m is inside the full band; 10 m is at the frame's edge, 0.57 of
    // the way down the fade, and 8/10 by distance; 12.5 m is past silent and refused.
    const Ears inside = playAndHear(b, blast, 8.0f, 0.0f);
    const Ears edge = playAndHear(b, blast, 10.0f, 0.0f);
    check(edge.both() / inside.both() > 0.35 && edge.both() / inside.both() < 0.55,
          "at the frame's edge it is fading, not gone", edge.both() / inside.both());
    const int refusedBefore = b.sound.tally().refused;
    const Ears past = playAndHear(b, blast, 12.5f, 0.0f);
    check(past.both() < 1e-6 && b.sound.tally().refused == refusedBefore + 1,
          "past the edge it is refused, not started silent", past.both());
    // And the distance is still MU's 1/d inside the full band: 4 m against 8 m.
    const Ears four = playAndHear(b, blast, 0.0f, 4.0f);
    const Ears eight = playAndHear(b, blast, 0.0f, 8.0f);
    check(std::fabs(eight.both() / four.both() - 0.5) < 0.08, "twice as far is half as loud",
          eight.both() / four.both());

    // D. Far is duller. A wider frame, so both are well inside it.
    b.look(0.02f);
    b.hush();
    b.frame();
    b.sound.playAt(blast, 2.0f, 0.0f, 0.0f);
    b.frame();
    b.sound.follow(nullptr, nullptr);
    const double near = brightness(b.pull(0.4f));
    b.hush();
    b.frame();
    b.sound.playAt(blast, 20.0f, 0.0f, 0.0f);
    b.frame();
    b.sound.follow(nullptr, nullptr);
    const double far = brightness(b.pull(0.4f));
    check(far < near * 0.8, "at twenty metres it has lost its top", far / near);
    b.look(0.1f);

    // B. The merge: the same event twice on a frame at one place is one voice, +3 dB.
    const int mergedBefore = b.sound.tally().merged;
    const Ears one = playAndHear(b, blast, 3.0f, 0.0f);
    const Ears two = playAndHear(b, blast, 3.0f, 0.0f, 2);
    check(b.sound.tally().merged == mergedBefore + 1, "a second play on the frame is merged",
          double(b.sound.tally().merged - mergedBefore));
    check(std::fabs(two.both() / one.both() - std::sqrt(2.0)) < 0.05,
          "and is the square root of two louder", two.both() / one.both());

    // B. A busy event swallows. Two plays on two frames take both its voices; a third while both
    // still sound is not started, as MU's Play() on a playing DirectSound buffer is not -- the
    // restart that made a pack of spiders stutter. The hero's own takes the older voice.
    b.hush();
    const int swallowedBefore = b.sound.tally().swallowed;
    for (int k = 0; k < 3; ++k) {
        b.frame();
        b.sound.playAt(blast, 4.0f, 0.0f, 0.0f);
    }
    check(b.sound.tally().swallowed == swallowedBefore + 1 && b.sound.tally().sounding == 2,
          "a third cry while both voices sound is swallowed",
          double(b.sound.tally().swallowed - swallowedBefore));
    b.frame();
    b.sound.playAt(blast, 0.0f, 0.0f, 0.0f);
    check(b.sound.tally().swallowed == swallowedBefore + 1 && b.sound.tally().sounding == 2,
          "but the hero's own takes the older voice", double(b.sound.tally().sounding));
    b.hush();

    // C. The duck: the hero's moment leans the world back to 0.6, in 50 ms.
    const Ears plain = playAndHear(b, blast, 0.0f, 0.0f);
    b.hush();
    b.sound.duck();
    const Ears ducked = playAndHear(b, blast, 0.0f, 0.0f);
    check(ducked.both() / plain.both() > 0.55 && ducked.both() / plain.both() < 0.7,
          "a duck leans the world back", ducked.both() / plain.both());
    b.settle(mu::game::Sound::Room::Dry);  // and it comes back
    const Ears back = playAndHear(b, blast, 0.0f, 0.0f);
    check(std::fabs(back.both() / plain.both() - 1.0) < 0.02, "and lets it go again",
          back.both() / plain.both());

    // F. A wall: half as loud and muffled, from its first sample.
    struct Walled {
        static bool never(void*, const float*, const float*) { return false; }
    };
    b.sound.walls(Walled::never, nullptr);
    b.hush();
    b.frame();
    b.sound.playAt(blast, 3.0f, 0.0f, 0.0f);
    b.frame();
    b.sound.follow(nullptr, nullptr);
    const std::vector<float> behind = b.pull(0.4f);
    b.sound.walls(nullptr, nullptr);
    b.hush();
    b.frame();
    b.sound.playAt(blast, 3.0f, 0.0f, 0.0f);
    b.frame();
    b.sound.follow(nullptr, nullptr);
    const std::vector<float> open = b.pull(0.4f);
    check(std::fabs(rms(behind).both() / rms(open).both() - 0.45) < 0.12,
          "behind a wall it is about half as loud", rms(behind).both() / rms(open).both());
    check(brightness(behind) < brightness(open) * 0.6, "and muffled",
          brightness(behind) / brightness(open));

    // E. The room: what is left once the voice has stopped. The open town rings a little and
    // a roof rings more.
    const auto tail = [&](mu::game::Sound::Room room) {
        b.settle(room);
        b.frame();
        b.sound.playAt(blast, 0.0f, 0.0f, 0.0f);
        b.frame();
        b.sound.follow(nullptr, nullptr);
        b.hush();
        return rms(b.pull(0.15f)).both();
    };
    const double dryTail = tail(mu::game::Sound::Room::Dry);
    const double openTail = tail(mu::game::Sound::Room::Open);
    const double roofTail = tail(mu::game::Sound::Room::Roofed);
    // Under -60 dBFS rather than 1e-6: since the world's plain lane (steps, swings, blows) sends
    // into the same reverb as its voices, miniaudio caches a period where the two meet, and a
    // dry room leaves the blast's last few milliseconds -- 1.2e-4, -78 dBFS -- in the window.
    // Nothing a room adds; still a test that a dry room rings at all.
    check(dryTail < 1e-3, "dry leaves nothing behind", dryTail);
    check(openTail > 1e-5, "the open town rings", openTail);
    check(roofTail > openTail * 2.0, "and under a roof it rings more", roofTail / openTail);
    b.settle(mu::game::Sound::Room::Dry);

    // B. The budget. Every event, placed, played round a ring six metres off -- all crowd, all
    // at one level -- with no mix pulled, so nothing finishes between plays.
    std::vector<int> all;
    for (const mu::content::SoundEvent& e : showing.events) {
        const int h = b.sound.load(e.name, true, true);
        if (h >= 0 && std::find(all.begin(), all.end(), h) == all.end()) all.push_back(h);
    }
    b.hush();
    const mu::game::Sound::Tally before = b.sound.tally();
    int most = 0;
    for (int i = 0; i < 60; ++i) {
        b.frame();
        const float a = float(i) * 0.7f;
        b.sound.playAt(all[size_t(i) % all.size()], 6.5f * std::cos(a), 0.0f,
                       6.5f * std::sin(a));
        most = std::max(most, b.sound.tally().sounding);
    }
    const mu::game::Sound::Tally full = b.sound.tally();
    check(all.size() >= 13, "enough events to fill the budget", double(all.size()));
    check(most == 24, "a crowd holds at twenty-four voices", double(most));
    check(full.refused > before.refused, "and the crowd's extra plays are refused",
          double(full.refused - before.refused));
    b.frame();
    b.sound.playAt(blast, 0.0f, 0.0f, 0.0f, kHero);
    check(b.sound.tally().stolen == full.stolen + 1 && b.sound.tally().sounding == 24,
          "the hero's own blast takes a crowd voice", double(b.sound.tally().stolen - full.stolen));

    // For the ear: one step every quarter second, walking the screen left to right, in the
    // open town as the game hears it.
    b.settle(mu::game::Sound::Room::Open);
    b.hush();
    std::vector<float> walk;
    struct Walker {
        float x = -11.0f;
    } walker;
    const int step = b.sound.load("player_step_soil", true);
    double leftHalf[2] = {0, 0}, rightHalf[2] = {0, 0};
    for (int block = 0; block < 400; ++block) {  // 4 s in 10 ms blocks
        walker.x = -11.0f + 22.0f * float(block) / 400.0f;
        b.frame();
        if (block % 25 == 0 && step >= 0) b.sound.playAt(step, walker.x, 0.0f, 2.0f, 7);
        b.sound.follow(
            [](void* context, uint32_t, float* x, float* y, float* z) {
                *x = static_cast<Walker*>(context)->x;
                *y = 0.0f;
                *z = 2.0f;
                return true;
            },
            &walker);
        const std::vector<float> got = b.pull(0.01f);
        const Ears e = rms(got);
        double* half = block < 200 ? leftHalf : rightHalf;
        half[0] += e.left;
        half[1] += e.right;
        walk.insert(walk.end(), got.begin(), got.end());
    }
    check(leftHalf[0] > leftHalf[1] && rightHalf[1] > rightHalf[0],
          "a walk across the screen crosses the ears", rightHalf[1] / rightHalf[0]);
    std::string wav = "sound_test.wav";
    if (argc > 0) {
        const std::string self = argv[0];
        const size_t slash = self.find_last_of('/');
        if (slash != std::string::npos) wav = self.substr(0, slash + 1) + wav;
    }
    writeWav(wav, walk);
    std::printf("  the walk is in %s\n", wav.c_str());

    b.sound.shutdown();
    std::printf("sound_test: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
