// The Windows/Linux port test: one MU monster from its glb, skinned on the CPU, its clips
// played in turn, drawn by bgfx through Vulkan. Not the game -- a check that bgfx, GLFW,
// cgltf and our shaders work off the Mac. docs/windows-linux-test.md; built by build.sh.
#include <bgfx/bgfx.h>
#include <bx/math.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

struct Vertex { float x, y, z, nx, ny, nz, u, v; };

struct Part {
    std::vector<float> pos, nrm, uv;     // bind pose, 3/3/2 per vertex
    std::vector<uint16_t> joints;        // 4 per vertex
    std::vector<float> weights;          // 4 per vertex
    std::vector<uint32_t> indices;
    std::vector<Vertex> skinned;
    bgfx::DynamicVertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
    bool glow = false;
};

// Column-major, as glTF and bx both store them: out = a * b.
static void mul(float* out, const float* a, const float* b) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    memcpy(out, r, sizeof r);
}

static bgfx::ShaderHandle loadShader(const char* dir, const char* name) {
    char path[512];
    snprintf(path, sizeof path, "shaders/%s/%s.bin", dir, name);
    FILE* f = fopen(path, "rb");
    if (!f) { printf("cannot read %s\n", path); return BGFX_INVALID_HANDLE; }
    std::vector<char> data; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    return bgfx::createShader(bgfx::copy(data.data(), uint32_t(data.size())));
}

static bgfx::TextureHandle loadTexture(const cgltf_texture_view& view) {
    if (!view.texture || !view.texture->image || !view.texture->image->buffer_view) {
        uint32_t white = 0xffffffff;
        return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&white, 4));
    }
    const cgltf_buffer_view* bv = view.texture->image->buffer_view;
    const auto* bytes = static_cast<const stbi_uc*>(cgltf_buffer_view_data(bv));
    int w, h, ch;
    stbi_uc* px = stbi_load_from_memory(bytes, int(bv->size), &w, &h, &ch, 4);
    auto t = bgfx::createTexture2D(uint16_t(w), uint16_t(h), false, 1, bgfx::TextureFormat::RGBA8,
                                   BGFX_SAMPLER_NONE, bgfx::copy(px, uint32_t(w * h * 4)));
    stbi_image_free(px);
    return t;
}

// Sets every animated node's translation/rotation/scale to the clip at time t.
static void pose(const cgltf_animation& anim, float t) {
    for (cgltf_size i = 0; i < anim.channels_count; ++i) {
        const cgltf_animation_channel& ch = anim.channels[i];
        const cgltf_animation_sampler& s = *ch.sampler;
        const cgltf_size n = s.input->count;
        float t0 = 0, t1 = 0;
        cgltf_size k = 0;
        for (; k + 1 < n; ++k) {
            cgltf_accessor_read_float(s.input, k + 1, &t1, 1);
            if (t1 > t) break;
        }
        cgltf_accessor_read_float(s.input, k, &t0, 1);
        const cgltf_size k1 = k + 1 < n ? k + 1 : k;
        cgltf_accessor_read_float(s.input, k1, &t1, 1);
        float f = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
        f = f < 0 ? 0 : (f > 1 ? 1 : f);
        if (s.interpolation == cgltf_interpolation_type_step) f = 0;
        float a[4], b[4];
        const int comps = ch.target_path == cgltf_animation_path_type_rotation ? 4 : 3;
        cgltf_accessor_read_float(s.output, k, a, comps);
        cgltf_accessor_read_float(s.output, k1, b, comps);
        cgltf_node* node = ch.target_node;
        if (ch.target_path == cgltf_animation_path_type_rotation) {
            float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
            const float sign = d < 0 ? -1.0f : 1.0f;
            float q[4], len = 0;
            for (int c = 0; c < 4; ++c) { q[c] = a[c] * (1 - f) + b[c] * sign * f; len += q[c] * q[c]; }
            len = std::sqrt(len);
            for (int c = 0; c < 4; ++c) node->rotation[c] = q[c] / len;
            node->has_rotation = 1;
        } else {
            float* dst = ch.target_path == cgltf_animation_path_type_translation ? node->translation : node->scale;
            for (int c = 0; c < 3; ++c) dst[c] = a[c] * (1 - f) + b[c] * f;
            if (ch.target_path == cgltf_animation_path_type_translation) node->has_translation = 1;
            else node->has_scale = 1;
        }
    }
}

