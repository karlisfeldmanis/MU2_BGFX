#include "core/pack.h"

#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>

#include <zstd.h>

#include "core/files.h"
#include "core/log.h"

namespace mu::core {
namespace {

struct Entry {
    int pack = 0;  // index into g_files
    uint64_t at = 0;
    uint32_t packed = 0;
    uint32_t size = 0;
    uint8_t method = kPackStored;
};

// Written once, at mount, before any job thread reads; read-only after. A read is a pread on
// the pack's descriptor, which needs no lock.
std::vector<int> g_files;
std::unordered_map<std::string, Entry> g_entries;  // key: the logical path, "assets/..."
std::string g_root;                                // the mounted folder, with its trailing "/"

uint64_t splitmix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

uint32_t u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint64_t u64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

#ifdef _WIN32
// Windows has no pread. ReadFile with the offset in an OVERLAPPED is its positional read, and
// like pread it leaves the descriptor's own position alone, so job threads read side by side.
ssize_t pread(int fd, void* out, size_t n, uint64_t at) {
    OVERLAPPED where{};
    where.Offset = DWORD(at & 0xffffffffu);
    where.OffsetHigh = DWORD(at >> 32);
    DWORD got = 0;
    const DWORD asked = DWORD(std::min<size_t>(n, 1u << 30));
    if (!ReadFile(HANDLE(_get_osfhandle(fd)), out, asked, &got, &where)) return -1;
    return ssize_t(got);
}
#else
using ::pread;
#endif

// The pack's length, or 0 when it cannot be had.
uint64_t lengthOf(int fd) {
#ifdef _WIN32
    const __int64 n = _filelengthi64(fd);
#else
    const off_t n = ::lseek(fd, 0, SEEK_END);
#endif
    return n > 0 ? uint64_t(n) : 0;
}

// The most a table or a file in a pack may say it holds. A pack whose header or table was
// damaged with its size and stamp unchanged -- which the launcher does not hash again -- asked
// for up to 4 GB a vector: bad_alloc in mountPacks, before the crash watch starts, and the game
// gone with no report and no box. The largest file shipped is under 17 MB (2026-10-15).
constexpr uint64_t kMostTable = 256ull << 20;
constexpr uint64_t kMostFile = 1ull << 30;

bool readAt(int fd, uint8_t* out, size_t n, uint64_t at) {
    while (n > 0) {
        const ssize_t got = pread(fd, out, n, at);
        if (got <= 0) return false;
        out += got;
        n -= size_t(got);
        at += uint64_t(got);
    }
    return true;
}

// The logical path of `path`, or empty when it is not under the mounted folder.
std::string logical(const std::string& path) {
    if (g_root.empty()) return {};
    std::string normal = std::filesystem::path(path).lexically_normal().generic_string();
    if (normal.compare(0, g_root.size(), g_root) != 0) return {};
    return normal.substr(g_root.size());
}

bool mountOne(const std::string& file) {
#ifndef O_BINARY
#define O_BINARY 0
#endif
    const int fd = ::open(file.c_str(), O_RDONLY | O_BINARY);
    if (fd < 0) {
        logError("pack: %s does not open", file.c_str());
        return false;
    }
    uint8_t header[32];
    if (!readAt(fd, header, sizeof(header), 0) || u32(header) != kPackMagic ||
        u32(header + 4) != kPackVersion) {
        logError("pack: %s is not a version %u pack", file.c_str(), kPackVersion);
        ::close(fd);
        return false;
    }
    const uint32_t count = u32(header + 8);
    const uint32_t tocPacked = u32(header + 12);
    const uint32_t tocSize = u32(header + 16);
    const uint64_t tocAt = u64(header + 24);
    const uint64_t fileLength = lengthOf(fd);
    if (tocAt > fileLength || tocPacked > fileLength - tocAt || tocSize > kMostTable) {
        logError("pack: %s's header is damaged", file.c_str());
        ::close(fd);
        return false;
    }
    std::vector<uint8_t> packed(tocPacked), toc(tocSize);
    if (!readAt(fd, packed.data(), packed.size(), tocAt)) {
        logError("pack: %s's table is cut short", file.c_str());
        ::close(fd);
        return false;
    }
    packScramble(packed.data(), packed.size(), tocAt);
    if (ZSTD_decompress(toc.data(), toc.size(), packed.data(), packed.size()) != tocSize) {
        logError("pack: %s's table does not open", file.c_str());
        ::close(fd);
        return false;
    }
    const int index = int(g_files.size());
    g_files.push_back(fd);
    size_t at = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 2 > toc.size()) break;
        uint16_t length;
        std::memcpy(&length, &toc[at], 2);
        at += 2;
        if (at + length + 17 > toc.size()) break;
        std::string name(reinterpret_cast<const char*>(&toc[at]), length);
        at += length;
        Entry entry;
        entry.pack = index;
        entry.at = u64(&toc[at]);
        entry.packed = u32(&toc[at + 8]);
        entry.size = u32(&toc[at + 12]);
        entry.method = toc[at + 16];
        at += 17;
        if (entry.at > fileLength || entry.packed > fileLength - entry.at || entry.size > kMostFile) {
            logError("pack: %s's entry for %s is damaged; left out", file.c_str(), name.c_str());
            continue;
        }
        g_entries[name] = entry;  // a later pack wins: a patch pack replaces a file
    }
    logf("pack: %s, %u files", std::filesystem::path(file).filename().string().c_str(), count);
    return true;
}

}  // namespace

