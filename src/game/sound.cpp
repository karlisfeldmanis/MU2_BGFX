#include "game/sound.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <miniaudio.h>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// Where a file's silence ends: the first sample louder than this. -50 dBFS is under anything
// audible over a game and over the dither a WAV's "silence" carries. plevelup.wav's lead
// comes out at 46 ms by it (-60 gives 41, -40 gives 52).
constexpr float kSilence = 0.00316f;  // -50 dBFS

// The device's period. miniaudio's own default is tuned for safety over latency; ten
// milliseconds is what a game on CoreAudio runs at, and it is the latency being corrected for.
constexpr uint32_t kPeriodMs = 10;

// How far a sound carries at full volume, in metres: MU's 0.004 against DirectSound's default
// 1.0 minimum distance, which is 250 units. Inverse distance past it -- the curve DirectSound's
// default draws. MU2's Sounds.Carry.
constexpr float kCarry = 2.5f;

// How long an ambient takes to come in and go out. **An invention:** MU starts and stops the
// wind dead on the threshold tile, and a buffer stopped mid-wave is a click. A tenth and a half
// is shorter than the step that crosses a doorway and long enough not to be heard as one.
constexpr ma_uint64 kLoopFadeMs = 150;

// How many of one event may sound at once: LoadWaveFile's channel count for every sound in the
// monster family. MU2's Sounds.Voices.
constexpr int kVoices = 2;

// --- docs/spatial-sound.md, steps A to D. Every number below is an invention. -------------

// How wide the screen is heard: a thing at the frame's side edge is panned this far and no
// further. Miniaudio's balance pan leaves the far ear at one minus this, -10 dB -- to one side,
// never in one ear only, which on headphones is heard as inside the head rather than beside it.
constexpr float kPanWidth = 0.7f;

// The frame's edge, in clip space's larger of |x| and |y|: full level inside kEdgeFull, silence
// at kEdgeSilent, straight between. Play::emit refuses a new sound past the frustum and a
// metre and a half, which at MU's camera is about the silent line, so nothing starts inaudible
// and nothing that walks out stops dead.
constexpr float kEdgeFull = 0.85f;
constexpr float kEdgeSilent = 1.2f;

// Air: the low-pass is open at kCarry and closes to kDullest by kDull. Most of MU's files are
// 22 kHz, so there is nothing above 11 kHz to take; what is heard going is the 2-6 kHz band,
// which is where "far" lives. Second order, gentle enough not to be heard as a filter.
constexpr float kOpen = 16000.0f;
constexpr float kDullest = 2500.0f;
constexpr float kDull = 20.0f;
// A cutoff is only re-set when it has moved more than this, as a ratio: a biquad re-set is
// cheap but not free, and a monster standing still should not re-set one every frame.
constexpr float kCutoffStep = 0.04f;

// The budget: placed voices at once, over every event. The screen holds thirty monsters and
// each could be two voices; twenty-four is enough for a crowd to read as one and few enough
// that the hero's blow is still heard over it.
constexpr int kVoicesTotal = 24;
// What is near enough to the hero to outrank the crowd: the monsters on him.
constexpr float kNear = 6.0f;
// A play of an event on the same frame as another of it, within this, joins it: its level
// rises as the square root of how many -- +3 dB a doubling, as uncorrelated sources sum --
// up to four.
constexpr float kMergeReach = 3.0f;
constexpr int kMergeMost = 4;

// The level-up's duck: the world and the ambience fall this far, fast, hold, and come back
// slowly. -4.4 dB is a lean rather than a gap.
constexpr float kDuck = 0.6f;
constexpr ma_uint64 kDuckInMs = 50;
constexpr ma_uint64 kDuckHoldMs = 600;
constexpr ma_uint64 kDuckOutMs = 400;

// How often the budget says what it did, in engine milliseconds, when it did anything.
constexpr ma_uint64 kTallyEveryMs = 10000;

// Behind a wall: -6 dB and the filter down to a muffle, eased in and out over a tenth of a
// second so a corner is heard being turned rather than switched. Only the line on the tile
// grid is asked, so a fence muffles as a house does; at -6 dB that is a lean, not a loss.
constexpr float kWalledGain = 0.5f;
constexpr float kMuffled = 1200.0f;
constexpr float kWallEaseMs = 100.0f;

