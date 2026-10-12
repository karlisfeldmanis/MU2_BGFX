#include "gfx/gui.h"

#include <cstring>

#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include "core/log.h"
#include "gfx/views.h"
#include "gfx/window.h"
#include "imgui.h"

// bgfx's own ImGui shaders, compiled for every renderer and kept in its examples
// (extern/bgfx.cmake/bgfx/examples/common/imgui): a position, a colour and a texture.
#include "vs_ocornut_imgui.bin.h"
#include "fs_ocornut_imgui.bin.h"

namespace mu::gfx {
namespace {

const bgfx::EmbeddedShader kShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END(),
};

// An ImTextureID is a bgfx texture's index; ImTextureID_Invalid (0) is none, so the index is
// kept one up.
ImTextureID toId(bgfx::TextureHandle handle) { return ImTextureID(handle.idx) + 1; }
bgfx::TextureHandle fromId(ImTextureID id) { return {uint16_t(id - 1)}; }

// Plain, dark and quiet: no borders, no ornaments (the Sanctuary taste the user set for the
// game's windows holds here too), the world behind showing faintly through.
void style(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 0.0f;
    s.WindowRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.WindowPadding = ImVec2(10.0f, 8.0f);
    s.ItemSpacing = ImVec2(8.0f, 5.0f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.08f, 0.90f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.10f, 0.95f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.14f, 0.95f);
    c[ImGuiCol_Header] = ImVec4(0.24f, 0.22f, 0.18f, 0.80f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.34f, 0.30f, 0.22f, 0.80f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.42f, 0.36f, 0.24f, 0.90f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.29f, 0.22f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.36f, 0.24f, 1.0f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.78f, 0.62f, 0.30f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.16f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.32f, 0.29f, 0.22f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.26f, 0.23f, 0.18f, 1.0f);
    s.ScaleAllSizes(scale);
}

}  // namespace

bool Gui::open(const Window& window, const std::string& font, float size) {
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;      // the panels' places are the code's, not a file's
    io.MouseDrawCursor = true;     // the window's own cursor is blank
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    io.BackendRendererName = "mu2_bgfx";
    // White glyphs in RGBA, not alpha alone: the shader multiplies the texel by the vertex
    // colour, and an A8 texel samples with no white in it.
    io.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;

    // The window's pixels over its points: 2 on a Retina display. The panels are laid out in
    // framebuffer pixels, as the pointer is, so the font and the spacing are scaled by it.
    int pointsW = 0, pointsH = 0;
    window.windowSize(&pointsW, &pointsH);
    const float scale = pointsW > 0 ? float(window.width()) / float(pointsW) : 1.0f;
    style(scale);
    ImFontConfig config;
    config.SizePixels = size * scale;
    if (font.empty() || !io.Fonts->AddFontFromFileTTF(font.c_str(), size * scale)) {
        if (!font.empty()) core::logError("gui: %s would not open; ImGui's own font", font.c_str());
        io.Fonts->AddFontDefault(&config);
    }

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    program_ = bgfx::createProgram(bgfx::createEmbeddedShader(kShaders, type, "vs_ocornut_imgui"),
                                   bgfx::createEmbeddedShader(kShaders, type, "fs_ocornut_imgui"),
                                   true);
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    if (!bgfx::isValid(program_)) {
        core::logError("gui: the ImGui program did not build");
        close();
        return false;
    }
    return true;
}

void Gui::close() {
    if (!context_) return;
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->GetTexID() != ImTextureID_Invalid) bgfx::destroy(fromId(texture->GetTexID()));
        texture->SetTexID(ImTextureID_Invalid);
        texture->SetStatus(ImTextureStatus_Destroyed);
    }
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    program_ = BGFX_INVALID_HANDLE;
    sampler_ = BGFX_INVALID_HANDLE;
    ImGui::DestroyContext(context_);
    context_ = nullptr;
}

void Gui::begin(const Window& window, float deltaSeconds) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(window.width()), float(window.height()));
    io.DeltaTime = deltaSeconds > 0.0f ? deltaSeconds : 1.0f / 60.0f;

    float x = 0.0f, y = 0.0f;
    window.pointer(&x, &y);
    io.AddMousePosEvent(x, y);
    // A press and its release inside one pump reach here as a click with nothing held: given
    // as both, in order, and ImGui's event queue spreads them over two frames.
    for (int b = 0; b < 2; ++b) {
        if (window.clicked(b)) io.AddMouseButtonEvent(b, true);
        io.AddMouseButtonEvent(b, window.held(b));
    }
    if (window.scroll() != 0.0f) io.AddMouseWheelEvent(0.0f, window.scroll());

    if (!window.typed().empty()) io.AddInputCharactersUTF8(window.typed().c_str());
    auto tap = [&io](ImGuiKey key) {
        io.AddKeyEvent(key, true);
        io.AddKeyEvent(key, false);
    };
    for (int i = 0; i < window.backspaces(); ++i) tap(ImGuiKey_Backspace);
    if (window.entered()) tap(ImGuiKey_Enter);
    if (window.escaped()) tap(ImGuiKey_Escape);
    if (window.tabbed()) tap(ImGuiKey_Tab);
    for (int i = 0; i < window.arrows(); ++i) tap(ImGuiKey_DownArrow);
    for (int i = 0; i > window.arrows(); --i) tap(ImGuiKey_UpArrow);
    io.AddKeyEvent(ImGuiMod_Shift, window.shift());

    ImGui::NewFrame();
}

