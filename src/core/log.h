// The log is the review loop. Everything the engine learns goes here: every asset with its
// bounds, every refusal, a frame line a second, and the run's summary. A run is reviewed by
// reading this file and a PNG, not by watching the window.
#pragma once

#include <cstdarg>
#include <cstdio>

namespace mu::core {

// Opens the log at `path`, keeping one previous run beside it as <path>.previous.
// Everything logged before this is buffered and flushed on open.
void logOpen(const char* path);
void logClose();
// Drops every line from here on, printed or kept: the bot's (tools/bot), whose hours of play
// would print the realm's every blow and keep them all waiting for a log nobody opens.
void logSilence(bool on);
// Every line logged from here on is also handed to `fn`, unstamped, under the log's lock and on
// whichever thread logged it; nullptr takes it off. One tap at a time: the sweep's
// (app/sweep.h), which keeps what a frame said beside that frame's time.
using LogTap = void (*)(const char* line, void* user);
void logTap(LogTap fn, void* user);

void logv(const char* fmt, va_list args);

#if defined(__MINGW32__)
// mingw's `printf` archetype is msvcrt's, which has no %zu or %lld, and every log line with a
// size_t warned (390 of the Windows build's 417 warnings, 2026-10-15). g++ builds with
// __USE_MINGW_ANSI_STDIO, so the vsnprintf behind logv is mingw's C99 one: checked as that.
#define MU_PRINTF(a, b) __attribute__((format(__MINGW_PRINTF_FORMAT, a, b)))
#elif defined(__GNUC__) || defined(__clang__)
#define MU_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define MU_PRINTF(a, b)
#endif

MU_PRINTF(1, 2) void logf(const char* fmt, ...);

// A refusal: logged with ERROR in front of it so a run's failures are one grep away.
MU_PRINTF(1, 2) void logError(const char* fmt, ...);

// How many errors have been logged. A review run reports it, so a silent failure is not
// silent twice.
int logErrorCount();

// Writes the last 64 KB logged to `fd`, oldest first, taking no lock and allocating nothing:
// what the crash and hang reports (core/watch.h) end with.
void logWriteTail(int fd);

}  // namespace mu::core