// The rooms, as the reverb's send level and Freeverb's own two knobs. The open town is a slap
// off its walls, mostly dry; under a roof is a small room. Eased over the wind's own 150 ms.
struct Preset {
    float wet;   // the send's return, linear
    float room;  // Freeverb's room size, 0..1
    float damp;  // and its damping, 0..1
};
constexpr Preset kDry = {0.0f, 0.5f, 0.5f};
constexpr Preset kOpenAir = {0.126f, 0.35f, 0.6f};  // -18 dB
constexpr Preset kRoofed = {0.32f, 0.7f, 0.35f};    // -10 dB
constexpr float kRoomEaseMs = 150.0f;

enum Importance { kCrowd = 0, kNearHero = 1, kHero = 2 };

// Freeverb (Jezar at Dreampoint, 2000, public domain): eight damped combs in parallel and four
// allpasses in series, a channel each, the right's lines 23 samples longer so the two ears
// differ. Tuned at 44.1 kHz and scaled to the device's rate. A miniaudio node with one stereo
// input and one stereo output, fed by the world's bus through a splitter; what it returns is
// the wet alone, and the splitter's other branch is the dry.
//
// The two knobs are written by the game's thread and read by the audio thread once a block;
// a float is written whole on every target this builds for, and a block read with the last
// value or the new one is the same to the ear.
struct Reverb {
    ma_node_base base;  // first, so a Reverb* is a ma_node*
    static constexpr int kCombs = 8, kPasses = 4;
    struct Line {
        std::vector<float> buffer;
        size_t at = 0;
        float store = 0.0f;
    };
    Line comb[2][kCombs];
    Line pass[2][kPasses];
    volatile float room = 0.5f;
    volatile float damp = 0.5f;

    void size(ma_uint32 rate) {
        static constexpr int kComb[kCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static constexpr int kPass[kPasses] = {556, 441, 341, 225};
        const float scale = float(rate) / 44100.0f;
        for (int side = 0; side < 2; ++side) {
            const int spread = side == 0 ? 0 : 23;
            for (int i = 0; i < kCombs; ++i) {
                comb[side][i] = Line{};
                comb[side][i].buffer.assign(size_t(float(kComb[i] + spread) * scale), 0.0f);
            }
            for (int i = 0; i < kPasses; ++i) {
                pass[side][i] = Line{};
                pass[side][i].buffer.assign(size_t(float(kPass[i] + spread) * scale), 0.0f);
            }
        }
    }

    static void process(ma_node* node, const float** in, ma_uint32* inCount, float** out,
                        ma_uint32* outCount) {
        Reverb& r = *static_cast<Reverb*>(static_cast<void*>(node));
        const float feedback = r.room * 0.28f + 0.7f;
        const float damp = r.damp * 0.4f;
        const ma_uint32 frames = std::min(*inCount, *outCount);
        const float* src = in[0];
        float* dst = out[0];
        for (ma_uint32 f = 0; f < frames; ++f) {
            const float input = (src[f * 2] + src[f * 2 + 1]) * 0.015f;
            for (int side = 0; side < 2; ++side) {
                float sum = 0.0f;
                for (Line& c : r.comb[side]) {
                    const float held = c.buffer[c.at];
                    c.store = held * (1.0f - damp) + c.store * damp;
                    c.buffer[c.at] = input + c.store * feedback;
                    if (++c.at == c.buffer.size()) c.at = 0;
                    sum += held;
                }
                for (Line& p : r.pass[side]) {
                    const float held = p.buffer[p.at];
                    p.buffer[p.at] = sum + held * 0.5f;
                    if (++p.at == p.buffer.size()) p.at = 0;
                    sum = held - sum;
                }
                dst[f * 2 + side] = sum;
            }
        }
        *outCount = frames;
    }
};

ma_node_vtable kReverbTable = {Reverb::process, nullptr, 1, 1, 0};

}  // namespace

struct Sound::Impl {
    ma_engine engine{};
    bool open = false;
    bool offline = false;
    std::string assetDir;
    const content::Showing* table = nullptr;
    float latency = 0.0f;  // seconds between queuing a sample and hearing it
    uint32_t dice = 0x2545f491u;  // which of an event's files; the drawing's, never the sim's

    // The buses: the interface, the placed world, and the ambience. Everything placed goes
    // through its own low-pass into `world`.
    ma_sound_group ui{}, world{}, ambience{};
    bool groups = false;

    // The room: the world's bus split into the dry and the reverb's send.
    ma_splitter_node split{};
    Reverb reverb{};
    bool roomed = false;
    Preset roomNow = kOpenAir, roomWanted = kOpenAir;
    ma_uint64 roomAt = 0;  // engine ms the ease last stepped

    // The walls, as the caller answers them.
    Sound::Clear clear = nullptr;
    void* clearContext = nullptr;
    ma_uint64 mixedAt = 0;  // engine ms follow() last eased the walls

