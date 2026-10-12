// Dear ImGui's configuration for the studio: CMakeLists.txt copies it into the build as the
// imconfig.h beside imgui.h (mu_imgui).
//
// Ours rather than bgfx's 3rdparty/dear-imgui/imconfig.h, which wants bgfx's example allocator,
// its widgets and stb compiled elsewhere. With this one ImGui keeps its own stb_truetype and
// stb_rect_pack static inside imgui_draw.cpp, so they cannot meet the overlay's.
#pragma once

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
