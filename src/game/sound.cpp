#include "game/sound.h"

#include <cfloat>
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
// 1.0 minimum distance, which is 250 units. Inverse distance past it, which is what miniaudio's
// inverse model with a rolloff of one computes -- the same curve DirectSound's default draws.
// MU2's Sounds.Carry.
constexpr float kCarry = 2.5f;

// How long an ambient takes to come in and go out. **An invention:** MU starts and stops the
// wind dead on the threshold tile, and a buffer stopped mid-wave is a click. A tenth and a half
// is shorter than the step that crosses a doorway and long enough not to be heard as one.
constexpr ma_uint64 kLoopFadeMs = 150;

// How many of one event may sound at once: LoadWaveFile's channel count for every sound in the
// monster family. MU2's Sounds.Voices.
constexpr int kVoices = 2;

}  // namespace

struct Sound::Impl {
    ma_engine engine{};
    bool open = false;
    std::string assetDir;
    const content::Showing* table = nullptr;
    float latency = 0.0f;  // seconds between queuing a sample and hearing it
    uint32_t dice = 0x2545f491u;  // which of an event's files; the drawing's, never the sim's

    // One of an event's files, as a sound on each of the event's voices. The decoded samples
    // are the resource manager's and shared: a file named by three events, as mspider1.wav is
    // by the spider's walk, bite and death, is decoded once.
    struct File {
        std::string path;
        float lead = 0.0f;  // seconds of silence at its head
        ma_sound sound[kVoices]{};
        int ready = 0;  // how many of `sound` were initialised
    };
    struct Event {
        std::string name;
        bool placed = false;
        // Pointers, because a ma_sound must not move once initialised.
        std::vector<std::unique_ptr<File>> files;
        int next = 0;                    // the voice the next play takes
        int sounding[kVoices] = {-1, -1};  // which file each voice last started
        uint32_t following[kVoices] = {0, 0};
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

    // Past the silence, and past what the device's buffer will hold it back by: heard a
    // buffer from now, it is heard at the point it would have reached by then.
    void start(ma_sound& sound, float lead) {
        ma_sound_stop(&sound);
        ma_sound_seek_to_second(&sound, lead + latency);
        ma_sound_start(&sound);
    }
};

Sound::Sound() : impl_(std::make_unique<Impl>()) {}
Sound::~Sound() { shutdown(); }

bool Sound::isOpen() const { return impl_->open; }

bool Sound::open(const std::string& assetDir, const content::Showing& table, bool muted) {
    shutdown();
    ma_engine_config config = ma_engine_config_init();
    config.periodSizeInMilliseconds = kPeriodMs;
    if (ma_engine_init(&config, &impl_->engine) != MA_SUCCESS) {
        core::logError("sound: no audio device; the game is silent");
        return false;
    }
    impl_->open = true;
    impl_->assetDir = assetDir;
    impl_->table = &table;
    if (muted) ma_engine_set_volume(&impl_->engine, 0.0f);
    // The ears stand up straight whatever the camera does: MU builds its turn from the yaw
    // alone, so a camera looking down at 48 degrees does not tip what is heard.
    ma_engine_listener_set_world_up(&impl_->engine, 0, 0.0f, 1.0f, 0.0f);

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
    const float volume = std::pow(10.0f, cooked->gainDb / 20.0f);
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
            ma_uint32 rate = 0;
            if (ma_decoder_init_file(path.c_str(), nullptr, &probe) == MA_SUCCESS) {
                rate = probe.outputSampleRate;
                ma_decoder_uninit(&probe);
            }
            const float* samples = static_cast<const float*>(pcm);
            ma_uint64 first = 0;
            while (first < frames && std::fabs(samples[first]) < kSilence) ++first;
            if (rate > 0 && first < frames) file->lead = float(first) / float(rate);
            ma_free(pcm, nullptr);
        }

