#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::core {

// Reads a whole file. Empty on failure, and the failure is in the log.
std::vector<uint8_t> readFile(const std::string& path);

bool fileExists(const std::string& path);

// The seconds-since-epoch of a file's last write, or 0. A sheet is reloaded when this moves.
int64_t fileModified(const std::string& path);

// The directory a path is in, without its trailing slash.
std::string directoryOf(const std::string& path);

// Joins, unless `rel` is already absolute.
std::string join(const std::string& dir, const std::string& rel);

}  // namespace mu::core
