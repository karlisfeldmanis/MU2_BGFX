#include "content/tables.h"

#include <cstring>

#include "content/reader.h"
#include "core/files.h"

namespace mu::content {
namespace {

// The rate the cook converted every delay at. A table cooked at one rate and read by a sim
// running at another is a fight that is quietly the wrong speed, and nothing about it looks
// wrong -- so it is checked rather than trusted. docs/conventions.md, "Time".
constexpr uint32_t kSimHz = 20;

// Version 2 added the arms and version 3 the attack actions a swing rate is made of. There is no version 1 anywhere but in a stale build directory, and
// the reader says so rather than reading a file whose fields have moved under it.
constexpr uint32_t kVersion = 3;

}  // namespace

bool parseTables(const std::vector<uint8_t>& bytes, Tables& out, std::string& error) {
    Reader reader(bytes.data(), bytes.size());

    char magic[4] = {};
    uint32_t version = 0, kinds = 0, nests = 0, arms = 0, actions = 0, size = 0;
    reader.take(magic, 4);
    reader.read(version);
    reader.read(out.hz);
    reader.read(kinds);
    reader.read(nests);
    reader.read(arms);
    reader.read(actions);
    reader.read(out.map);
    reader.read(size);
    reader.take(out.safeGate, sizeof(out.safeGate));
    if (reader.failed() || std::memcmp(magic, "MU2R", 4) != 0) {
        error = "not a .mur";
        return false;
    }
    if (version != kVersion) {
        error = "a .mur of version " + std::to_string(version) + ", and this reads " +
                std::to_string(kVersion) + "; recook (tools/cook.py --only tables)";
        return false;
    }
    if (out.hz != kSimHz) {
        error = "cooked for " + std::to_string(out.hz) + " Hz and this sim ticks at " +
                std::to_string(kSimHz);
        return false;
    }

    out.kinds.clear();
    out.kinds.reserve(kinds);
    for (uint32_t i = 0; i < kinds; ++i) {
        MonsterKind kind;
        reader.readString(kind.figure);
        reader.readString(kind.label);
        int32_t fields[15] = {};
        reader.take(fields, sizeof(fields));
        reader.read(kind.scale);
        if (reader.failed()) {
            error = "ran out of file inside breed " + std::to_string(i);
            return false;
        }
        kind.number = fields[0];
        kind.level = fields[1];
        kind.health = fields[2];
        kind.minimumDamage = fields[3];
        kind.maximumDamage = fields[4];
        kind.defense = fields[5];
        kind.moveRange = fields[6];
        kind.attackRange = fields[7];
        kind.viewRange = fields[8];
        kind.moveTicks = fields[9];
        kind.attackTicks = fields[10];
        kind.respawnTicks = fields[11];
        kind.attackRate = fields[12];
        kind.defenseRate = fields[13];
        kind.attackSkill = fields[14];
        out.kinds.push_back(std::move(kind));
    }

    if (!plausible(reader, nests, 24)) {
        error = "claims " + std::to_string(nests) + " nests and has no room for them";
        return false;
    }
    out.nests.clear();
    out.nests.reserve(nests);
    for (uint32_t i = 0; i < nests; ++i) {
        MonsterNest nest;
        reader.read(nest.kind);
        reader.read(nest.x1);
        reader.read(nest.x2);
        reader.read(nest.y1);
        reader.read(nest.y2);
        reader.read(nest.count);
        if (reader.failed()) {
            error = "ran out of file inside nest " + std::to_string(i);
            return false;
        }
        if (nest.kind >= out.kinds.size()) {
            error = "nest " + std::to_string(i) + " names breed " + std::to_string(nest.kind) +
                    " and there are " + std::to_string(out.kinds.size());
            return false;
        }
        out.nests.push_back(nest);
    }

    out.arms.clear();
    out.arms.reserve(arms);
    for (uint32_t i = 0; i < arms; ++i) {
        Arm arm;
        reader.readString(arm.name);
        reader.readString(arm.label);
        reader.readString(arm.stance);
        int32_t fields[11] = {};
        reader.take(fields, sizeof(fields));
        if (reader.failed()) {
            error = "ran out of file inside arm " + std::to_string(i);
            return false;
        }
        arm.kind = fields[0];
        arm.minimumDamage = fields[1];
        arm.maximumDamage = fields[2];
        arm.attackSpeed = fields[3];
        arm.defense = fields[4];
        arm.wantsStrength = fields[5];
        arm.wantsAgility = fields[6];
        arm.classes = fields[7];
        arm.group = fields[8];
        arm.number = fields[9];
        arm.flags = fields[10];
        out.arms.push_back(std::move(arm));
    }

    if (!plausible(reader, actions, 12)) {
        error = "claims " + std::to_string(actions) + " attack actions and has no room for them";
        return false;
    }
    out.actions.clear();
    out.actions.resize(actions);
    for (PlayerAction& one : out.actions) {
        reader.read(one.action);
        reader.read(one.keys);
        reader.read(one.speed);
    }
    if (reader.failed()) {
        error = "ran out of file inside the attack actions";
        return false;
    }

    if (size == 0 || !plausible(reader, size * size, sizeof(uint16_t))) {
        error = "claims a grid " + std::to_string(size) + " tiles a side and has no room for it";
        return false;
    }
    std::vector<uint16_t> words(size_t(size) * size_t(size));
    reader.take(words.data(), words.size() * sizeof(uint16_t));
    if (reader.failed()) {
        error = "ran out of file inside the attribute grid";
        return false;
    }
    out.grid.set(int(size), std::move(words));
    if (out.grid.empty()) {
        error = "the attribute grid did not take";
        return false;
    }
    return true;
}

bool loadTables(const std::string& path, Tables& out, std::string& error) {
    std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) {
        error = path + " did not read";
        return false;
    }
    return parseTables(bytes, out, error);
}

}  // namespace mu::content
