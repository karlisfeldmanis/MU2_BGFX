// core/watch.h on Windows (docs/plans/windows-client.md): the same black box, by Windows' own means.
// A crash is an unhandled SEH exception, not a signal, and a stack is a minidump rather than
// backtrace(): MiniDumpWriteDump writes every thread's stack into crash-<when>.dmp, which a
// player sends back and Visual Studio or WinDbg opens. A freeze is a minidump taken from the
// watcher thread, with the main thread in it wherever it is stuck; nothing is stopped.
// The crash's .txt names the module the fault is in and walks the crashing thread's stack as
// module+offset, so a driver's crash reads as one (nvwgf2umx.dll+0x...) before any dump is opened.
#include "core/watch.h"

#include "core/log.h"

#include <windows.h>
#include <dbghelp.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>

#include <algorithm>
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
std::string g_name;
char g_note[1024];
std::atomic<size_t> g_noteUsed{0};
// Made up front: the crash handler runs on a broken process and allocates nothing.
char g_crashPath[1024];
char g_crashDump[1024];
std::atomic<int64_t> g_beatMs{0};
std::thread g_watcher;
std::mutex g_lock;
std::condition_variable g_wake;
bool g_stopping = false;
std::atomic<bool> g_reported{false};

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

void sayNotes(int fd) { (void)!_write(fd, g_note, unsigned(g_noteUsed.load())); }

// "nvwgf2umx.dll+0x1a2b3c": the module an address is in, by its file name, and how far into it.
// MU2.exe's own offsets read against the release's exe: addr2line -e MU2.exe 0x140000000+offset.
void where(void* at, char* out, size_t room) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = {};
    if (at && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 static_cast<LPCSTR>(at), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH)) {
        const char* name = path;
        for (const char* c = path; *c; ++c)
            if (*c == '\\' || *c == '/') name = c + 1;
        std::snprintf(out, room, "%s+0x%llx", name,
                      (unsigned long long)(static_cast<char*>(at) - reinterpret_cast<char*>(module)));
    } else {
        std::snprintf(out, room, "%p (in no module)", at);
    }
}

// The crashing thread's frames, unwound from the fault's own registers by the unwind tables every
// x64 module carries (no symbols, no dbghelp). A frame with no table is a leaf only at the top.
void sayStack(int fd, const CONTEXT* fault) {
#if defined(_M_X64) || defined(__x86_64__)
    if (!fault) return;
    CONTEXT ctx = *fault;
    char line[400];
    for (int i = 0; i < 48 && ctx.Rip; ++i) {
        char at[320];
        where(reinterpret_cast<void*>(ctx.Rip), at, sizeof(at));
        std::snprintf(line, sizeof(line), "  %2d  %s\n", i, at);
        say(fd, line);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
        if (!fn) {
            if (i > 0 || !ctx.Rsp || (ctx.Rsp & 7)) break;
            ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
            continue;
        }
        void* handlerData = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handlerData, &frame, nullptr);
    }
#else
    (void)fd;
    (void)fault;
