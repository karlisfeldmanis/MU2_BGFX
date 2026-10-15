// SHA-256, for the launcher's check of each game file against the release's manifest
// (tools/mu/release.py names every file on the box by it), and for the game's pass (net/pass.h),
// which the client and the server both prove with. FIPS 180-4, nothing more.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace mu::launcher {

class Sha256 {
public:
    Sha256();
    void add(const void* bytes, size_t size);
    void digest(uint8_t out[32]);  // the hash is spent after
    std::string hex();             // the digest as 64 lower-case hex digits

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buffer_[64];
    size_t held_ = 0;
    uint64_t length_ = 0;
};

std::string sha256(const std::string& bytes);
// A file's, read a MB at a time; `read` hears each MB (for a progress bar). Empty when it cannot
// be read.
std::string sha256File(const std::filesystem::path& path, const std::function<void(uint64_t)>& read = {});

}  // namespace mu::launcher
