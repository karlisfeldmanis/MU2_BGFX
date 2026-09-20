// Loading a compiled shader pair off disk, in one place.
//
// This existed twice before it existed here -- once in renderer.cpp and once in overlay.cpp,
// both in an anonymous namespace, both identical -- and effects.cpp would have been the
// third. The two older copies are deliberately left where they are for now rather than
// churned in a sprint that is not about them; moving them onto this is a contained job with
// an obvious acceptance test, which is exactly what PLAN.md hands to the junior.
#pragma once

#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "core/files.h"
#include "core/log.h"

namespace mu::gfx {

inline bgfx::ShaderHandle loadShaderFile(const std::string& dir, const char* name) {
    // bgfx_compile_shaders keeps the source's own extension: vs_effect.sc becomes
    // vs_effect.sc.bin, not vs_effect.bin.
    const std::string path = dir + "/" + name + ".sc.bin";
    std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) return BGFX_INVALID_HANDLE;
    const bgfx::Memory* mem = bgfx::copy(bytes.data(), uint32_t(bytes.size()));
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    if (bgfx::isValid(handle)) bgfx::setName(handle, name);
    return handle;
}

inline bgfx::ProgramHandle loadProgramFiles(const std::string& dir, const char* vs,
                                            const char* fs) {
    bgfx::ShaderHandle v = loadShaderFile(dir, vs);
    bgfx::ShaderHandle f = loadShaderFile(dir, fs);
    if (!bgfx::isValid(v) || !bgfx::isValid(f)) {
        core::logError("the program %s/%s did not load", vs, fs);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(v, f, true);
}

}  // namespace mu::gfx