    // The ears, as listen() last said.
    uint32_t hero = 0;
    float ear[3] = {0.0f, 0.0f, 0.0f};
    float shot[16] = {};
    bool shotKnown = false;
    uint64_t frame = 0;
    ma_uint64 duckEnds = 0;  // engine ms the duck lets go at; 0 for none

    Tally counted;
    Tally lastSaid;
    int peak = 0;
    ma_uint64 saidAt = 0;

    // One of an event's files, as a sound on each of the event's voices. The decoded samples
    // are the resource manager's and shared: a file named by three events, as mspider1.wav is
    // by the spider's walk, bite and death, is decoded once.
    struct File {
        std::string path;
        float lead = 0.0f;  // seconds of silence at its head
        ma_sound sound[kVoices]{};
        ma_lpf_node air[kVoices]{};
        float cutoff[kVoices] = {0.0f, 0.0f};
        ma_uint32 channels = 0;  // the sound's output, which the filter must match
        int ready = 0;           // how many of `sound` were initialised
        int filtered = 0;        // and of `air`
    };
    struct Event {
        std::string name;
        bool placed = false;
        float volume = 1.0f;  // the cooked trim
        // Pointers, because a ma_sound must not move once initialised.
        std::vector<std::unique_ptr<File>> files;
        int next = 0;                    // the voice the next play takes
        int sounding[kVoices] = {-1, -1};  // which file each voice last started
        uint32_t following[kVoices] = {0, 0};
        // Each voice's place and standing, for the pan, the budget and the merge.
        float at[kVoices][3] = {};
        uint64_t startedOn[kVoices] = {0, 0};  // the frame
        int merged[kVoices] = {1, 1};           // plays it stands for
        int importance[kVoices] = {kCrowd, kCrowd};
        float gain[kVoices] = {0.0f, 0.0f};     // what mix() last set, before the trim
        float walled[kVoices] = {0.0f, 0.0f};   // 0 clear to 1 behind a wall, eased
        int plays = 0;  // for the log at shutdown: what a run was heard to say
        bool looping = false;  // an ambient that is on; see loop()
    };
    std::vector<std::unique_ptr<Event>> events;

    uint32_t roll() {
        dice ^= dice << 13;
        dice ^= dice >> 17;
        dice ^= dice << 5;
        return dice;
    }

    // Past the silence, and -- for the level-up alone -- past what the device's buffer will
    // hold it back by, so its swell is where the flares are. Not for anything else: MU's own
    // attack sounds put their impact in the first tens of milliseconds, and skipping a 30 ms
    // buffer took 31% of eMeleeHit1's energy and 61% of pWalk_Soil's, so one hit in four and
    // every soil step came out as a click. Heard a buffer late, as DirectSound heard them.
    void start(ma_sound& sound, float lead, bool buffered = false) {
        ma_sound_stop(&sound);
        ma_sound_seek_to_second(&sound, lead + (buffered ? latency : 0.0f));
        ma_sound_start(&sound);
    }

    bool playing(const Event& event, int v) {
        if (event.sounding[v] < 0) return false;
        ma_sound& sound = event.files[size_t(event.sounding[v])]->sound[v];
        return ma_sound_is_playing(&sound) && !ma_sound_at_end(&sound);
    }

    // What a point is worth to the ear: its level (distance and the frame's edge), its pan,
    // its filter and its importance.
    struct Weight {
        float gain = 1.0f;
        float pan = 0.0f;
        float cutoff = kOpen;
        int importance = kCrowd;
    };
    Weight weigh(const float at[3], uint32_t following) const {
        Weight w;
        const float dx = at[0] - ear[0];
        const float dz = at[2] - ear[2];
        // Flat, as MU's is: the loudness is the character's, and he hears along the ground.
        const float d = std::sqrt(dx * dx + dz * dz);
        w.gain = kCarry / std::max(d, kCarry);
        if (shotKnown) {
            // bx's row-vector order: clip = [x y z 1] * shot.
            const float* m = shot;
            const float cx = at[0] * m[0] + at[1] * m[4] + at[2] * m[8] + m[12];
            const float cy = at[0] * m[1] + at[1] * m[5] + at[2] * m[9] + m[13];
            const float cw = at[0] * m[3] + at[1] * m[7] + at[2] * m[11] + m[15];
            if (cw <= 1e-4f) {
                w.gain = 0.0f;  // behind the camera: not on the screen at all
            } else {
                const float nx = cx / cw, ny = cy / cw;
                const float edge = std::max(std::fabs(nx), std::fabs(ny));
                w.gain *= std::clamp((kEdgeSilent - edge) / (kEdgeSilent - kEdgeFull), 0.0f,
                                     1.0f);
                w.pan = std::clamp(nx, -1.0f, 1.0f) * kPanWidth;
            }
        }
        const float t = std::clamp((d - kCarry) / (kDull - kCarry), 0.0f, 1.0f);
        w.cutoff = std::exp(std::log(kOpen) + (std::log(kDullest) - std::log(kOpen)) * t);
        // The hero's own, and what sounds where he stands -- the coins he picks up.
        if ((following != 0 && following == hero) || d < 1.0f) {
            w.importance = kHero;
        } else if (d <= kNear) {
            w.importance = kNearHero;
        }
        return w;
    }

