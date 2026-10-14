// core/watch.h on Windows (docs/plans/windows-client.md): the same black box, by Windows' own means.
// A crash is an unhandled SEH exception, not a signal, and a stack is a minidump rather than
// backtrace(): MiniDumpWriteDump writes every thread's stack into crash-<when>.dmp, which a
// player sends back and Visual Studio or WinDbg opens. A freeze is a minidump taken from the
// watcher thread, with the main thread in it wherever it is stuck; nothing is stopped.
#include "core/watch.h"

#include "core/log.h"

#include <windows.h>
#include <dbghelp.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

namespace mu::core {
namespace {

constexpr double kHangSeconds = 5.0;
constexpr double kHangAgainSeconds = 30.0;

std::string g_dir;
// Made up front: the crash handler runs on a broken process and allocates nothing.
char g_crashPath[1024];
char g_crashDump[1024];
std::atomic<int64_t> g_beatMs{0};
std::thread g_watcher;
std::mutex g_lock;
std::condition_variable g_wake;
bool g_stopping = false;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string stamp() {
    time_t now = time(nullptr);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d_%H-%M-%S", localtime(&now));
    return text;
}

void say(int fd, const char* text) { (void)!_write(fd, text, unsigned(std::strlen(text))); }

const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION (a bad pointer)";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    default: return "an unhandled exception";
    }
}

// Every thread's stack and the faulting one's registers, into `path`. `info` is the crash's
// own record, or null for a look at a live process.
void dumpInto(const char* path, EXCEPTION_POINTERS* info) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION about{};
    about.ThreadId = GetCurrentThreadId();
    about.ExceptionPointers = info;
    about.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                      info ? &about : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void* at = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
    const int fd = _open(g_crashPath, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
    if (fd >= 0) {
        char line[512];
        // The exe's own base, so an address can be read against `x86_64-w64-mingw32-nm -C`.
        std::snprintf(line, sizeof(line),
                      "mu2 crashed: %s (0x%08lx) at address %p, pid %lu\n"
                      "the exe is loaded at %p; every thread's stack is in %s\n\n",
                      exceptionName(code), (unsigned long)code, at,
                      (unsigned long)GetCurrentProcessId(), (void*)GetModuleHandleA(nullptr),
                      g_crashDump);
        say(fd, line);
        say(fd, "the last of the log:\n");
        logWriteTail(fd);
        _close(fd);
    }
    dumpInto(g_crashDump, info);
    std::fprintf(stderr, "mu2 crashed; the report is beside mu2.log, in logs/\n");
    return EXCEPTION_CONTINUE_SEARCH;  // and Windows ends the process as it would have
}

void onTerminate() {
    if (std::exception_ptr thrown = std::current_exception()) {
        try {
            std::rethrow_exception(thrown);
        } catch (const std::exception& e) {
            logf("CRASH an uncaught exception: %s", e.what());
        } catch (...) {
            logf("CRASH an uncaught exception that is not a std::exception");
        }
    }
    std::abort();
}

void hangInto(const std::string& path, double goneSeconds) {
    const std::string dump = path.substr(0, path.size() - 4) + ".dmp";
    const int fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
    if (fd < 0) return;
    char line[1280];
    std::snprintf(line, sizeof(line),
                  "mu2 froze: no frame for %.1f s, pid %lu\n"
                  "Every thread's stack, the main thread's among them, is in %s.\n\n",
                  goneSeconds, (unsigned long)GetCurrentProcessId(), dump.c_str());
    say(fd, line);
    say(fd, "the last of the log:\n");
    logWriteTail(fd);
    _close(fd);
    dumpInto(dump.c_str(), nullptr);
}

void watch() {
    int reports = 0;
    std::unique_lock<std::mutex> hold(g_lock);
    while (!g_stopping) {
        g_wake.wait_for(hold, std::chrono::milliseconds(500));
        if (g_stopping) break;
        const int64_t beat = g_beatMs.load();
        if (beat <= 0) continue;
        const double gone = double(nowMs() - beat) / 1000.0;
        if (gone < kHangSeconds) {
            reports = 0;
            continue;
        }
        if (reports == 0 || (reports == 1 && gone >= kHangAgainSeconds)) {
            ++reports;
            const std::string path = g_dir + "/hang-" + stamp() + ".txt";
            hold.unlock();
            hangInto(path, gone);
            logError("HANG no frame for %.1f s; every thread's stack is in %s", gone,
                     path.c_str());
            hold.lock();
            std::fprintf(stderr, "mu2 has had no frame for %.0f s; %s\n", gone, path.c_str());
        }
    }
}

}  // namespace

void watchStart(const char* dir) {
    g_dir = dir;
    _mkdir(g_dir.c_str());
    const std::string when = stamp();
    std::snprintf(g_crashPath, sizeof(g_crashPath), "%s/crash-%s.txt", g_dir.c_str(), when.c_str());
    std::snprintf(g_crashDump, sizeof(g_crashDump), "%s/crash-%s.dmp", g_dir.c_str(), when.c_str());
    // A stack overflow leaves the handler no stack of its own unless some is kept back.
    ULONG reserve = 64 * 1024;
    SetThreadStackGuarantee(&reserve);
    SetUnhandledExceptionFilter(onCrash);
    std::set_terminate(onTerminate);
}

void watchBeat() {
    const int64_t now = nowMs();
    const int64_t before = g_beatMs.exchange(now);
    if (before == 0) {
        g_watcher = std::thread(watch);
        return;
    }
    if (before < 0) return;
    const double gone = double(now - before) / 1000.0;
    if (gone >= kHangSeconds) logf("HANG no frame for %.1f s; the report is beside mu2.log", gone);
}

void watchPause() {
    if (g_beatMs.load() != 0) g_beatMs = -1;
}

void watchStop() {
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_stopping = true;
    }
    g_wake.notify_all();
    if (g_watcher.joinable()) g_watcher.join();
    g_beatMs = 0;
}

}  // namespace mu::core
