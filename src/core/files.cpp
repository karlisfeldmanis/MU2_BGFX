#include "core/files.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "core/log.h"

namespace mu::core {
namespace {

const FileSource* g_source = nullptr;

}  // namespace

void setFileSource(const FileSource* source) { g_source = source; }

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> out;
    if (g_source && g_source->read(path, out)) return out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        logError("cannot read %s", path.c_str());
        return out;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(size_t(n));
        if (std::fread(out.data(), 1, size_t(n), f) != size_t(n)) {
            logError("%s ended early", path.c_str());
            out.clear();
        }
    }
    std::fclose(f);
    return out;
}

bool fileExists(const std::string& path) {
    if (g_source && g_source->has(path)) return true;
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG);
}

int64_t fileModified(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) return int64_t(st.st_mtime);
    // A file in a pack is there and never changes: one stamp, forever. Not 0, which every
    // watcher reads as "no such file" -- the player's app ran without its lighting sheet so,
    // and with it the tonemap, the mist and the grade (the user, 2026-10-14).
    return g_source && g_source->has(path) ? 1 : 0;
}

std::vector<std::string> listFiles(const std::string& dir) {
    std::vector<std::string> leaves;
    if (g_source) leaves = g_source->list(dir);
    std::error_code error;
    for (const auto& file : std::filesystem::directory_iterator(dir, error)) {
        if (!file.is_regular_file(error)) continue;
        std::string leaf = file.path().filename().string();
        if (!std::binary_search(leaves.begin(), leaves.end(), leaf)) leaves.push_back(leaf);
    }
    std::sort(leaves.begin(), leaves.end());
    return leaves;
}

std::string directoryOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string join(const std::string& dir, const std::string& rel) {
    if (!rel.empty() && rel[0] == '/') return rel;
    if (dir.empty()) return rel;
    return dir + "/" + rel;
}

}  // namespace mu::core