    // Whether a point is behind a wall from the ears: 1 or 0, or 0 when nobody answers.
    float walledAt(const float at[3]) const {
        if (clear == nullptr) return 0.0f;
        return clear(clearContext, ear, at) ? 0.0f : 1.0f;
    }

    // Sets one voice's pan, level and filter from where it is now.
    void mix(Event& event, int v) {
        if (event.sounding[v] < 0) return;
        File& file = *event.files[size_t(event.sounding[v])];
        const Weight w = weigh(event.at[v], event.following[v]);
        const float walled = event.walled[v];
        event.gain[v] = w.gain * (1.0f + (kWalledGain - 1.0f) * walled);
        event.importance[v] = w.importance;
        ma_sound& sound = file.sound[v];
        ma_sound_set_pan(&sound, w.pan);
        ma_sound_set_volume(&sound,
                            event.volume * std::sqrt(float(event.merged[v])) * event.gain[v]);
        if (v < file.filtered) {
            const float rate = float(ma_engine_get_sample_rate(&engine));
            // Behind a wall the filter closes toward the muffle, in the log of the frequency.
            const float open = std::min(w.cutoff, rate * 0.45f);
            const float cutoff =
                std::exp(std::log(open) +
                         (std::log(std::min(kMuffled, open)) - std::log(open)) * walled);
            if (std::fabs(std::log(cutoff / file.cutoff[v])) > kCutoffStep) {
                const ma_lpf_config config =
                    ma_lpf_config_init(ma_format_f32, file.channels, ma_uint32(rate), cutoff, 2);
                ma_lpf_node_reinit(&config, &file.air[v]);
                file.cutoff[v] = cutoff;
            }
        }
    }

    void duck() {
        if (!groups) return;
        ma_sound_group_set_fade_in_milliseconds(&world, -1.0f, kDuck, kDuckInMs);
        ma_sound_group_set_fade_in_milliseconds(&ambience, -1.0f, kDuck, kDuckInMs);
        duckEnds = ma_engine_get_time_in_milliseconds(&engine) + kDuckHoldMs;
    }

    void setRoom(const Preset& p) {
        roomNow = p;
        if (!roomed) return;
        reverb.room = p.room;
        reverb.damp = p.damp;
        ma_node_set_output_bus_volume(&reverb, 0, p.wet);
    }

    int sounding() {
        int count = 0;
        for (auto& event : events) {
            if (!event->placed) continue;
            for (int v = 0; v < kVoices; ++v) count += playing(*event, v) ? 1 : 0;
        }
        return count;
    }
};

Sound::Sound() : impl_(std::make_unique<Impl>()) {}
Sound::~Sound() { shutdown(); }

bool Sound::isOpen() const { return impl_->open; }