        // Decoded whole now, so the first play is not a file read on the frame it is wanted --
        // the same lesson as every pool in this engine. An unplaced event needs one voice: the
        // level-up restarts on itself.
        const int voices = placed ? kVoices : 1;
        const ma_uint32 flags =
            MA_SOUND_FLAG_DECODE | (placed ? 0u : ma_uint32(MA_SOUND_FLAG_NO_SPATIALIZATION));
        for (int v = 0; v < voices; ++v) {
            ma_sound& sound = file->sound[v];
            if (ma_sound_init_from_file(&impl_->engine, path.c_str(), flags, nullptr, nullptr,
                                        &sound) != MA_SUCCESS) {
                break;
            }
            ++file->ready;
            ma_sound_set_volume(&sound, volume);
            if (placed) {
                // Every knob set, including those being set to their own defaults: a voice
                // takes its character from these and a default is a decision nobody made.
                // MU2's Sounds.Made, which has the same table against what DirectSound did.
                ma_sound_set_positioning(&sound, ma_positioning_absolute);
                ma_sound_set_attenuation_model(&sound, ma_attenuation_model_inverse);
                ma_sound_set_min_distance(&sound, kCarry);
                // No cull: MU mixes every attached object however far off, and a hundred
                // metres at this falloff is thirty decibels down, quiet rather than gone.
                ma_sound_set_max_distance(&sound, FLT_MAX);
                ma_sound_set_rolloff(&sound, 1.0f);
                ma_sound_set_min_gain(&sound, 0.0f);
                ma_sound_set_max_gain(&sound, 1.0f);
                // No doppler: the client never calls SetVelocity.
                ma_sound_set_doppler_factor(&sound, 0.0f);
                ma_sound_set_directional_attenuation_factor(&sound, 0.0f);
            }
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
    for (auto& event : impl_->events) {
        for (auto& file : event->files) {
            for (int v = 0; v < file->ready; ++v) ma_sound_uninit(&file->sound[v]);
        }
    }
    impl_->events.clear();
    ma_engine_uninit(&impl_->engine);
    impl_->open = false;
    impl_->table = nullptr;
}

void Sound::play(const std::string& name) {
    if (!impl_->open) return;
    for (auto& event : impl_->events) {
        if (event->name != name || event->placed) continue;
        Impl::File& file = *event->files.front();
        impl_->start(file.sound[0], file.lead);
        ++event->plays;
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

void Sound::playAt(int handle, float x, float z, uint32_t following) {
    if (!impl_->open || handle < 0 || size_t(handle) >= impl_->events.size()) return;
    Impl::Event& event = *impl_->events[size_t(handle)];
    if (!event.placed) return;
    const int voice = event.next;
    event.next = (event.next + 1) % kVoices;

    // The steal: whatever this voice was sounding stops, as DirectSound's round-robin does not
    // wait for a buffer to finish before re-using it.
    if (event.sounding[voice] >= 0) {
        ma_sound_stop(&event.files[size_t(event.sounding[voice])]->sound[voice]);
    }
    const int pick = int(impl_->roll() % uint32_t(event.files.size()));
    Impl::File& file = *event.files[size_t(pick)];
    if (voice >= file.ready) return;
    event.sounding[voice] = pick;
    event.following[voice] = following;
    ma_sound& sound = file.sound[voice];
    // Flat, and placed before it starts, so no voice is ever briefly heard where the last one
    // stood.
    ma_sound_set_position(&sound, x, 0.0f, z);
    impl_->start(sound, file.lead);
    ++event.plays;
}

void Sound::listen(float x, float z, float forwardX, float forwardZ) {
    if (!impl_->open) return;
    const float length = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    ma_engine_listener_set_position(&impl_->engine, 0, x, 0.0f, z);
    if (length > 1e-4f) {
        ma_engine_listener_set_direction(&impl_->engine, 0, forwardX / length, 0.0f,
                                         forwardZ / length);
    }
}

void Sound::follow(Where where, void* context) {
    if (!impl_->open || where == nullptr) return;
    for (auto& event : impl_->events) {
        if (!event->placed) continue;
        for (int v = 0; v < kVoices; ++v) {
            if (event->following[v] == 0 || event->sounding[v] < 0) continue;
            ma_sound& sound = event->files[size_t(event->sounding[v])]->sound[v];
            if (!ma_sound_is_playing(&sound)) {
                event->following[v] = 0;
                continue;
            }
            float x = 0.0f, z = 0.0f;
            if (where(context, event->following[v], &x, &z)) {
                ma_sound_set_position(&sound, x, 0.0f, z);
            }
        }
    }
}

}  // namespace mu::game
