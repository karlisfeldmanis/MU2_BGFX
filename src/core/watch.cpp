#include "core/watch.h"

#include "core/log.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

extern char** environ;

namespace mu::core {
namespace {

// A frame gone this long is a freeze. The longest honest frame is a world loading on travel,
// about a second; a shot stalls one to a quarter.
constexpr double kHangSeconds = 5.0;
constexpr double kHangAgainSeconds = 30.0;

std::string g_dir;
// Made up front: a signal handler may not allocate, so the crash report's name is ready.
char g_crashPath[1024];
std::atomic<int64_t> g_beatMs{0};
std::thread g_watcher;
std::mutex g_lock;
std::condition_variable g_wake;
bool g_stopping = false;
char g_altStack[64 * 1024];

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

void say(int fd, const char* text) { (void)!::write(fd, text, std::strlen(text)); }

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (a bad pointer)";
    case SIGBUS: return "SIGBUS (a bad pointer)";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT (an abort, an assert or an uncaught throw)";
    case SIGTRAP: return "SIGTRAP";
    default: return "a fatal signal";
    }
}

void onCrash(int sig, siginfo_t* info, void*) {
    const int fd = ::open(g_crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char line[256];
        std::snprintf(line, sizeof(line), "mu2 crashed: %s at address %p, pid %d\n\n",
                      signalName(sig), info ? info->si_addr : nullptr, int(getpid()));
        say(fd, line);
        say(fd, "the crashing thread (names mangled: pipe through c++filt):\n");
        void* frames[128];
        const int n = backtrace(frames, 128);
        backtrace_symbols_fd(frames, n, fd);
        say(fd, "\nthe last of the log:\n");
        logWriteTail(fd);
        ::close(fd);
    }
    say(STDERR_FILENO, "mu2 crashed; the report is in crashes/\n");
    // Handed back to the default, which ends the process and lets macOS write its .ips.
    signal(sig, SIG_DFL);
    raise(sig);
}

// What std::terminate prints before it aborts, so an uncaught throw's report says what it was.
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

// /usr/bin/sample: every thread's stack, symbolised, over two seconds of the stuck process.
// Run from outside it, so it works whatever the main thread is stuck in.
void sampleInto(const std::string& path, double goneSeconds) {
    {
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return;
        std::fprintf(f, "mu2 froze: no frame for %.1f s, pid %d\n"
                        "Read the main thread (com.apple.main-thread) first.\n\n",
                     goneSeconds, int(getpid()));
        std::fclose(f);
    }
    const std::string samplePath = path + ".sample";
    const std::string pid = std::to_string(getpid());
    const char* argv[] = {"/usr/bin/sample", pid.c_str(), "2", "-mayDie", "-file",
                          samplePath.c_str(), nullptr};
    // Its chatter kept out of the game's console.
    posix_spawn_file_actions_t quiet;
    posix_spawn_file_actions_init(&quiet);
    posix_spawn_file_actions_addopen(&quiet, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&quiet, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t child = 0;
    const int spawned =
        posix_spawn(&child, argv[0], &quiet, nullptr, const_cast<char**>(argv), environ);
    posix_spawn_file_actions_destroy(&quiet);
    if (spawned == 0) {
        int status = 0;
        waitpid(child, &status, 0);
    }
    const int fd = ::open(path.c_str(), O_WRONLY | O_APPEND);
    if (fd < 0) return;
    if (FILE* s = std::fopen(samplePath.c_str(), "r")) {
        char buf[16384];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), s)) > 0) (void)!::write(fd, buf, n);
        std::fclose(s);
        std::remove(samplePath.c_str());
    } else {
        say(fd, "(sample could not be run)\n");
    }
    say(fd, "\nthe last of the log:\n");
    logWriteTail(fd);
    ::close(fd);
}

void watch() {
    int reports = 0;  // for the stall now under way
    std::unique_lock<std::mutex> hold(g_lock);
    while (!g_stopping) {
        g_wake.wait_for(hold, std::chrono::milliseconds(500));
        if (g_stopping) break;
        const int64_t beat = g_beatMs.load();
        if (beat <= 0) continue;  // not started, or paused
        const double gone = double(nowMs() - beat) / 1000.0;
        if (gone < kHangSeconds) {
            reports = 0;
            continue;
        }
        if (reports == 0 || (reports == 1 && gone >= kHangAgainSeconds)) {
            ++reports;
            const std::string path = g_dir + "/hang-" + stamp() + ".txt";
            hold.unlock();
            sampleInto(path, gone);
            hold.lock();
            std::fprintf(stderr, "mu2 has had no frame for %.0f s; %s\n", gone, path.c_str());
        }
    }
}

}  // namespace

void watchStart(const char* root) {
    g_dir = std::string(root) + "/crashes";
    ::mkdir(g_dir.c_str(), 0755);
    std::snprintf(g_crashPath, sizeof(g_crashPath), "%s/crash-%s.txt", g_dir.c_str(),
                  stamp().c_str());

    // Its own stack, or a stack overflow has none left to report on.
    stack_t alt{};
    alt.ss_sp = g_altStack;
    alt.ss_size = sizeof(g_altStack);
    sigaltstack(&alt, nullptr);
    struct sigaction act{};
    act.sa_sigaction = onCrash;
    act.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&act.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) sigaction(sig, &act, nullptr);
    std::set_terminate(onTerminate);
}

void watchBeat() {
    const int64_t now = nowMs();
    const int64_t before = g_beatMs.exchange(now);
    if (before == 0) {
        g_watcher = std::thread(watch);
        return;
    }
    if (before < 0) return;  // paused
    const double gone = double(now - before) / 1000.0;
    if (gone >= kHangSeconds) logf("HANG no frame for %.1f s; the report is in crashes/", gone);
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