bool Sound::open(const std::string& assetDir, const content::Showing& table, bool muted,
                 bool offline) {
    shutdown();
    ma_engine_config config = ma_engine_config_init();
    config.periodSizeInMilliseconds = kPeriodMs;
    if (offline) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = 48000;
    }
    if (ma_engine_init(&config, &impl_->engine) != MA_SUCCESS) {
        core::logError("sound: no audio device; the game is silent");
        return false;
    }
    impl_->open = true;
    impl_->offline = offline;
    impl_->assetDir = assetDir;
    impl_->table = &table;
    impl_->counted = impl_->lastSaid = Tally{};
    impl_->peak = 0;
    impl_->saidAt = 0;
    impl_->duckEnds = 0;
    impl_->shotKnown = false;
    if (muted) ma_engine_set_volume(&impl_->engine, 0.0f);

    impl_->groups =
        ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->ui) == MA_SUCCESS &&
        ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->world) == MA_SUCCESS &&
        ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->ambience) == MA_SUCCESS;
    if (!impl_->groups) core::logError("sound: no buses; everything mixes straight to the end");

    // The room: world -> splitter, whose first branch is the dry and whose second is the
    // reverb, returning the wet alone at the preset's level.
    impl_->roomed = false;
    if (impl_->groups) {
        ma_node_graph* graph = ma_engine_get_node_graph(&impl_->engine);
        ma_node* end = ma_engine_get_endpoint(&impl_->engine);
        const ma_uint32 channels = ma_engine_get_channels(&impl_->engine);
        const ma_splitter_node_config split = ma_splitter_node_config_init(channels);
        ma_node_config node = ma_node_config_init();
        node.vtable = &kReverbTable;
        const ma_uint32 stereo[1] = {2};
        node.pInputChannels = stereo;
        node.pOutputChannels = stereo;
        impl_->reverb.size(ma_engine_get_sample_rate(&impl_->engine));
        if (channels == 2 &&
            ma_splitter_node_init(graph, &split, nullptr, &impl_->split) == MA_SUCCESS) {
            if (ma_node_init(graph, &node, nullptr, &impl_->reverb) == MA_SUCCESS) {
                ma_node_attach_output_bus(&impl_->world, 0, &impl_->split, 0);
                ma_node_attach_output_bus(&impl_->split, 0, end, 0);
                ma_node_attach_output_bus(&impl_->split, 1, &impl_->reverb, 0);
                ma_node_attach_output_bus(&impl_->reverb, 0, end, 0);
                impl_->roomed = true;
            } else {
                ma_splitter_node_uninit(&impl_->split, nullptr);
            }
        }
        if (!impl_->roomed) core::logError("sound: no reverb; the world is dry");
    }
    impl_->roomWanted = kOpenAir;
    impl_->setRoom(kOpenAir);
    impl_->roomAt = impl_->mixedAt = 0;

    // What the device actually took, which is not always what was asked: the buffer is its
    // period times its periods, and that is how long a sample queued now waits to be heard.
    if (const ma_device* device = ma_engine_get_device(&impl_->engine)) {
        const float rate = float(device->playback.internalSampleRate);
        if (rate > 0.0f) {
            impl_->latency = float(device->playback.internalPeriodSizeInFrames) *
                             float(device->playback.internalPeriods) / rate;
        }
        core::logf("sound: %s at %u Hz, %u x %u frames, %.1f ms to the ear%s",
                   device->playback.name, device->playback.internalSampleRate,
                   device->playback.internalPeriods, device->playback.internalPeriodSizeInFrames,
                   double(impl_->latency) * 1000.0, muted ? ", muted" : "");
    } else {
        impl_->latency = 0.0f;
        core::logf("sound: offline at %u Hz", ma_engine_get_sample_rate(&impl_->engine));
    }
    return true;
}

