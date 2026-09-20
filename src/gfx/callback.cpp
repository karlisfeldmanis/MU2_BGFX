#include "gfx/callback.h"

#include <bimg/encode.h>
#include <bx/file.h>

#include "core/log.h"

namespace mu::gfx {

void Callback::fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) {
    core::logError("bgfx fatal %d at %s:%u: %s", int(code), filePath, unsigned(line), str);
    core::logClose();
    abort();
}

void Callback::traceVargs(const char* filePath, uint16_t line, const char* format, va_list argList) {
    char body[2048];
    bx::vsnprintf(body, sizeof(body), format, argList);
    // bgfx ends most of its traces with a newline; the log adds its own.
    size_t n = size_t(bx::strLen(body));
    while (n && (body[n - 1] == '\n' || body[n - 1] == '\r')) body[--n] = '\0';
    if (n) core::logf("bgfx %s:%u %s", filePath, unsigned(line), body);
}

void Callback::screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                          bgfx::TextureFormat::Enum format, const void* data, uint32_t size,
                          bool yflip) {
    bx::FileWriter writer;
    bx::Error err;
    if (!bx::open(&writer, filePath, false, &err)) {
        core::logError("cannot write the shot %s", filePath);
        return;
    }
    // The format is bgfx's to say, not ours to assume: the backbuffer is BGRA8 here and
    // need not be on the next machine.
    bimg::imageWritePng(&writer, width, height, pitch, data, bimg::TextureFormat::Enum(format),
                        yflip, &err);
    bx::close(&writer);
    if (!err.isOk()) {
        core::logError("the shot %s did not encode", filePath);
        return;
    }
    core::logf("shot %s (%ux%u, %u bytes)", filePath, width, height, size);
}

}  // namespace mu::gfx
