#include "game/sound.h"

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

}  // namespace

struct Sound::Impl {
    ma_engine engine{};
    bool open = false;
    float latency = 0.0f;  // seconds between queuing a sample and hearing it
    struct Voice {
        std::string event;
        std::string file;
        ma_sound sound{};
        float lead = 0.0f;  // seconds of silence at its head
        bool ready = false;
    };
    // Pointers, because a ma_sound must not move once initialised.
    std::vector<std::unique_ptr<Voice>> voices;
};

Sound::Sound() : impl_(std::make_unique<Impl>()) {}
Sound::~Sound() { shutdown(); }

bool Sound::isOpen() const { return impl_->open; }

bool Sound::open(const std::string& assetDir, const content::Showing& table,
                 const char* const* wanted, bool muted) {
    shutdown();
    ma_engine_config config = ma_engine_config_init();
    config.periodSizeInMilliseconds = kPeriodMs;
    if (ma_engine_init(&config, &impl_->engine) != MA_SUCCESS) {
        core::logError("sound: no audio device; the game is silent");
        return false;
    }
    impl_->open = true;
    if (muted) ma_engine_set_volume(&impl_->engine, 0.0f);

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

    for (const char* const* name = wanted; name && *name; ++name) {
        const content::SoundEvent* event = table.event(*name);
        if (event == nullptr || event->files.empty()) {
            core::logError("sound: no cooked event '%s'", *name);
            continue;
        }
        // MU picks among an event's files at random; the level-up has one, and so will every
        // event here until a second is wanted. The first is taken.
        const std::string path = assetDir + "/" + event->files.front();
        if (!core::fileExists(path)) {
            core::logError("sound: no %s (tools/cook.py --only showing)", path.c_str());
            continue;
        }
        auto voice = std::make_unique<Impl::Voice>();
        voice->event = *name;
        voice->file = event->files.front();

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
            if (rate > 0 && first < frames) voice->lead = float(first) / float(rate);
            ma_free(pcm, nullptr);
        }

        // Decoded whole now, so the first play is not a file read on the frame it is wanted --
        // the same lesson as every pool in this engine. Not placed: MU plays the level-up with
        // no object, at the ears, whether or not he is on screen.
        const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
        if (ma_sound_init_from_file(&impl_->engine, path.c_str(), flags, nullptr, nullptr,
                                    &voice->sound) != MA_SUCCESS) {
            core::logError("sound: %s would not decode", path.c_str());
            continue;
        }
        voice->ready = true;
        ma_sound_set_volume(&voice->sound, std::pow(10.0f, event->gainDb / 20.0f));
        core::logf("sound: '%s' is %s, %.0f ms of silence at its head, %+.1f dB", *name,
                   voice->file.c_str(), double(voice->lead) * 1000.0, double(event->gainDb));
        impl_->voices.push_back(std::move(voice));
    }
    return true;
}

void Sound::shutdown() {
    if (!impl_ || !impl_->open) return;
    for (auto& voice : impl_->voices) {
        if (voice->ready) ma_sound_uninit(&voice->sound);
    }
    impl_->voices.clear();
    ma_engine_uninit(&impl_->engine);
    impl_->open = false;
}

void Sound::play(const std::string& event) {
    if (!impl_->open) return;
    for (auto& voice : impl_->voices) {
        if (voice->event != event || !voice->ready) continue;
        // Past the silence, and past what the device's buffer will hold it back by: heard a
        // buffer from now, it is heard at the point it would have reached by then.
        const float from = voice->lead + impl_->latency;
        ma_sound_stop(&voice->sound);
        ma_sound_seek_to_second(&voice->sound, from);
        ma_sound_start(&voice->sound);
        core::logf("sound: %s from %.0f ms (%.0f of silence, %.0f of buffer)", event.c_str(),
                   double(from) * 1000.0, double(voice->lead) * 1000.0,
                   double(impl_->latency) * 1000.0);
        return;
    }
}

}  // namespace mu::game
