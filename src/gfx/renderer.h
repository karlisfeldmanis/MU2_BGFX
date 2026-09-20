// The frame: six views, in the order docs/conventions.md fixes them. Knows meshes and
// materials and nothing about the game — no map, no figure, no rules.
#pragma once

#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/mesh.h"
#include "gfx/lighting.h"

namespace mu::gfx {

struct Camera {
    float position[3] = {0, 0, 0};
    float target[3] = {0, 0, 0};
    float up[3] = {0, 1, 0};
    float fovDegrees = 30.0f;
    float nearPlane = 10.0f;
    float farPlane = 20000.0f;
};

// One thing to draw: a mesh and where it stands. Instances of the same mesh become one draw.
struct Drawable {
    const content::Mesh* mesh = nullptr;
    float transform[16];
};

class Renderer {
public:
    bool init(int width, int height, const std::string& shaderDir);
    void shutdown();
    void resize(int width, int height);

    // The whole frame. `drawables` may hold the same mesh many times.
    void draw(const Camera& camera, const Lighting& lighting,
              const std::vector<Drawable>& drawables);

    uint32_t lastDrawCount() const { return drawCount_; }

private:
    struct Batch {
        const content::Mesh* mesh = nullptr;
        uint32_t first = 0;   // into the frame's instance buffer
        uint32_t count = 0;
    };

    bool createTargets(int width, int height);
    void destroyTargets();
    bool loadPrograms(const std::string& shaderDir);
    void submitBatches(bgfx::ViewId view, bgfx::ProgramHandle program,
                       const std::vector<Batch>& batches, const bgfx::InstanceDataBuffer& idb,
                       uint64_t state, bool bindMaterial);
    void screenPass(bgfx::ViewId view, bgfx::ProgramHandle program);

    int width_ = 0;
    int height_ = 0;
    uint32_t drawCount_ = 0;

    // The shadow map is square and fixed; a split framed on the camera does not want to
    // change size with the window.
    static constexpr uint16_t kShadowSize = 2048;

    bgfx::FrameBufferHandle shadowFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowMap_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle prepassFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle prepassColour_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sceneDepth_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle ssaoFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ssaoTex_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle blurFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blurTex_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadeFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadeColour_ = BGFX_INVALID_HANDLE;

    bgfx::ProgramHandle shadowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle prepassProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle ssaoProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle blurProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shadeProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle presentProgram_ = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle uSunDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSunColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSkyColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGroundColour_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uCamPos_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uMaterial_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uShadowMtx_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uShadowParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uCamInvProj_ = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle sAlbedo_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sNormal_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sOrm_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sEmissive_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sShadowCompare_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sShadowDepth_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sPrepass_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sAo_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sColour_ = BGFX_INVALID_HANDLE;

    bgfx::VertexBufferHandle screenVb_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout screenLayout_;

    std::vector<Batch> batches_;
};

}  // namespace mu::gfx