#if defined(_WIN32)
static const char* kTitle = "MU2 - character on Windows (Wine)";
static const char* kWhere = "Windows via Wine";
#else
static const char* kTitle = "MU2 - character on Linux";
static const char* kWhere = "Linux";
#endif
static const char* kClipNames[] = {"stand", "stand 2", "walk", "attack 1", "attack 2", "hit", "die"};

int main(int argc, char** argv) {
    const char* file = "DeathKnight01.glb";
    bgfx::RendererType::Enum type = bgfx::RendererType::Vulkan;
    const char* dir = "spirv";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "d3d11")) { type = bgfx::RendererType::Direct3D11; dir = "dx11"; }
        else if (!strcmp(argv[i], "opengl")) { type = bgfx::RendererType::OpenGL; dir = "glsl"; }
        else file = argv[i];
    }

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* win = glfwCreateWindow(1280, 800, kTitle, nullptr, nullptr);
    int fw, fh;
    glfwGetFramebufferSize(win, &fw, &fh);
    bgfx::renderFrame();
    bgfx::Init init;
    init.type = type;
#if defined(_WIN32)
    init.swapChain.nwh = glfwGetWin32Window(win);
#else
    init.swapChain.nwh = (void*)(uintptr_t)glfwGetX11Window(win);
    init.swapChain.ndt = glfwGetX11Display();
