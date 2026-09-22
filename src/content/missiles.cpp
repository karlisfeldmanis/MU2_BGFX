#include "content/missiles.h"

#include <cstring>

#include "content/reader.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::content {
namespace {

// tools/cook.py's MISSILE_VERSION. A mismatch is refused rather than defaulted, as the .mum's
// is: every field in a row is a number the flight is drawn from, and a reader that invented
// one would throw the wrong size of rock at the wrong height and look like it had worked.
constexpr uint32_t kVersion = 1;

// The smallest a row can be, for the hostile-count check: two length prefixes, eight floats
// and a part count.
constexpr size_t kLeastRow = sizeof(uint16_t) * 2 + sizeof(float) * 8 + sizeof(uint32_t);
// And the smallest a part can be: a length prefix and the blend byte.
constexpr size_t kLeastPart = sizeof(uint16_t) + sizeof(uint8_t);

}  // namespace

bool Missiles::read(const std::string& path) {
    rows.clear();
    const std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) {
        core::logError("missiles: %s did not read (tools/cook.py --only missiles)",
                       path.c_str());
        return false;
    }

    Reader reader(bytes.data(), bytes.size());
    char magic[4] = {};
    uint32_t version = 0, count = 0;
    reader.take(magic, 4);
    reader.read(version);
    reader.read(count);
    if (reader.failed() || std::memcmp(magic, "MU2P", 4) != 0) {
        core::logError("missiles: %s is not a .mup", path.c_str());
        return false;
    }
    if (version != kVersion) {
        core::logError("missiles: %s is of version %u and this reads %u; recook "
                       "(tools/cook.py --only missiles)", path.c_str(), version, kVersion);
        return false;
    }
    if (!plausible(reader, count, kLeastRow)) {
        core::logError("missiles: %s claims %u missiles and has no room for them",
                       path.c_str(), count);
        return false;
    }

    rows.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        MissileRow one;
        uint32_t parts = 0;
        reader.readString(one.name);
        reader.readString(one.mesh);
        reader.read(one.scale);
        reader.read(one.frames);
        reader.read(one.lift);
        reader.take(one.muzzle, sizeof(one.muzzle));
        reader.read(one.sideways);
        reader.read(one.sidewaysSpread);
        reader.read(parts);
        if (reader.failed()) {
            core::logError("missiles: %s ran out inside missile %u", path.c_str(), i);
            return false;
        }
        if (!plausible(reader, parts, kLeastPart)) {
            core::logError("missiles: %s claims %s has %u parts and has no room for them",
                           path.c_str(), one.name.c_str(), parts);
            return false;
        }
        one.parts.resize(parts);
        for (uint32_t p = 0; p < parts; ++p) {
            uint8_t additive = 0;
            reader.readString(one.parts[p].sheet);
            reader.read(additive);
            one.parts[p].additive = additive != 0;
        }
        if (reader.failed()) {
            core::logError("missiles: %s ran out inside the parts of %s", path.c_str(),
                           one.name.c_str());
            return false;
        }
        rows.push_back(std::move(one));
    }
    return true;
}

}  // namespace mu::content