void packScramble(uint8_t* bytes, size_t n, uint64_t at) {
    // Eight bytes of keystream a block, the block numbered by its place in the pack.
    for (size_t i = 0; i < n;) {
        const uint64_t place = at + i;
        const uint64_t key = splitmix(0x4d5532u ^ (place >> 3));
        const uint8_t* k = reinterpret_cast<const uint8_t*>(&key);
        for (size_t b = place & 7; b < 8 && i < n; ++b, ++i) bytes[i] ^= k[b];
    }
}

int mountPacks(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code error;
    // increment(error), not the range-for's ++, which throws on a folder that fails part-way
    // (an antivirus or a sync driver in the way): an uncaught throw before the crash watch.
    for (std::filesystem::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
        const std::filesystem::directory_entry& file = *it;
        if (file.path().extension() == ".mupak") files.push_back(file.path().generic_string());
    }
    // In name order, so which pack wins a path is the same on every machine.
    std::sort(files.begin(), files.end());
    g_root = std::filesystem::path(dir).lexically_normal().generic_string();
    if (g_root.empty() || g_root.back() != '/') g_root += '/';
    int mounted = 0;
    for (const std::string& file : files) mounted += mountOne(file) ? 1 : 0;
    if (mounted == 0) {
        logError("pack: no packs in %s", dir.c_str());
        g_root.clear();
        return 0;
    }
    static const FileSource source = {packRead, packHas, packList};
    setFileSource(&source);
    return mounted;
}

bool packsMounted() { return !g_entries.empty(); }

bool packRead(const std::string& path, std::vector<uint8_t>& out) {
    const std::string name = logical(path);
    if (name.empty()) return false;
    const auto found = g_entries.find(name);
    if (found == g_entries.end()) return false;
    const Entry& entry = found->second;
    std::vector<uint8_t> packed(entry.packed);
    if (!readAt(g_files[size_t(entry.pack)], packed.data(), packed.size(), entry.at)) {
        logError("pack: %s is cut short", name.c_str());
        return false;
    }
    packScramble(packed.data(), packed.size(), entry.at);
    if (entry.method == kPackStored) {
        out = std::move(packed);
        return true;
    }
    std::vector<uint8_t> bytes(entry.size);
    if (ZSTD_decompress(bytes.data(), bytes.size(), packed.data(), packed.size()) != entry.size) {
        logError("pack: %s does not decompress", name.c_str());
        return false;
    }
    out = std::move(bytes);
    return true;
}

bool packHas(const std::string& path) {
    const std::string name = logical(path);
    return !name.empty() && g_entries.count(name) != 0;
}

std::vector<std::string> packList(const std::string& dir) {
    std::vector<std::string> leaves;
    std::string prefix = logical(dir + "/x");
    if (prefix.empty()) return leaves;
    prefix.pop_back();  // "assets/cooked/figures/"
    for (const auto& [name, entry] : g_entries) {
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.find('/', prefix.size()) != std::string::npos) continue;
        leaves.push_back(name.substr(prefix.size()));
    }
    std::sort(leaves.begin(), leaves.end());
    return leaves;
}

}  // namespace mu::core
