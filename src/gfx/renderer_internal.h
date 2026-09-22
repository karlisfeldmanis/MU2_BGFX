// What the Renderer's own translation units share, and nothing outside gfx/ may include.
//
// `renderer.cpp` was 1707 lines. It is five files now -- the draw spine, the setup, the
// reflection probe, the outline and the lights -- all of them implementing the one `Renderer`
// declared in `renderer.h`. That is deliberately a split of the IMPLEMENTATION and not of the
// class: there is still one Renderer, one header and one set of call sites, and splitting a
// class in half to make two files would have been a different and worse change.
//
// These two helpers were an anonymous namespace in the old single file. Four of the five
// pieces load a program, so they had to become something all five can see; inline in a header
// keeps them as cheap as they were.
#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/files.h"
#include "core/log.h"

namespace mu::gfx {

inline bgfx::ShaderHandle loadShader(const std::string& dir, const char* name) {
    // bgfx_compile_shaders keeps the source's own extension: vs_depth.sc becomes
    // vs_depth.sc.bin, not vs_depth.bin.
    const std::string path = dir + "/" + name + ".sc.bin";
    std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) return BGFX_INVALID_HANDLE;
    const bgfx::Memory* mem = bgfx::copy(bytes.data(), uint32_t(bytes.size()));
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    if (bgfx::isValid(handle)) bgfx::setName(handle, name);
    return handle;
}

inline bgfx::ProgramHandle loadProgram(const std::string& dir, const char* vs, const char* fs) {
    bgfx::ShaderHandle v = loadShader(dir, vs);
    bgfx::ShaderHandle f = loadShader(dir, fs);
    if (!bgfx::isValid(v) || !bgfx::isValid(f)) {
        core::logError("the program %s/%s did not load", vs, fs);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(v, f, true);
}

}  // namespace mu::gfx
