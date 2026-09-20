#include "content/mesh.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <bx/math.h>

#include <cmath>
#include <cstring>

#include "core/files.h"
#include "core/log.h"

namespace mu::content {
namespace {

bgfx::VertexLayout g_layout;
bool g_layoutReady = false;

const char* baseName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

// Reads an accessor into floats, `components` at a time. cgltf does the de-quantising and
// the stride, so a normalised byte accessor arrives as floats in [0,1] as glTF says it
// should.
void readAccessor(const cgltf_accessor* accessor, float* out, cgltf_size components) {
    if (!accessor) return;
    cgltf_accessor_unpack_floats(accessor, out, accessor->count * components);
}

// Where a texture's bytes are: a path beside the .glb, or a slice of the .glb's own buffer.
bgfx::TextureHandle textureFrom(const cgltf_texture_view& view, const std::string& dir,
                                Textures& textures, TextureRole role) {
    if (!view.texture || !view.texture->image) return BGFX_INVALID_HANDLE;
    const cgltf_image* image = view.texture->image;
    if (image->uri && std::strncmp(image->uri, "data:", 5) != 0) {
        // cgltf leaves percent-escapes in the URI; MU2's build has none, and a name that
        // does would fail to open loudly rather than quietly load the wrong file.
        return textures.load(core::join(dir, image->uri), role);
    }
    if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data) {
        const uint8_t* base = static_cast<const uint8_t*>(image->buffer_view->buffer->data);
        const uint8_t* bytes = base + image->buffer_view->offset;
        std::string name = std::string(image->name ? image->name : "embedded") + "@" + dir;
        return textures.loadFromMemory(name, bytes, uint32_t(image->buffer_view->size), role);
    }
    return BGFX_INVALID_HANDLE;
}

// The cutout threshold a material draws with, or -1 for no cutout. It is read off glTF's
// own `alpha_mode` and `alpha_cutoff`, which is what MU2's pipeline writes -- no pixel of
// the albedo is inspected here, and the cook of sprint 3 is where looking at the alpha
// would belong if the flag ever turns out to disagree with the texture. Decided once at
// load and not guessed in the shader; see docs/conventions.md.
float cutoutFor(const cgltf_material* material) {
    if (!material) return -1.0f;
    if (material->alpha_mode == cgltf_alpha_mode_mask) {
        return material->alpha_cutoff > 0.0f ? material->alpha_cutoff : 0.5f;
    }
    return -1.0f;
}

}  // namespace

const bgfx::VertexLayout& Mesh::layout() {
    if (!g_layoutReady) {
        g_layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        g_layoutReady = true;
    }
    return g_layout;
}

