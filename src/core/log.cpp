#include "core/log.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <ctime>
#include <string>

namespace mu::core {
namespace {

FILE* g_file = nullptr;
std::atomic<int> g_errors{0};
// The preloader's worker logs while the main thread may too: a line is written whole.
std::recursive_mutex g_lock;
// What is logged before logOpen: the arguments are parsed before the log path is known, and
// a complaint about them must not be lost.
std::string g_pending;

// Wall clock, not `clock()`. `clock()` counts this process's own CPU time, and a run that
// waits -- for the drawable, for a file, for the compositor -- spends wall seconds it never
// charges. A 400-frame run read 0.28 s in the log against 0.79 s of wall, so every timestamp
// in it priced the run 2.8x fast, which is exactly the direction a renderer must not lie in.
// Steady rather than system time, so a clock adjustment mid-run cannot move a stamp backwards.
double secondsSinceStart() {
    static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

void logOpen(const char* path) {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (g_file) return;
    std::string previous = std::string(path) + ".previous";
    std::remove(previous.c_str());
    std::rename(path, previous.c_str());
    g_file = std::fopen(path, "w");
    if (!g_file) {
        std::fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    time_t now = time(nullptr);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    std::fprintf(g_file, "mu2 %s\n", stamp);
    if (!g_pending.empty()) {
        std::fwrite(g_pending.data(), 1, g_pending.size(), g_file);
        g_pending.clear();
    }
    std::fflush(g_file);
}

void logClose() {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void logv(const char* fmt, va_list args) {
    char line[2048];
    va_list copy;
    va_copy(copy, args);
    int n = std::vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n < 0) return;

    char stamped[2112];
    int m = std::snprintf(stamped, sizeof(stamped), "[%7.3f] %s\n", secondsSinceStart(), line);
    if (m < 0) return;

    std::lock_guard<std::recursive_mutex> hold(g_lock);
    std::fwrite(stamped, 1, size_t(m), stdout);
    if (g_file) {
        std::fwrite(stamped, 1, size_t(m), g_file);
        std::fflush(g_file);
    } else {
        g_pending.append(stamped, size_t(m));
    }
}

void logf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logv(fmt, args);
    va_end(args);
}

void logError(const char* fmt, ...) {
    ++g_errors;
    char line[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    logf("ERROR %s", line);
}

int logErrorCount() { return g_errors; }

}  // namespace mu::core
