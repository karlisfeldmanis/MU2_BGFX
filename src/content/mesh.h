// A .glb into buffers bgfx can draw. Static only for now; the skinned layout arrives with
// the figures in sprint 4.
#pragma once

#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/texture.h"

namespace mu::content {

struct Vertex {
    float position[3];
    float normal[3];
    float tangent[4];  // w is the bitangent's sign, as glTF has it
    float uv[2];
};
static_assert(sizeof(Vertex) == 48, "the vertex layout drifted");

// One material, closed: four maps and three flags, and nothing else. docs/conventions.md.
struct Material {
    bgfx::TextureHandle albedo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normal = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle orm = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle emissive = BGFX_INVALID_HANDLE;
    // Below this alpha a pixel is discarded, in every pass. Negative means no cutout, which
    // is the whole test: a cutout is decided when the model is read, not guessed per pixel.
    float cutout = -1.0f;
    bool twoSided = false;
    std::string name;
};

// One draw: a range of indices sharing a material.
struct Part {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t material = 0;
};

struct Bounds {
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    float centre[3] = {0, 0, 0};
    float radius = 0.0f;
};

class Mesh {
public:
    bool load(const std::string& path, Textures& textures);

    // A mesh made rather than read: the bench's ground, and later the terrain's chunks.
    // Takes the vectors, works out the bounds, and makes the buffers.
    bool build(const std::string& name, std::vector<Vertex> vertices,
               std::vector<uint32_t> indices, std::vector<Material> materials,
               std::vector<Part> parts);

    void shutdown();

    const std::vector<Part>& parts() const { return parts_; }
    const std::vector<Material>& materials() const { return materials_; }
    bgfx::VertexBufferHandle vertexBuffer() const { return vbh_; }
    bgfx::IndexBufferHandle indexBuffer() const { return ibh_; }
    const Bounds& bounds() const { return bounds_; }
    uint32_t triangleCount() const { return indexCount_ / 3; }
    uint32_t vertexCount() const { return vertexCount_; }
    const std::string& name() const { return name_; }

    // The layout every static draw uses. Valid after the first Mesh::load in the process.
    static const bgfx::VertexLayout& layout();

private:
    std::string name_;
    std::vector<Part> parts_;
    std::vector<Material> materials_;
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    uint32_t indexCount_ = 0;
    uint32_t vertexCount_ = 0;
    Bounds bounds_;
};

}  // namespace mu::content