int Sound::load(const std::string& name, bool placed, bool quietly) {
    if (!impl_->open || impl_->table == nullptr) return -1;
    for (size_t i = 0; i < impl_->events.size(); ++i) {
        if (impl_->events[i]->name == name) return int(i);
    }
    const content::SoundEvent* cooked = impl_->table->event(name);
    if (cooked == nullptr || cooked->files.empty()) {
        if (!quietly) core::logError("sound: no cooked event '%s'", name.c_str());
        return -1;
    }
    auto event = std::make_unique<Impl::Event>();
    event->name = name;
    event->placed = placed;
    event->volume = std::pow(10.0f, cooked->gainDb / 20.0f);
    ma_node_graph* graph = ma_engine_get_node_graph(&impl_->engine);
    const ma_uint32 rate = ma_engine_get_sample_rate(&impl_->engine);
    for (const std::string& relative : cooked->files) {
        const std::string path = impl_->assetDir + "/" + relative;
        if (!core::fileExists(path)) {
            core::logError("sound: no %s (tools/cook.py --only showing)", path.c_str());
            continue;
        }
        auto file = std::make_unique<Impl::File>();
        file->path = relative;

        // The lead, from the samples themselves: decoded once as mono float and scanned.
        ma_decoder_config decode = ma_decoder_config_init(ma_format_f32, 1, 0);
        ma_uint64 frames = 0;
        void* pcm = nullptr;
        if (ma_decode_file(path.c_str(), &decode, &frames, &pcm) == MA_SUCCESS && pcm) {
            ma_decoder probe;
            ma_uint32 fileRate = 0;
            if (ma_decoder_init_file(path.c_str(), nullptr, &probe) == MA_SUCCESS) {
                fileRate = probe.outputSampleRate;
                ma_decoder_uninit(&probe);
            }
            const float* samples = static_cast<const float*>(pcm);
            ma_uint64 first = 0;
            while (first < frames && std::fabs(samples[first]) < kSilence) ++first;
            if (fileRate > 0 && first < frames) file->lead = float(first) / float(fileRate);
            ma_free(pcm, nullptr);
        }

        // Decoded whole now, so the first play is not a file read on the frame it is wanted --
        // the same lesson as every pool in this engine. An unplaced event needs one voice: the
        // level-up restarts on itself.
        //
        // No voice is spatialised by miniaudio: a placed one is panned and levelled by mix()
        // from the screen, and an unplaced one is at the ears.
        const int voices = placed ? kVoices : 1;
        const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
        ma_sound_group* bus = impl_->groups && !placed ? &impl_->ui : nullptr;
        for (int v = 0; v < voices; ++v) {
            ma_sound& sound = file->sound[v];
            if (ma_sound_init_from_file(&impl_->engine, path.c_str(), flags, bus, nullptr,
                                        &sound) != MA_SUCCESS) {
                break;
            }
            ++file->ready;
            ma_sound_set_volume(&sound, event->volume);
            if (!placed) continue;
            // Its own air: the sound into its low-pass, the low-pass into the world's bus.
            // Started open; mix() closes it by distance.
            file->channels = ma_node_get_output_channels(&sound, 0);
            const float open = std::min(kOpen, float(rate) * 0.45f);
            const ma_lpf_node_config air =
                ma_lpf_node_config_init(file->channels, rate, open, 2);
            if (ma_lpf_node_init(graph, &air, nullptr, &file->air[v]) != MA_SUCCESS) continue;
            ++file->filtered;
            file->cutoff[v] = open;
            ma_node_attach_output_bus(&sound, 0, &file->air[v], 0);
            ma_node_attach_output_bus(&file->air[v], 0,
                                      impl_->groups ? static_cast<ma_node*>(&impl_->world)
                                                    : ma_engine_get_endpoint(&impl_->engine),
                                      0);
        }
        if (file->ready == 0) {
            core::logError("sound: %s would not decode", path.c_str());
            continue;
        }
        event->files.push_back(std::move(file));
    }
    if (event->files.empty()) return -1;
    core::logf("sound: '%s' is %zu file%s from %s, %.0f ms of silence at its head, %+.1f dB%s",
               name.c_str(), event->files.size(), event->files.size() == 1 ? "" : "s",
               event->files.front()->path.c_str(), double(event->files.front()->lead) * 1000.0,
               double(cooked->gainDb), placed ? ", placed" : "");
    impl_->events.push_back(std::move(event));
    return int(impl_->events.size() - 1);
}

void Sound::shutdown() {
    if (!impl_ || !impl_->open) return;
    std::string heard;
    for (auto& event : impl_->events) {
        if (event->plays == 0) continue;
        heard += (heard.empty() ? "" : ", ") + event->name + " x" + std::to_string(event->plays);
    }
    core::logf("sound: heard %s", heard.empty() ? "nothing" : heard.c_str());
    const Tally& t = impl_->counted;
    core::logf("sound: the budget refused %d, merged %d and stole %d; %d of %d at the most",
               t.refused, t.merged, t.stolen, impl_->peak, kVoicesTotal);
    // Sounds before the filters they feed, filters before the buses, buses before the room,
    // the room before the engine.
    for (auto& event : impl_->events) {
        for (auto& file : event->files) {
            for (int v = 0; v < file->ready; ++v) ma_sound_uninit(&file->sound[v]);
            for (int v = 0; v < file->filtered; ++v) ma_lpf_node_uninit(&file->air[v], nullptr);
        }
    }
    impl_->events.clear();
    if (impl_->groups) {
        ma_sound_group_uninit(&impl_->ui);
        ma_sound_group_uninit(&impl_->world);
        ma_sound_group_uninit(&impl_->ambience);
        impl_->groups = false;
    }
    if (impl_->roomed) {
        ma_splitter_node_uninit(&impl_->split, nullptr);
        ma_node_uninit(&impl_->reverb, nullptr);
        impl_->roomed = false;
    }
    impl_->clear = nullptr;
    impl_->clearContext = nullptr;
    ma_engine_uninit(&impl_->engine);
    impl_->open = false;
    impl_->table = nullptr;
}

void Sound::play(const std::string& name) {
    if (!impl_->open) return;
    for (auto& event : impl_->events) {
        if (event->name != name || event->placed) continue;
        Impl::File& file = *event->files.front();
        impl_->start(file.sound[0], file.lead, true);
        ++event->plays;
        // The duck: the world leans back under the level-up and comes in again after it.
        impl_->duck();
        core::logf("sound: %s from %.0f ms (%.0f of silence, %.0f of buffer)", name.c_str(),
                   double(file.lead + impl_->latency) * 1000.0, double(file.lead) * 1000.0,
                   double(impl_->latency) * 1000.0);
        return;
    }
}

