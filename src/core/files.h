#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::core {

// Another source of files, asked before the disk: the player's packs (core/pack.h), which set
// it when they are mounted. Null, the default and the developer's build, is the disk alone.
// A hook rather than a call so core stays free of zstd, and the server of the packs.
struct FileSource {
    bool (*read)(const std::string& path, std::vector<uint8_t>& out);
    bool (*has)(const std::string& path);
    std::vector<std::string> (*list)(const std::string& dir);  // sorted leaves
};
void setFileSource(const FileSource* source);

// Reads a whole file. Empty on failure, and the failure is in the log. A path the file source
// holds is read from it; every other from the disk.
std::vector<uint8_t> readFile(const std::string& path);

bool fileExists(const std::string& path);

// The leaves of the files directly in `dir`, the packs' and the disk's, sorted.
std::vector<std::string> listFiles(const std::string& dir);

// The seconds-since-epoch of a file's last write, or 0 when there is no such file. A file in a
// pack is never written, and is always 1. A sheet is reloaded when this moves.
int64_t fileModified(const std::string& path);

// The directory a path is in, without its trailing slash.
std::string directoryOf(const std::string& path);

// Joins, unless `rel` is already absolute.
std::string join(const std::string& dir, const std::string& rel);

}  // namespace mu::core