#endif
}

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
    // One at a time, and a second skipped rather than waited for: DbgHelp is not thread-safe,
    // and a lost device's report (watchReport, the main thread) can come while the freeze
    // watch is still dumping the same Present's stall, which already holds every stack.
    static std::atomic_flag dumping = ATOMIC_FLAG_INIT;
    if (dumping.test_and_set()) return;
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        dumping.clear();
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION about{};
    about.ThreadId = GetCurrentThreadId();
    about.ExceptionPointers = info;
    about.ClientPointers = FALSE;
    // Every thread's stack and the modules, a few hundred KB: small enough to be sent home
    // (launcher/crash_upload.h), where the memory that pointers reach made megabytes.
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                      info ? &about : nullptr, nullptr, nullptr);
    CloseHandle(file);
    dumping.clear();
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    if (g_reported) return EXCEPTION_CONTINUE_SEARCH;  // watchReport said it already
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void* at = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
    const int fd = _open(g_crashPath, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
    if (fd >= 0) {
        char line[1600];
        char in[320];
        where(at, in, sizeof(in));
        // The exe's own base, so an address can be read against `x86_64-w64-mingw32-nm -C`.
        std::snprintf(line, sizeof(line),
                      "mu2 crashed: %s (0x%08lx) at address %p, pid %lu\n"
                      "in %s\n"
                      "the exe is loaded at %p; every thread's stack is in %s\n",
                      exceptionName(code), (unsigned long)code, at,
                      (unsigned long)GetCurrentProcessId(), in, (void*)GetModuleHandleA(nullptr),
                      g_crashDump);
        say(fd, line);
        sayNotes(fd);
        say(fd, "\nthe crashing thread:\n");
        sayStack(fd, info ? info->ContextRecord : nullptr);
        say(fd, "\nthe last of the log:\n");
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
            char what[300];
            std::snprintf(what, sizeof(what), "an uncaught exception: %s", e.what());
            watchReport(what);
        } catch (...) {
            logf("CRASH an uncaught exception that is not a std::exception");
            watchReport("an uncaught exception that is not a std::exception");
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
                  "Every thread's stack, the main thread's among them, is in %s.\n",
                  goneSeconds, (unsigned long)GetCurrentProcessId(), dump.c_str());
    say(fd, line);
    sayNotes(fd);
    say(fd, "\n");
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
            const std::string path = g_dir + "/" + g_name + "hang-" + stamp() + ".txt";
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

void watchStart(const char* dir, const char* name) {
    g_dir = dir;
    g_name = name;
    _mkdir(g_dir.c_str());
    const std::string when = stamp();
    std::snprintf(g_crashPath, sizeof(g_crashPath), "%s/%scrash-%s.txt", g_dir.c_str(), name,
                  when.c_str());
    std::snprintf(g_crashDump, sizeof(g_crashDump), "%s/%scrash-%s.dmp", g_dir.c_str(), name,
                  when.c_str());
    // A stack overflow leaves the handler no stack of its own unless some is kept back.
    ULONG reserve = 64 * 1024;
    SetThreadStackGuarantee(&reserve);
    SetUnhandledExceptionFilter(onCrash);
    std::set_terminate(onTerminate);
}

void watchReport(const char* what) {
    const int fd = _open(g_crashPath, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
    if (fd >= 0) {
        char line[1600];
        std::snprintf(line, sizeof(line),
                      "mu2 stopped: %s, pid %lu\n"
                      "the exe is loaded at %p; every thread's stack is in %s\n",
                      what, (unsigned long)GetCurrentProcessId(), (void*)GetModuleHandleA(nullptr),
                      g_crashDump);
        say(fd, line);
        sayNotes(fd);
        say(fd, "\nthe thread that stopped it:\n");
        CONTEXT here{};
        RtlCaptureContext(&here);
        sayStack(fd, &here);
        say(fd, "\nthe last of the log:\n");
        logWriteTail(fd);
        _close(fd);
    }
    dumpInto(g_crashDump, nullptr);
    g_reported = true;
}

void watchNote(const char* line) {
    const size_t used = g_noteUsed.load();
    const int n = std::snprintf(g_note + used, sizeof(g_note) - used, "%s\n", line);
    if (n > 0) g_noteUsed = std::min(sizeof(g_note) - 1, used + size_t(n));
}

void watchBeat() {
    const int64_t now = nowMs();
    const int64_t before = g_beatMs.exchange(now);
    // The first beat starts the watch but does not arm it: frame 0 compiles the shaders and
    // uploads the first textures, 7.1 s on an Intel UHD 770 under Direct3D 11 on 2026-10-18,
    // which filed a hang for a game that then played on. From the second beat it watches.
    if (before == 0) {
        g_beatMs = -1;
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
