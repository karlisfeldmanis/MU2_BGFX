#include "content/showing.h"

#include <cstring>

#include "content/reader.h"
#include "core/files.h"

namespace mu::content {
namespace {

constexpr uint32_t kVersion = 1;

// What the cook makes every sound, and what a mixer may therefore assume. MU's own files are
// in nine formats and 47 of the 104 are stereo -- which cannot be panned to a place, so the
// cook downmixes rather than the device resampling per voice at play. See cook_showing.
constexpr uint32_t kSampleRate = 22050;
constexpr uint32_t kChannels = 1;

// The smallest a row can possibly be, for the hostile-count check: two length prefixes for an
// effect, and a prefix plus onset, gain and a file count for an event.
constexpr size_t kLeastEffect = sizeof(uint16_t) * 2;
constexpr size_t kLeastEvent = sizeof(uint16_t) + sizeof(float) * 2 + sizeof(uint32_t);

}  // namespace

bool parseShowing(const std::vector<uint8_t>& bytes, Showing& out, std::string& error) {
    Reader reader(bytes.data(), bytes.size());

    char magic[4] = {};
    uint32_t version = 0, effects = 0, events = 0;
    reader.take(magic, 4);
    reader.read(version);
    reader.read(effects);
    reader.read(events);
    reader.read(out.sampleRate);
    reader.read(out.channels);
    if (reader.failed() || std::memcmp(magic, "MU2S", 4) != 0) {
        error = "not a .mus";
        return false;
    }
    if (version != kVersion) {
        error = "a .mus of version " + std::to_string(version) + ", and this reads " +
                std::to_string(kVersion) + "; recook (tools/cook.py --only showing)";
        return false;
    }
    // Checked and not trusted, for the reason the rules tables check their tick rate: a file
    // cooked to one format and read by a device set up for another is a fight that is quietly
    // the wrong pitch, and nothing about it looks wrong.
    if (out.sampleRate != kSampleRate || out.channels != kChannels) {
        error = "cooked at " + std::to_string(out.sampleRate) + " Hz in " +
                std::to_string(out.channels) + " channel(s), and this reads " +
                std::to_string(kSampleRate) + " Hz mono";
        return false;
    }

    if (!plausible(reader, effects, kLeastEffect)) {
        error = "claims " + std::to_string(effects) + " effects and has no room for them";
        return false;
    }
    out.effects.reserve(effects);
    for (uint32_t i = 0; i < effects; ++i) {
        EffectSheet one;
        reader.readString(one.name);
        reader.readString(one.path);
        if (reader.failed()) {
            error = "ran out of file inside effect " + std::to_string(i);
            return false;
        }
        out.effects.push_back(std::move(one));
    }

    if (!plausible(reader, events, kLeastEvent)) {
        error = "claims " + std::to_string(events) + " sound events and has no room for them";
        return false;
    }
    out.events.reserve(events);
    for (uint32_t i = 0; i < events; ++i) {
        SoundEvent one;
        uint32_t files = 0;
        reader.readString(one.name);
        reader.read(one.onset);
        reader.read(one.gainDb);
        reader.read(files);
        if (reader.failed()) {
            error = "ran out of file inside sound event " + std::to_string(i);
            return false;
        }
        if (!plausible(reader, files, sizeof(uint16_t))) {
            error = one.name + " claims " + std::to_string(files) +
                    " files and has no room for them";
            return false;
        }
        one.files.resize(files);
        for (uint32_t f = 0; f < files; ++f) reader.readString(one.files[f]);
        if (reader.failed()) {
            error = "ran out of file inside sound event " + one.name;
            return false;
        }
        out.events.push_back(std::move(one));
    }
    return true;
}

bool loadShowing(const std::string& path, Showing& out, std::string& error) {
    std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) {
        error = path + " did not read";
        return false;
    }
    return parseShowing(bytes, out, error);
}

}  // namespace mu::content