bool Mesh::load(const std::string& path, Textures& textures) {
    name_ = baseName(path);
    const std::string dir = core::directoryOf(path);

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        core::logError("%s is not a glTF cgltf can read", path.c_str());
        return false;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        core::logError("%s: its buffers did not load", path.c_str());
        cgltf_free(data);
        return false;
    }

    // Materials first, so a primitive can name one by index.
    materials_.reserve(data->materials_count + 1);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        Material out;
        out.name = m.name ? m.name : "material";
        out.twoSided = m.double_sided;
        out.cutout = cutoutFor(&m);
        if (m.has_pbr_metallic_roughness) {
            out.albedo = textureFrom(m.pbr_metallic_roughness.base_color_texture, dir, textures,
                                     TextureRole::Albedo);
            // Occlusion, roughness and metal are one texture in glTF's own layout: G is
            // roughness and B is metal, and MU2's pipeline writes occlusion into R of the
            // same file, which is why the occlusion view is not read separately.
            out.orm = textureFrom(m.pbr_metallic_roughness.metallic_roughness_texture, dir,
                                  textures, TextureRole::Data);
        }
        if (!bgfx::isValid(out.orm)) {
            out.orm = textureFrom(m.occlusion_texture, dir, textures, TextureRole::Data);
        }
        out.normal = textureFrom(m.normal_texture, dir, textures, TextureRole::Normal);
        out.emissive = textureFrom(m.emissive_texture, dir, textures, TextureRole::Emissive);

        if (!bgfx::isValid(out.albedo)) out.albedo = textures.white();
        if (!bgfx::isValid(out.normal)) out.normal = textures.flatNormal();
        if (!bgfx::isValid(out.orm)) out.orm = textures.neutralOrm();
        if (!bgfx::isValid(out.emissive)) out.emissive = textures.black();
        materials_.push_back(out);
    }
    // A primitive with no material of its own draws with this one.
    Material fallback;
    fallback.name = "none";
    fallback.albedo = textures.white();
    fallback.normal = textures.flatNormal();
    fallback.orm = textures.neutralOrm();
    fallback.emissive = textures.black();
    const uint32_t fallbackIndex = uint32_t(materials_.size());
    materials_.push_back(fallback);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<float> scratch;

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            const cgltf_accessor* aPos = nullptr;
            const cgltf_accessor* aNormal = nullptr;
            const cgltf_accessor* aTangent = nullptr;
            const cgltf_accessor* aUv = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& attr = prim.attributes[ai];
                switch (attr.type) {
                    case cgltf_attribute_type_position: aPos = attr.data; break;
                    case cgltf_attribute_type_normal: aNormal = attr.data; break;
                    case cgltf_attribute_type_tangent: aTangent = attr.data; break;
                    case cgltf_attribute_type_texcoord:
                        // The first set, whatever is in it, and the rest are dropped on the
                        // floor. MU2's build does NOT have one set a primitive: House01,
                        // this bench's own model, carries TEXCOORD_0 and TEXCOORD_1 on all
                        // four of its primitives, and only the first is read. That is right
                        // here because MU2's materials sample set 0 and set 1 is a lightmap
                        // or a second layer nothing in this frame asks for yet. It is NOT
                        // right for a Fab-converted figure, whose TEXCOORD_0 is zeroed with
                        // the real layout left in TEXCOORD_1 -- read as written, the whole
                        // figure samples one texel, which passes for black leather for a
                        // long time. Which set a primitive means is a cook-time decision and
                        // belongs with the figures in sprint 4, not as a guess here.
                        if (!aUv) aUv = attr.data;
                        break;
                    default: break;
                }
            }
            if (!aPos || aPos->count == 0) continue;

            const uint32_t baseVertex = uint32_t(vertices.size());
            const size_t count = size_t(aPos->count);
            vertices.resize(baseVertex + count);

            scratch.assign(count * 3, 0.0f);
            readAccessor(aPos, scratch.data(), 3);
            for (size_t i = 0; i < count; ++i) {
                std::memcpy(vertices[baseVertex + i].position, &scratch[i * 3], sizeof(float) * 3);
            }

            if (aNormal && aNormal->count == aPos->count) {
                scratch.assign(count * 3, 0.0f);
                readAccessor(aNormal, scratch.data(), 3);
                for (size_t i = 0; i < count; ++i) {
                    std::memcpy(vertices[baseVertex + i].normal, &scratch[i * 3], sizeof(float) * 3);
                }
            } else {
                for (size_t i = 0; i < count; ++i) {
                    vertices[baseVertex + i].normal[0] = 0.0f;
                    vertices[baseVertex + i].normal[1] = 1.0f;
                    vertices[baseVertex + i].normal[2] = 0.0f;
                }
            }

            if (aUv && aUv->count == aPos->count) {
                scratch.assign(count * 2, 0.0f);
                readAccessor(aUv, scratch.data(), 2);
                for (size_t i = 0; i < count; ++i) {
                    vertices[baseVertex + i].uv[0] = scratch[i * 2 + 0];
                    vertices[baseVertex + i].uv[1] = scratch[i * 2 + 1];
                }
            }

            bool haveTangents = aTangent && aTangent->count == aPos->count;
            if (haveTangents) {
                scratch.assign(count * 4, 0.0f);
                readAccessor(aTangent, scratch.data(), 4);
                for (size_t i = 0; i < count; ++i) {
                    std::memcpy(vertices[baseVertex + i].tangent, &scratch[i * 4], sizeof(float) * 4);
                }
            }

            const uint32_t firstIndex = uint32_t(indices.size());
            uint32_t indexCount = 0;
            if (prim.indices) {
                indexCount = uint32_t(prim.indices->count);
                indices.reserve(indices.size() + indexCount);
                for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                    indices.push_back(baseVertex +
                                      uint32_t(cgltf_accessor_read_index(prim.indices, i)));
                }
            } else {
                indexCount = uint32_t(count);
                indices.reserve(indices.size() + indexCount);
                for (uint32_t i = 0; i < indexCount; ++i) indices.push_back(baseVertex + i);
            }

            if (!haveTangents) {
                // glTF allows a model with a normal map and no tangents, and MU2's build has
                // some. Derived per triangle from the UVs and averaged, which is what a
                // normal map was authored against; a made-up frame turns lighting on its side.
                std::vector<float> accum(count * 3, 0.0f);
                // The bitangent is accumulated too, only so that its sign can be recovered.
                // glTF stores that sign in tangent.w, and MU's sheets mirror UV islands
                // heavily: assuming +1 lights the relief from the wrong side on half a model.
                std::vector<float> bitan(count * 3, 0.0f);
                for (uint32_t i = 0; i + 2 < indexCount; i += 3) {
                    const uint32_t i0 = indices[firstIndex + i + 0];
                    const uint32_t i1 = indices[firstIndex + i + 1];
                    const uint32_t i2 = indices[firstIndex + i + 2];
                    const Vertex& v0 = vertices[i0];
                    const Vertex& v1 = vertices[i1];
                    const Vertex& v2 = vertices[i2];
                    const float e1[3] = {v1.position[0] - v0.position[0],
                                         v1.position[1] - v0.position[1],
                                         v1.position[2] - v0.position[2]};
                    const float e2[3] = {v2.position[0] - v0.position[0],
                                         v2.position[1] - v0.position[1],
                                         v2.position[2] - v0.position[2]};
                    const float du1 = v1.uv[0] - v0.uv[0], dv1 = v1.uv[1] - v0.uv[1];
                    const float du2 = v2.uv[0] - v0.uv[0], dv2 = v2.uv[1] - v0.uv[1];
                    const float det = du1 * dv2 - du2 * dv1;
                    if (std::fabs(det) < 1e-12f) continue;
                    const float r = 1.0f / det;
                    const float t[3] = {(e1[0] * dv2 - e2[0] * dv1) * r,
                                        (e1[1] * dv2 - e2[1] * dv1) * r,
                                        (e1[2] * dv2 - e2[2] * dv1) * r};
                    const float bt[3] = {(e2[0] * du1 - e1[0] * du2) * r,
                                         (e2[1] * du1 - e1[1] * du2) * r,
                                         (e2[2] * du1 - e1[2] * du2) * r};
                    for (uint32_t v : {i0, i1, i2}) {
                        const size_t k = (v - baseVertex) * 3;
                        accum[k + 0] += t[0];
                        accum[k + 1] += t[1];
                        accum[k + 2] += t[2];
                        bitan[k + 0] += bt[0];
                        bitan[k + 1] += bt[1];
                        bitan[k + 2] += bt[2];
                    }
                }
                for (size_t i = 0; i < count; ++i) {
                    Vertex& v = vertices[baseVertex + i];
                    float t[3] = {accum[i * 3 + 0], accum[i * 3 + 1], accum[i * 3 + 2]};
                    // Gram-Schmidt against the normal, and any perpendicular will do where
                    // the triangles cancelled out.
                    const float d = t[0] * v.normal[0] + t[1] * v.normal[1] + t[2] * v.normal[2];
                    t[0] -= v.normal[0] * d;
                    t[1] -= v.normal[1] * d;
                    t[2] -= v.normal[2] * d;
                    float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
                    if (len < 1e-8f) {
                        const float up[3] = {0.0f, 0.0f, 1.0f};
                        t[0] = v.normal[1] * up[2] - v.normal[2] * up[1];
                        t[1] = v.normal[2] * up[0] - v.normal[0] * up[2];
                        t[2] = v.normal[0] * up[1] - v.normal[1] * up[0];
                        len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
                        if (len < 1e-8f) {
                            t[0] = 1.0f;
                            t[1] = 0.0f;
                            t[2] = 0.0f;
                            len = 1.0f;
                        }
                    }
                    v.tangent[0] = t[0] / len;
                    v.tangent[1] = t[1] / len;
                    v.tangent[2] = t[2] / len;
                    // glTF's own rule: w is the sign that makes cross(normal, tangent) point
                    // the way the UVs actually run.
                    const float cx = v.normal[1] * v.tangent[2] - v.normal[2] * v.tangent[1];
                    const float cy = v.normal[2] * v.tangent[0] - v.normal[0] * v.tangent[2];
                    const float cz = v.normal[0] * v.tangent[1] - v.normal[1] * v.tangent[0];
                    const float handed = cx * bitan[i * 3 + 0] + cy * bitan[i * 3 + 1] +
                                         cz * bitan[i * 3 + 2];
                    v.tangent[3] = handed < 0.0f ? -1.0f : 1.0f;
                }
            }

            Part part;
            part.firstIndex = firstIndex;
            part.indexCount = indexCount;
            part.material = fallbackIndex;
            if (prim.material) {
                part.material = uint32_t(prim.material - data->materials);
            }
            parts_.push_back(part);
        }
    }

    cgltf_free(data);

    if (vertices.empty() || indices.empty()) {
        core::logError("%s holds no triangles", path.c_str());
        return false;
    }

    std::vector<Material> materials = std::move(materials_);
    std::vector<Part> parts = std::move(parts_);
    materials_.clear();
    parts_.clear();
    return build(name_, std::move(vertices), std::move(indices), std::move(materials),
                 std::move(parts));
}