void Sound::play(int handle) {
    if (!impl_->open || handle < 0 || size_t(handle) >= impl_->events.size()) return;
    Impl::Event& event = *impl_->events[size_t(handle)];
    if (event.placed) return;
    const int pick = int(impl_->roll() % uint32_t(event.files.size()));
    Impl::File& file = *event.files[size_t(pick)];
    // One voice: the previous press is cut off by this one, as a button's is.
    for (auto& other : event.files) ma_sound_stop(&other->sound[0]);
    impl_->start(file.sound[0], file.lead);
    ++event.plays;
}

void Sound::loop(int handle, bool wanted) {
    if (!impl_->open || handle < 0 || size_t(handle) >= impl_->events.size()) return;
    Impl::Event& event = *impl_->events[size_t(handle)];
    if (event.placed || wanted == event.looping) return;
    event.looping = wanted;
    // An ambient is never one of the one-shots, so marking its sound looping here cannot leave
    // a swing ringing for ever: the two share the loader and nothing else.
    ma_sound& sound = event.files.front()->sound[0];
    if (wanted) {
        // Off the interface's bus and onto the ambience's, which the level-up ducks.
        if (impl_->groups) ma_node_attach_output_bus(&sound, 0, &impl_->ambience, 0);
        ma_sound_set_looping(&sound, MA_TRUE);
        // A stop still fading out from the last doorway is called off, or it would end the
        // wind a moment after it came back.
        ma_sound_set_stop_time_in_pcm_frames(&sound, ~ma_uint64(0));
        ma_sound_set_fade_in_milliseconds(&sound, 0.0f, 1.0f, kLoopFadeMs);
        ma_sound_start(&sound);
        ++event.plays;
    } else {
        ma_sound_stop_with_fade_in_milliseconds(&sound, kLoopFadeMs);
    }
}

void Sound::playAt(int handle, float x, float y, float z, uint32_t following) {
    if (!impl_->open || handle < 0 || size_t(handle) >= impl_->events.size()) return;
    Impl& im = *impl_;
    Impl::Event& event = *im.events[size_t(handle)];
    if (!event.placed) return;
    const float at[3] = {x, y, z};

    // The merge: this event already started on this frame, near here, is this play too.
    for (int v = 0; v < kVoices; ++v) {
        if (event.startedOn[v] != im.frame || !im.playing(event, v)) continue;
        const float dx = event.at[v][0] - x, dz = event.at[v][2] - z;
        if (dx * dx + dz * dz > kMergeReach * kMergeReach) continue;
        if (event.merged[v] < kMergeMost) ++event.merged[v];
        im.mix(event, v);
        ++im.counted.merged;
        ++event.plays;
        return;
    }

    const Impl::Weight weight = im.weigh(at, following);
    if (weight.gain <= 0.0f) {
        ++im.counted.refused;
        return;
    }
    const int voice = event.next;

    // The budget. The event's own round-robin below steals within the event and does not
    // change the count; only a voice that is not already sounding needs a place.
    if (!im.playing(event, voice) && im.sounding() >= kVoicesTotal) {
        Impl::Event* victim = nullptr;
        int victimVoice = -1;
        for (auto& other : im.events) {
            if (!other->placed) continue;
            for (int v = 0; v < kVoices; ++v) {
                if (!im.playing(*other, v)) continue;
                if (victim == nullptr || other->importance[v] < victim->importance[victimVoice] ||
                    (other->importance[v] == victim->importance[victimVoice] &&
                     other->gain[v] < victim->gain[victimVoice])) {
                    victim = other.get();
                    victimVoice = v;
                }
            }
        }
        const bool outranks =
            victim != nullptr &&
            (weight.importance > victim->importance[victimVoice] ||
             (weight.importance == victim->importance[victimVoice] &&
              weight.gain > victim->gain[victimVoice]));
        if (!outranks) {
            ++im.counted.refused;
            return;
        }
        ma_sound_stop(
            &victim->files[size_t(victim->sounding[victimVoice])]->sound[victimVoice]);
        victim->following[victimVoice] = 0;
        ++im.counted.stolen;
    }
    event.next = (event.next + 1) % kVoices;

    // The steal: whatever this voice was sounding stops, as DirectSound's round-robin does not
    // wait for a buffer to finish before re-using it.
    if (event.sounding[voice] >= 0) {
        ma_sound_stop(&event.files[size_t(event.sounding[voice])]->sound[voice]);
    }
    const int pick = int(im.roll() % uint32_t(event.files.size()));
    Impl::File& file = *event.files[size_t(pick)];
    if (voice >= file.ready) return;
    event.sounding[voice] = pick;
    event.following[voice] = following;
    event.at[voice][0] = x;
    event.at[voice][1] = y;
    event.at[voice][2] = z;
    event.startedOn[voice] = im.frame;
    event.merged[voice] = 1;
    // Behind a wall from its first sample, not eased into one.
    event.walled[voice] = im.walledAt(at);
    // Levelled, panned and filtered before it starts, so no voice is ever briefly heard as
    // the last one was.
    im.mix(event, voice);
    im.start(file.sound[voice], file.lead);
    ++event.plays;
    im.peak = std::max(im.peak, im.sounding());
}