#endif
    init.swapChain.width = uint32_t(fw);
    init.swapChain.height = uint32_t(fh);
    init.reset = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X4;
    if (!bgfx::init(init)) { printf("bgfx init failed\n"); return 1; }
    printf("renderer: %s\n", bgfx::getRendererName(bgfx::getRendererType()));
    fflush(stdout);
    bgfx::setDebug(BGFX_DEBUG_TEXT);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x181420ff, 1.0f, 0);

    cgltf_options opts = {};
    cgltf_data* gltf = nullptr;
    if (cgltf_parse_file(&opts, file, &gltf) != cgltf_result_success ||
        cgltf_load_buffers(&opts, gltf, file) != cgltf_result_success) {
        printf("cannot load %s\n", file);
        return 1;
    }
    const cgltf_skin* skin = gltf->skins_count ? &gltf->skins[0] : nullptr;
    std::vector<float> inverseBind(skin ? skin->joints_count * 16 : 0);
    for (cgltf_size j = 0; skin && j < skin->joints_count; ++j)
        cgltf_accessor_read_float(skin->inverse_bind_matrices, j, &inverseBind[j * 16], 16);

    std::vector<Part> parts;
    for (cgltf_size m = 0; m < gltf->meshes_count; ++m)
        for (cgltf_size p = 0; p < gltf->meshes[m].primitives_count; ++p) {
            const cgltf_primitive& prim = gltf->meshes[m].primitives[p];
            Part part;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                const cgltf_accessor* acc = at.data;
                auto readF = [&](std::vector<float>& out, int n) {
                    out.resize(acc->count * n);
                    for (cgltf_size v = 0; v < acc->count; ++v) cgltf_accessor_read_float(acc, v, &out[v * n], n);
                };
                if (at.type == cgltf_attribute_type_position) readF(part.pos, 3);
                else if (at.type == cgltf_attribute_type_normal) readF(part.nrm, 3);
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) readF(part.uv, 2);
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) readF(part.weights, 4);
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) {
                    part.joints.resize(acc->count * 4);
                    for (cgltf_size v = 0; v < acc->count; ++v) {
                        cgltf_uint j[4] = {};
                        cgltf_accessor_read_uint(acc, v, j, 4);
                        for (int c = 0; c < 4; ++c) part.joints[v * 4 + c] = uint16_t(j[c]);
                    }
                }
            }
            for (cgltf_size i = 0; i < prim.indices->count; ++i)
                part.indices.push_back(uint32_t(cgltf_accessor_read_index(prim.indices, i)));
            const size_t count = part.pos.size() / 3;
            if (part.nrm.size() < count * 3) part.nrm.assign(count * 3, 0.0f);
            if (part.uv.size() < count * 2) part.uv.assign(count * 2, 0.0f);
            part.skinned.resize(count);
            if (prim.material) {
                part.glow = prim.material->alpha_mode == cgltf_alpha_mode_blend;
                part.tex = loadTexture(part.glow && prim.material->emissive_texture.texture
                                           ? prim.material->emissive_texture
                                           : prim.material->pbr_metallic_roughness.base_color_texture);
            } else {
                part.tex = loadTexture(cgltf_texture_view{});
            }
            parts.push_back(std::move(part));
        }
    printf("%s: %zu parts, %zu joints, %zu clips\n", file, parts.size(),
           skin ? size_t(skin->joints_count) : size_t(0), size_t(gltf->animations_count));

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    for (Part& part : parts) {
        part.vbh = bgfx::createDynamicVertexBuffer(uint32_t(part.skinned.size()), layout);
        part.ibh = bgfx::createIndexBuffer(bgfx::copy(part.indices.data(), uint32_t(part.indices.size() * 4)),
                                          BGFX_BUFFER_INDEX32);
    }
    auto prog = bgfx::createProgram(loadShader(dir, "vs_skin"), loadShader(dir, "fs_skin"), true);
    auto sColor = bgfx::createUniform("s_color", bgfx::UniformType::Sampler);
    auto uMode = bgfx::createUniform("u_mode", bgfx::UniformType::Vec4);

    std::vector<float> jointMtx(skin ? skin->joints_count * 16 : 0);
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    bool framed = false;

    int clip = 0;
    bool manual = false;
    double clipStart = glfwGetTime(), last = clipStart;
    bool keyWas[GLFW_KEY_LAST + 1] = {};
    int frames = 0;
    float fps = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        const double now = glfwGetTime();
        const int clips = int(gltf->animations_count);
        auto pressed = [&](int key) {
            const bool down = glfwGetKey(win, key) == GLFW_PRESS;
            const bool hit = down && !keyWas[key];
            keyWas[key] = down;
            return hit;
        };
        for (int k = 0; k < clips && k < 9; ++k)
            if (pressed(GLFW_KEY_1 + k)) { clip = k; clipStart = now; manual = true; }
        if (pressed(GLFW_KEY_SPACE) && clips) { clip = (clip + 1) % clips; clipStart = now; manual = true; }
        if (pressed(GLFW_KEY_A)) manual = false;
        if (!manual && clips && now - clipStart > 4.0) { clip = (clip + 1) % clips; clipStart = now; }

        float clipLen = 0;
        if (clips) {
            const cgltf_animation& anim = gltf->animations[clip];
            for (cgltf_size i = 0; i < anim.samplers_count; ++i)
                clipLen = std::fmax(clipLen, anim.samplers[i].input->max[0]);
            pose(anim, clipLen > 0 ? float(std::fmod(now - clipStart, double(clipLen))) : 0.0f);
        }
        for (cgltf_size j = 0; skin && j < skin->joints_count; ++j) {
            float world[16];
            cgltf_node_transform_world(skin->joints[j], world);
            mul(&jointMtx[j * 16], world, &inverseBind[j * 16]);
        }
        for (Part& part : parts) {
            const size_t count = part.skinned.size();
            for (size_t v = 0; v < count; ++v) {
                const float* p = &part.pos[v * 3];
                const float* n = &part.nrm[v * 3];
                float sp[3] = {}, sn[3] = {};
                if (skin && !part.joints.empty()) {
                    for (int i = 0; i < 4; ++i) {
                        const float w = part.weights[v * 4 + i];
                        if (w <= 0) continue;
                        const float* m = &jointMtx[part.joints[v * 4 + i] * 16];
                        for (int r = 0; r < 3; ++r) {
                            sp[r] += w * (m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r]);
                            sn[r] += w * (m[r] * n[0] + m[4 + r] * n[1] + m[8 + r] * n[2]);
                        }
                    }
                } else {
                    memcpy(sp, p, sizeof sp);
                    memcpy(sn, n, sizeof sn);
                }
                part.skinned[v] = {sp[0], sp[1], sp[2], sn[0], sn[1], sn[2], part.uv[v * 2], part.uv[v * 2 + 1]};
                if (!framed)
                    for (int r = 0; r < 3; ++r) { lo[r] = std::fmin(lo[r], sp[r]); hi[r] = std::fmax(hi[r], sp[r]); }
            }
            bgfx::update(part.vbh, 0, bgfx::copy(part.skinned.data(), uint32_t(count * sizeof(Vertex))));
        }
        if (!framed) {
            framed = true;
            printf("bounds %.2f %.2f %.2f .. %.2f %.2f %.2f\n", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
            fflush(stdout);
        }

        // Framed on the first pose's bounds; glTF is Y up, the camera circles it slowly.
        const float height = hi[1] - lo[1];
        const float cx = (lo[0] + hi[0]) * 0.5f, cy = lo[1] + height * 0.5f, cz = (lo[2] + hi[2]) * 0.5f;
        const float dist = height * 1.9f, turn = float(now) * 0.35f;
        float view[16], proj[16], model[16];
        bx::mtxLookAt(view, {cx + std::sin(turn) * dist, cy + height * 0.25f, cz + std::cos(turn) * dist},
                      {cx, cy, cz}, {0, 1, 0}, bx::Handedness::Right);
        bx::mtxProj(proj, 40, float(fw) / float(fh), height * 0.05f, height * 20,
                    bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
        bx::mtxIdentity(model);
        bgfx::setViewRect(0, 0, 0, uint16_t(fw), uint16_t(fh));
        bgfx::setViewTransform(0, view, proj);
        for (int pass = 0; pass < 2; ++pass)
            for (Part& part : parts) {
                if (part.glow != (pass == 1)) continue;
                float mode[4] = {part.glow ? 1.0f : 0.0f, 0, 0, 0};
                bgfx::setTransform(model);
                bgfx::setUniform(uMode, mode);
                bgfx::setTexture(0, sColor, part.tex);
                bgfx::setVertexBuffer(0, part.vbh);
                bgfx::setIndexBuffer(part.ibh);
                uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
                state |= part.glow ? BGFX_STATE_BLEND_ADD : (BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z);
                bgfx::setState(state);
                bgfx::submit(0, prog);
            }

        ++frames;
        if (now - last > 0.5) { fps = float(frames / (now - last)); frames = 0; last = now; }
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f, "MU2 on %s -- bgfx: %s", kWhere,
                            bgfx::getRendererName(bgfx::getRendererType()));
        bgfx::dbgTextPrintf(1, 2, 0x0e, "%s  --  action%d \"%s\"  (%.2f s)   %.0f fps",
                            file, clip, clip < 7 ? kClipNames[clip] : "?", clipLen, fps);
        bgfx::dbgTextPrintf(1, 3, 0x07, "1-%d pick a clip, space next, A auto-cycle%s, esc quits",
                            clips, manual ? "" : " (on)");
        bgfx::frame();
    }
    for (Part& part : parts) { bgfx::destroy(part.vbh); bgfx::destroy(part.ibh); bgfx::destroy(part.tex); }
    bgfx::destroy(prog); bgfx::destroy(sColor); bgfx::destroy(uMode);
    bgfx::shutdown();
    glfwTerminate();
    cgltf_free(gltf);
}