void Gui::end() {
    ImGui::Render();
    render(ImGui::GetDrawData());
}

bool Gui::wantsPointer() const { return context_ && ImGui::GetIO().WantCaptureMouse; }
bool Gui::wantsKeys() const { return context_ && ImGui::GetIO().WantTextInput; }

void Gui::render(ImDrawData* data) {
    // The textures ImGui asks for -- the font atlas, made and grown as glyphs are first drawn.
    if (data->Textures) {
        for (ImTextureData* texture : *data->Textures) {
            if (texture->Status == ImTextureStatus_WantCreate) {
                // Made empty and then filled: a bgfx texture made WITH its pixels is immutable,
                // and the glyphs ImGui adds later (a digit first drawn, a new size) never land.
                const bgfx::TextureHandle handle = bgfx::createTexture2D(
                    uint16_t(texture->Width), uint16_t(texture->Height), false, 1,
                    bgfx::TextureFormat::RGBA8,   // TexDesiredFormat, set in open()
                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
                bgfx::updateTexture2D(handle, 0, 0, 0, 0, uint16_t(texture->Width),
                                      uint16_t(texture->Height),
                                      bgfx::copy(texture->GetPixels(), uint32_t(texture->GetSizeInBytes())));
                texture->SetTexID(toId(handle));
                texture->SetStatus(ImTextureStatus_OK);
            } else if (texture->Status == ImTextureStatus_WantUpdates) {
                const bgfx::TextureHandle handle = fromId(texture->GetTexID());
                const int bpp = texture->BytesPerPixel;
                for (const ImTextureRect& r : texture->Updates) {
                    const bgfx::Memory* memory = bgfx::alloc(uint32_t(r.w * r.h * bpp));
                    for (int row = 0; row < r.h; ++row) {
                        std::memcpy(memory->data + row * r.w * bpp,
                                    texture->GetPixelsAt(r.x, r.y + row), size_t(r.w * bpp));
                    }
                    bgfx::updateTexture2D(handle, 0, 0, r.x, r.y, r.w, r.h, memory);
                }
                texture->SetStatus(ImTextureStatus_OK);
            } else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0) {
                bgfx::destroy(fromId(texture->GetTexID()));
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }

    const float width = data->DisplaySize.x, height = data->DisplaySize.y;
    if (width <= 0.0f || height <= 0.0f) return;
    bgfx::setViewName(ViewGui, "gui");
    bgfx::setViewMode(ViewGui, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(ViewGui, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewGui, 0, 0, uint16_t(width), uint16_t(height));
    float ortho[16];
    bx::mtxOrtho(ortho, 0.0f, width, height, 0.0f, 0.0f, 1000.0f, 0.0f,
                 bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(ViewGui, nullptr, ortho);
    bgfx::touch(ViewGui);

    for (int n = 0; n < data->CmdListsCount; ++n) {
        const ImDrawList* list = data->CmdLists[n];
        const uint32_t vertices = uint32_t(list->VtxBuffer.size());
        const uint32_t indices = uint32_t(list->IdxBuffer.size());
        if (bgfx::getAvailTransientVertexBuffer(vertices, layout_) < vertices ||
            bgfx::getAvailTransientIndexBuffer(indices, sizeof(ImDrawIdx) == 4) < indices) {
            break;   // out of transient room this frame: the rest of the panels wait a frame
        }
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, vertices, layout_);
        bgfx::allocTransientIndexBuffer(&tib, indices, sizeof(ImDrawIdx) == 4);
        std::memcpy(tvb.data, list->VtxBuffer.Data, vertices * sizeof(ImDrawVert));
        std::memcpy(tib.data, list->IdxBuffer.Data, indices * sizeof(ImDrawIdx));

        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                cmd.UserCallback(list, &cmd);
                continue;
            }
            if (cmd.ElemCount == 0) continue;
            const float x0 = bx::max(cmd.ClipRect.x, 0.0f), y0 = bx::max(cmd.ClipRect.y, 0.0f);
            const float x1 = bx::min(cmd.ClipRect.z, width), y1 = bx::min(cmd.ClipRect.w, height);
            if (x1 <= x0 || y1 <= y0) continue;
            bgfx::setScissor(uint16_t(x0), uint16_t(y0), uint16_t(x1 - x0), uint16_t(y1 - y0));
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                                 BGFX_STATE_BLEND_INV_SRC_ALPHA));
            const ImTextureID id = cmd.GetTexID();
            if (id != ImTextureID_Invalid) bgfx::setTexture(0, sampler_, fromId(id));
            bgfx::setVertexBuffer(0, &tvb, cmd.VtxOffset, vertices);
            bgfx::setIndexBuffer(&tib, cmd.IdxOffset, cmd.ElemCount);
            bgfx::submit(ViewGui, program_);
        }
    }
}

}  // namespace mu::gfx
