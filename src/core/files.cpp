#include "core/files.h"

#include <sys/stat.h>

#include <cstdio>

#include "core/log.h"

namespace mu::core {

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> out;
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
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG);
}

int64_t fileModified(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return 0;
    return int64_t(st.st_mtime);
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