bool Mesh::buildFromCooked(const CookedMesh& cooked, const std::string& name,
                           const std::string& assetDir, Textures& textures) {
    // The cook has already done everything this used to do at load: flattened the nodes,
    // derived nothing (every primitive in MU2's build carries its own TANGENT), decided
    // each material's cutout off its alpha_mode, and written the textures out as .ktx with
    // their chains in them. What is left is to name the files and make the buffers.
    std::vector<Material> materials;
    materials.reserve(cooked.materials.size());
    for (const CookedMaterial& from : cooked.materials) {
        Material out;
        out.name = from.name;
        out.cutout = from.cutout;
        out.twoSided = from.twoSided;
        auto texture = [&](const std::string& path, TextureRole role) {
            bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
            if (!path.empty()) handle = textures.load(core::join(assetDir, path), role);
            return handle;
        };
        out.albedo = texture(from.albedo, TextureRole::Albedo);
        out.normal = texture(from.normal, TextureRole::Normal);
        out.orm = texture(from.orm, TextureRole::Data);
        out.emissive = texture(from.emissive, TextureRole::Emissive);
        if (!bgfx::isValid(out.albedo)) out.albedo = textures.white();
        if (!bgfx::isValid(out.normal)) out.normal = textures.flatNormal();
        if (!bgfx::isValid(out.orm)) out.orm = textures.white();
        if (!bgfx::isValid(out.emissive)) out.emissive = textures.black();
        materials.push_back(out);
    }

    std::vector<Part> parts;
    parts.reserve(cooked.parts.size());
    for (const CookedPart& from : cooked.parts) {
        parts.push_back(Part{from.firstIndex, from.indexCount, from.material});
    }

    // The layouts are the same 48 bytes in the same order, and cooked.h asserts it, so the
    // vertices are copied rather than converted.
    std::vector<Vertex> vertices(cooked.vertices.size());
    if (!vertices.empty()) {
        std::memcpy(vertices.data(), cooked.vertices.data(),
                    vertices.size() * sizeof(Vertex));
    }
    return build(name, std::move(vertices), std::vector<uint32_t>(cooked.indices),
                 std::move(materials), std::move(parts));
}