void Sound::listen(uint32_t hero, const float at[3], const float shot[16]) {
    if (!impl_->open) return;
    Impl& im = *impl_;
    im.hero = hero;
    for (int i = 0; i < 3; ++i) im.ear[i] = at[i];
    for (int i = 0; i < 16; ++i) im.shot[i] = shot[i];
    im.shotKnown = true;
    ++im.frame;

    const ma_uint64 now = ma_engine_get_time_in_milliseconds(&im.engine);
    // The room eases toward the one wanted.
    if (im.roomAt == 0) im.roomAt = now;
    {
        const float step = std::min(1.0f, float(now - im.roomAt) / kRoomEaseMs);
        im.roomAt = now;
        const Preset& to = im.roomWanted;
        const Preset& was = im.roomNow;
        im.setRoom({was.wet + (to.wet - was.wet) * step, was.room + (to.room - was.room) * step,
                    was.damp + (to.damp - was.damp) * step});
    }
    if (im.duckEnds != 0 && now >= im.duckEnds) {
        ma_sound_group_set_fade_in_milliseconds(&im.world, -1.0f, 1.0f, kDuckOutMs);
        ma_sound_group_set_fade_in_milliseconds(&im.ambience, -1.0f, 1.0f, kDuckOutMs);
        im.duckEnds = 0;
    }
    // What the budget did lately, said only when it did something.
    if (now >= im.saidAt + kTallyEveryMs) {
        const Tally& t = im.counted;
        const Tally& was = im.lastSaid;
        if (t.refused != was.refused || t.merged != was.merged || t.stolen != was.stolen) {
            core::logf("sound: %d of %d voices at the most; %d refused, %d merged, %d stolen "
                       "in the last %llu s",
                       im.peak, kVoicesTotal, t.refused - was.refused, t.merged - was.merged,
                       t.stolen - was.stolen,
                       static_cast<unsigned long long>(kTallyEveryMs / 1000));
            im.lastSaid = t;
        }
        im.saidAt = now;
    }
}

void Sound::follow(Where where, void* context) {
    if (!impl_->open) return;
    const ma_uint64 now = ma_engine_get_time_in_milliseconds(&impl_->engine);
    const float ease = impl_->mixedAt == 0
                           ? 1.0f
                           : std::min(1.0f, float(now - impl_->mixedAt) / kWallEaseMs);
    impl_->mixedAt = now;
    for (auto& event : impl_->events) {
        if (!event->placed) continue;
        for (int v = 0; v < kVoices; ++v) {
            if (!impl_->playing(*event, v)) {
                event->following[v] = 0;
                continue;
            }
            if (where != nullptr && event->following[v] != 0) {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (where(context, event->following[v], &x, &y, &z)) {
                    event->at[v][0] = x;
                    event->at[v][1] = y;
                    event->at[v][2] = z;
                }
            }
            const float walled = impl_->walledAt(event->at[v]);
            event->walled[v] += (walled - event->walled[v]) * ease;
            impl_->mix(*event, v);
        }
    }
}

void Sound::duck() {
    if (impl_->open) impl_->duck();
}

void Sound::room(Room which) {
    impl_->roomWanted = which == Room::Dry ? kDry : which == Room::Roofed ? kRoofed : kOpenAir;
}

void Sound::walls(Clear clear, void* context) {
    impl_->clear = clear;
    impl_->clearContext = context;
}

uint64_t Sound::render(float* out, uint64_t frames) {
    if (!impl_->open || !impl_->offline) return 0;
    ma_uint64 read = 0;
    if (ma_engine_read_pcm_frames(&impl_->engine, out, frames, &read) != MA_SUCCESS) return 0;
    return read;
}

Sound::Tally Sound::tally() const {
    Tally t = impl_->counted;
    t.sounding = impl_->open ? impl_->sounding() : 0;
    return t;
}

}  // namespace mu::game