bool Mesh::build(const std::string& name, std::vector<Vertex> vertices,
                 std::vector<uint32_t> indices, std::vector<Material> materials,
                 std::vector<Part> parts) {
    name_ = name;
    materials_ = std::move(materials);
    parts_ = std::move(parts);

    if (vertices.empty() || indices.empty()) {
        core::logError("%s holds no triangles", name_.c_str());
        return false;
    }

    bounds_.min[0] = bounds_.min[1] = bounds_.min[2] = 1e30f;
    bounds_.max[0] = bounds_.max[1] = bounds_.max[2] = -1e30f;
    for (const Vertex& v : vertices) {
        for (int i = 0; i < 3; ++i) {
            bounds_.min[i] = std::min(bounds_.min[i], v.position[i]);
            bounds_.max[i] = std::max(bounds_.max[i], v.position[i]);
        }
    }
    float extent = 0.0f;
    for (int i = 0; i < 3; ++i) {
        bounds_.centre[i] = (bounds_.min[i] + bounds_.max[i]) * 0.5f;
        const float half = (bounds_.max[i] - bounds_.min[i]) * 0.5f;
        extent += half * half;
    }
    bounds_.radius = std::sqrt(extent);

    vertexCount_ = uint32_t(vertices.size());
    indexCount_ = uint32_t(indices.size());

    const bgfx::Memory* vmem =
        bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(Vertex)));
    vbh_ = bgfx::createVertexBuffer(vmem, layout());
    const bgfx::Memory* imem =
        bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint32_t)));
    ibh_ = bgfx::createIndexBuffer(imem, BGFX_BUFFER_INDEX32);

    core::logf("model %s: %u triangles, %u vertices, %zu parts, %zu materials", name_.c_str(),
               triangleCount(), vertexCount_, parts_.size(), materials_.size());
    core::logf("  bounds (%.1f %.1f %.1f) to (%.1f %.1f %.1f), radius %.1f", bounds_.min[0],
               bounds_.min[1], bounds_.min[2], bounds_.max[0], bounds_.max[1], bounds_.max[2],
               bounds_.radius);
    return bgfx::isValid(vbh_) && bgfx::isValid(ibh_);
}

void Mesh::shutdown() {
    if (bgfx::isValid(vbh_)) bgfx::destroy(vbh_);
    if (bgfx::isValid(ibh_)) bgfx::destroy(ibh_);
    vbh_ = BGFX_INVALID_HANDLE;
    ibh_ = BGFX_INVALID_HANDLE;
    parts_.clear();
    materials_.clear();
}

}  // namespace mu::content
